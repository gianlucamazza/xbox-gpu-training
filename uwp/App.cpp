#include "App.h"
#include "../src/cpp/e0/model.h"
#include "../src/cpp/e0/runtime_lifecycle.h"
#include "App.g.cpp"
#include "DashboardView.h"
#include "pch.h"
#include <atomic>
#include <bcrypt.h>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#ifndef XGPU_COMMIT
#define XGPU_COMMIT "unknown"
#endif
namespace {
std::filesystem::path inbox;
std::mutex active_mutex;
std::condition_variable active_changed;
std::filesystem::path active_job;
std::unique_ptr<xgpu::DashboardView> dashboard;
std::string worker_uuid() {
  unsigned char uuid[16];
  if (BCryptGenRandom(nullptr, uuid, sizeof(uuid),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
    throw std::runtime_error("worker UUID generation failed");
  uuid[6] = (uuid[6] & 0x0f) | 0x40;
  uuid[8] = (uuid[8] & 0x3f) | 0x80;
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (unsigned i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10)
      out << '-';
    out << std::setw(2) << unsigned(uuid[i]);
  }
  return out.str();
}
void worker() {
  winrt::init_apartment();
  std::unique_ptr<e0::WorkerLock> ownership;
  std::unique_ptr<e0::RuntimeLifecycle> lifecycle;
  try {
    auto local = std::filesystem::path(
        winrt::Windows::Storage::ApplicationData::Current()
            .LocalFolder()
            .Path()
            .c_str());
    inbox = local / L"inbox";
    std::filesystem::create_directories(inbox);
    ownership = std::make_unique<e0::WorkerLock>(local / L"worker.lock");
    lifecycle = std::make_unique<e0::RuntimeLifecycle>(
        local, worker_uuid(), GetCurrentProcessId(),
        winrt::to_string(winrt::Windows::ApplicationModel::Package::Current()
                             .Id()
                             .FullName()),
        XGPU_COMMIT);
    lifecycle->on_published_fence_frozen(
        [](const GpuRuntimeFault &) { ExitProcess(1); });
    lifecycle->start();
    e0::reconcile_claims(inbox, lifecycle->snapshot());
    auto shader = std::filesystem::path(
                      winrt::Windows::ApplicationModel::Package::Current()
                          .InstalledLocation()
                          .Path()
                          .c_str()) /
                  L"Assets" / L"e0_tensor.cso";
    auto kernel = e0::make_gpu(e0::path_text(shader));
    kernel->progress_callback = [&](const std::string &phase, uint64_t trunk,
                                    uint64_t cooldown) {
      lifecycle->progress(phase, trunk, cooldown);
    };
    kernel->gpu_progress = [&](uint64_t fence, const std::string &operation) {
      lifecycle->gpu_progress(fence, operation);
    };
    lifecycle->ready();
    lifecycle->publish();
    e0::atomic_json(
        local / L"device.json",
        {{"schema", "floppylm.device.v1"},
         {"hardware_gpu", kernel->hardware()},
         {"adapter", kernel->adapter()},
         {"package",
          winrt::to_string(winrt::Windows::ApplicationModel::Package::Current()
                               .Id()
                               .FullName())},
         {"state", "ready"},
         {"capabilities", e0::capabilities()},
         {"commit", XGPU_COMMIT}});
    while (true) {
      if (lifecycle->snapshot().at("state") == "failed")
        throw std::runtime_error(
            "worker lifecycle failed; explicit app restart required");
      for (const auto &entry : std::filesystem::directory_iterator(inbox)) {
        if (entry.path().extension() != L".ready")
          continue;
        auto stem = entry.path().stem().wstring();
        const auto job_id = winrt::to_string(stem);
        if (job_id.empty() || job_id.find_first_not_of(
                                  "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQR"
                                  "STUVWXYZ0123456789-_") != std::string::npos)
          continue;
        auto claimed = inbox / (stem + L".claimed");
        if (!MoveFileExW(entry.path().c_str(), claimed.c_str(),
                         MOVEFILE_REPLACE_EXISTING))
          continue;
        auto job = inbox / (stem + L".job.json");
        {
          std::lock_guard<std::mutex> lock(active_mutex);
          active_job = job;
        }
        bool execution_admitted = false;
        std::string submitted_hash;
        try {
          const auto id = winrt::to_string(stem);
          const auto owned_job = e0::persist_claim(
              inbox, id, lifecycle->snapshot(), &submitted_hash);
          execution_admitted = true;
          lifecycle->running(e0::read_json(inbox / (id + ".owner.json")));
          lifecycle->publish();
          const auto content = e0::read_json(owned_job);
          if (content.contains("job_id") &&
              content.at("job_id").get<std::string>() != winrt::to_string(stem))
            throw std::runtime_error("job_id must match job file name");
          if (content.value("schema", "") == "floppylm.e0.fixture.v1")
            e0::atomic_json(inbox / (stem + L".actual.json"),
                            e0::fixture_report(content, *kernel));
          else if (content.value("schema", "") == "floppylm.e0.kernels.v1")
            e0::atomic_json(inbox / (stem + L".actual.json"),
                            e0::kernel_fixture_report(content, *kernel));
          else if (content.value("schema", "") == "floppylm.e0.optimizer.v1")
            e0::atomic_json(inbox / (stem + L".actual.json"),
                            e0::optimizer_fixture_report(content));
          else
            e0::run_job(owned_job, *kernel,
                        content.value("stop_after", uint64_t(0)));
        } catch (const std::exception &error) {
          auto out = inbox / L"results" / stem;
          std::filesystem::create_directories(out);
          e0::Json report = e0::Json::object();
          try {
            report = e0::read_json(out / L"status.json");
          } catch (...) {
          }
          const bool rejected_existing =
              !execution_admitted &&
              std::filesystem::exists(out / L"status.json");
          const bool preserve_existing =
              rejected_existing ||
              (report.contains("state") &&
               report.value("state", "") != "running" && !kernel->poisoned());
          if (rejected_existing) {
            e0::Json rejection{{"job_id", winrt::to_string(stem)},
                               {"state", "failed"},
                               {"error", error.what()}};
            if (!submitted_hash.empty())
              rejection["job_sha256"] = submitted_hash;
            e0::atomic_json(inbox / (stem + L".rejected.json"), rejection);
          }
          if (!preserve_existing) {
            report["state"] = "failed";
            report["error"] = error.what();
            report["job_id"] = winrt::to_string(stem);
          }
          if (kernel->poisoned()) {
            const auto &fault = kernel->runtime_fault;
            const e0::Json metadata{{"kind", fault.kind},
                                    {"error", fault.error},
                                    {"requested_fence", fault.requested_fence},
                                    {"completed_fence", fault.completed_fence},
                                    {"elapsed_ms", fault.elapsed_ms}};
            report["runtime_fault"] = metadata;
            lifecycle->fail(metadata);
          }
          if (!preserve_existing)
            e0::atomic_json(out / L"status.json", report);
        }
        {
          std::lock_guard<std::mutex> lock(active_mutex);
          active_job.clear();
        }
        active_changed.notify_all();
        if (kernel->poisoned()) {
          lifecycle->publish();
          try {
            e0::atomic_json(local / L"device.json",
                            {{"state", "failed"},
                             {"error", kernel->runtime_fault.kind},
                             {"hardware_gpu", false},
                             {"commit", XGPU_COMMIT}});
          } catch (...) {
          }
          // Keep ownership, heartbeat and the quarantined GPU alive. Only an
          // explicit process restart can create a fresh device or accept work.
          while (true)
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        lifecycle->ready();
        lifecycle->publish();
      }
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  } catch (const std::exception &error) {
    // A second worker must not overwrite the current owner's readiness files.
    if (!ownership)
      return;
    if (lifecycle) {
      if (lifecycle->snapshot().at("fault").is_null())
        lifecycle->fail({{"kind", "worker_failure"},
                         {"error", error.what()},
                         {"requested_fence", 0},
                         {"completed_fence", 0},
                         {"elapsed_ms", 0}});
      try {
        lifecycle->publish();
      } catch (...) {
      }
    }
    auto local = std::filesystem::path(
        winrt::Windows::Storage::ApplicationData::Current()
            .LocalFolder()
            .Path()
            .c_str());
    try {
      e0::atomic_json(local / L"device.json", {{"state", "failed"},
                                               {"error", error.what()},
                                               {"hardware_gpu", false},
                                               {"commit", XGPU_COMMIT}});
    } catch (...) {
    }
    while (true)
      std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}
} // namespace
namespace winrt::Xgpu::implementation {
App::App() {
  Suspending([](auto const &, auto const &args) {
    auto deferral = args.SuspendingOperation().GetDeferral();
    {
      std::lock_guard<std::mutex> lock(active_mutex);
      if (!active_job.empty()) {
        std::ofstream file(active_job.parent_path() /
                           (active_job.stem().stem().wstring() + L".cancel"));
        file << "suspend";
      }
    }
    std::thread([deferral]() mutable {
      std::unique_lock<std::mutex> lock(active_mutex);
      active_changed.wait_for(lock, std::chrono::seconds(4),
                              [] { return active_job.empty(); });
      lock.unlock();
      deferral.Complete();
    }).detach();
  });
}
void App::OnLaunched(
    Windows::ApplicationModel::Activation::LaunchActivatedEventArgs const &) {
  auto window = Windows::UI::Xaml::Window::Current();
  if (!window.Content()) {
    auto local =
        std::filesystem::path(Windows::Storage::ApplicationData::Current()
                                  .LocalFolder()
                                  .Path()
                                  .c_str());
    dashboard = std::make_unique<xgpu::DashboardView>(local, [] {
      std::lock_guard<std::mutex> lock(active_mutex);
      return active_job;
    });
    window.Content(dashboard->root());
    std::thread(worker).detach();
  }
  window.Activate();
}
} // namespace winrt::Xgpu::implementation
int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  try {
    winrt::Windows::UI::Xaml::Application::Start(
        [](auto &&) { winrt::make<winrt::Xgpu::implementation::App>(); });
    return 0;
  } catch (...) {
    return 1;
  }
}
