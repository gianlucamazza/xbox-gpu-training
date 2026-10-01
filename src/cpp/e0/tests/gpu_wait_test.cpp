#include "model.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
struct Fake {
  uint64_t clock = 0, done = 0;
  int waits = 0, signals = 0, arms = 0;
  std::string device, signal_error, arm_error;
  bool finish = true, sentinel = false, remove = false;
  GpuWaitApi::Result result = GpuWaitApi::Result::Wake;
  GpuWaitApi api() {
    return {[&] { return clock; },
            [&] { return sentinel ? UINT64_MAX : done; },
            [&] { return device; },
            [&](uint64_t) {
              ++signals;
              return signal_error;
            },
            [&](uint64_t) {
              ++arms;
              return arm_error;
            },
            [&](uint32_t ms) {
              require(ms <= 250 && ms > 0, "poll out of bounds");
              ++waits;
              clock += ms;
              if (finish)
                done = 7;
              if (remove)
                device = "DXGI_ERROR_DEVICE_REMOVED";
              return result;
            },
            [] { return std::string("ERROR_INVALID_HANDLE"); }};
  }
};
struct FailingKernel : e0::Kernel {
  e0::CpuKernel cpu;
  e0::Tensor run(const e0::Command &, const e0::Tensor &, const e0::Tensor &,
                 const e0::Tensor &, const e0::Tensor &,
                 const e0::Tensor &) override {
    runtime_fault = {"gpu_wait_failed", "synthetic failed wait", 7, 6, 250};
    throw std::runtime_error("synthetic failed wait");
  }
  std::vector<e0::Values> read(const std::vector<e0::Tensor> &t) override {
    return cpu.read(t);
  }
};
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  try {
    {
      Fake f;
      auto api = f.api();
      GpuRuntimeFault fault;
      require(BoundedGpuWait(api, 7, fault), "healthy failed");
      require(f.waits == 1, "healthy waits");
    }
    {
      Fake f;
      f.done = 7;
      auto api = f.api();
      GpuRuntimeFault fault;
      require(BoundedGpuWait(api, 7, fault) && f.waits == 0,
              "already complete");
    }
    {
      Fake f;
      f.finish = false;
      auto api = f.api();
      GpuRuntimeFault fault;
      require(!BoundedGpuWait(api, 7, fault) &&
                  fault.kind == "gpu_wait_timeout",
              "timeout missing");
      require(f.waits == 2400 && fault.elapsed_ms == 600000, "deadline wrong");
      const auto waits = f.waits, signals = f.signals;
      require(!BoundedGpuWait(api, 8, fault) && f.waits == waits &&
                  f.signals == signals,
              "poison reused");
    }
    for (auto result :
         {GpuWaitApi::Result::Failed, GpuWaitApi::Result::Unexpected}) {
      Fake f;
      f.result = result;
      auto api = f.api();
      GpuRuntimeFault fault;
      require(!BoundedGpuWait(api, 7, fault) && fault.kind == "gpu_wait_failed",
              "wait failure ignored");
    }
    {
      Fake f;
      f.remove = true;
      auto api = f.api();
      GpuRuntimeFault fault;
      require(!BoundedGpuWait(api, 7, fault) &&
                  fault.kind == "gpu_device_removed",
              "device loss ignored");
    }
    {
      Fake f;
      f.sentinel = true;
      auto api = f.api();
      GpuRuntimeFault fault;
      require(!BoundedGpuWait(api, 7, fault) && f.signals == 0,
              "UINT64_MAX accepted");
    }
    for (bool signal : {false, true}) {
      Fake f;
      (signal ? f.signal_error : f.arm_error) = "HRESULT";
      auto api = f.api();
      GpuRuntimeFault fault;
      require(!BoundedGpuWait(api, 7, fault) && fault.kind == "gpu_fence_error",
              "fence error ignored");
    }
    {
      Fake f;
      f.finish = false;
      f.result = GpuWaitApi::Result::Wake;
      auto api = f.api();
      GpuRuntimeFault fault;
      require(!BoundedGpuWait(api, 7, fault, 700) && fault.elapsed_ms == 700,
              "spurious wake accepted");
    }
    const auto root =
        std::filesystem::temp_directory_path() /
        ("xgpu-scientific-probe-refusal-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto file = root / "job.json";
    e0::atomic_json(file,
                    {{"schema", "floppylm.e0.job.v1"},
                     {"runtime_fault_probe", {{"kind", "gpu_wait_timeout"}}}});
    e0::CpuKernel kernel;
    bool refused = false;
    try {
      e0::run_job(file, kernel);
    } catch (const std::exception &e) {
      refused = std::string(e.what()).find("forbidden for scientific") !=
                std::string::npos;
    }
    require(refused, "scientific fault probe accepted");
    const auto initial = e0::read_json(
        std::filesystem::path(argv[1]) /
        "contracts/floppylm/fixtures/valid/floppylm.e0.weights.v1--tiny.json");
    e0::atomic_json(root / "initial.json", initial);
    {
      std::ofstream data(root / "data.bin", std::ios::binary);
      data << std::string(64, 'a');
    }
    {
      std::ofstream indices(root / "indices.bin", std::ios::binary);
      indices << std::string(64, '\0');
    }
    auto desc = [&](const std::filesystem::path &p) {
      return e0::Json{{"path", e0::path_text(p.lexically_relative(root))},
                      {"bytes", std::filesystem::file_size(p)},
                      {"sha256", e0::sha256_file(p)}};
    };
    e0::Json job{{"schema", "floppylm.e0.job.v1"},
                 {"job_id", "trial"},
                 {"config", initial.at("config")},
                 {"initialization", desc(root / "initial.json")},
                 {"data", desc(root / "data.bin")},
                 {"indices", desc(root / "indices.bin")},
                 {"spec",
                  {{"batch", 1},
                   {"tokens", 16},
                   {"branches", 3},
                   {"warmup_frac", 0.02},
                   {"cooldown_frac", 0.1},
                   {"lr", 0.001},
                   {"wd", 0.0}}}};
    e0::atomic_json(file, job);
    const auto stopped = e0::run_job(file, kernel, 1);
    const auto ckpt = root / "results/trial/checkpoint.json";
    const auto saved = e0::sha256_file(ckpt);
    require(stopped.at("state") == "interrupted",
            "initial checkpoint unavailable");
    job["resume"] = desc(ckpt);
    e0::atomic_json(file, job);
    FailingKernel failed;
    try {
      e0::run_job(file, failed);
    } catch (const std::exception &) {
    }
    const auto result = e0::read_json(root / "results/trial/status.json");
    require(result.at("state") == "failed" &&
                result.at("runtime_fault").at("kind") == "gpu_wait_failed",
            "runtime fault classification absent");
    require(e0::sha256_file(ckpt) == saved &&
                result.at("checkpoint").at("sha256") == saved,
            "runtime fault overwrote persisted checkpoint");
    e0::CpuKernel branch_kernel;
    branch_kernel.progress_callback = [&](const std::string &phase,
                                          uint64_t trunk, uint64_t) {
      if (phase == "trunk" && trunk == 2) {
        branch_kernel.runtime_fault = {"gpu_wait_failed",
                                       "after published branch", 8, 7, 250};
        throw std::runtime_error("after published branch");
      }
    };
    try {
      e0::run_job(file, branch_kernel);
    } catch (const std::exception &) {
    }
    const auto published = e0::read_json(root / "results/trial/status.json");
    require(published.at("branches").size() == 1,
            "branch publication fixture failed");
    const auto branch = root / "results/trial/branch-2.json";
    const auto branch_hash = e0::sha256_file(branch);
    e0::CpuKernel resumed;
    bool replayed = false;
    resumed.progress_callback = [&](const std::string &phase, uint64_t,
                                    uint64_t cooldown) {
      if (phase == "cooldown" && cooldown == 2)
        replayed = true;
    };
    const auto completed = e0::run_job(file, resumed);
    require(completed.at("state") == "completed" && !replayed &&
                e0::sha256_file(branch) == branch_hash,
            "published branch was recomputed or changed during resume");
    std::filesystem::remove_all(root);
    std::cout
        << "GPU bounded wait policy and scientific probe rejection passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
