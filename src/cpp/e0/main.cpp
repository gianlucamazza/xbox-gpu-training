#include "model.h"
#include <iostream>
#include <memory>
#include <stdexcept>

int main(int argc, char **argv) {
  try {
    std::string job, fixture, optimizer, kernels, out, shader = "e0_tensor.cso";
    bool reference = false;
    uint64_t stop = 0;
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--reference")
        reference = true;
      else if (arg == "--job" && i + 1 < argc)
        job = argv[++i];
      else if (arg == "--optimizer-fixture" && i + 1 < argc)
        optimizer = argv[++i];
      else if (arg == "--kernel-fixture" && i + 1 < argc)
        kernels = argv[++i];
      else if (arg == "--fixture" && i + 1 < argc)
        fixture = argv[++i];
      else if (arg == "--out" && i + 1 < argc)
        out = argv[++i];
      else if (arg == "--shader" && i + 1 < argc)
        shader = argv[++i];
      else if (arg == "--stop-after" && i + 1 < argc)
        stop = std::stoull(argv[++i]);
      else if (arg == "--help") {
        std::cout
            << "xgpu_e0 (--job job.json | --fixture fixture.json | "
               "--kernel-fixture kernels.json | --optimizer-fixture "
               "optimizer.json) "
               "[--out result.json] [--reference] [--shader e0_tensor.cso] "
               "[--stop-after N]\n";
        return 0;
      } else
        throw std::runtime_error("unknown/missing argument: " + arg);
    }
    if (!optimizer.empty()) {
      if (out.empty() || !job.empty() || !fixture.empty() || !kernels.empty())
        throw std::runtime_error(
            "optimizer fixture requires --out and no job/fixture");
      e0::atomic_json(out,
                      e0::optimizer_fixture_report(e0::read_json(optimizer)));
      return 0;
    }
    if (unsigned(!job.empty()) + unsigned(!fixture.empty()) +
            unsigned(!kernels.empty()) !=
        1)
      throw std::runtime_error(
          "choose one of --job, --fixture or --kernel-fixture");
    std::unique_ptr<e0::Kernel> kernel;
    if (reference)
      kernel = std::make_unique<e0::CpuKernel>();
    else
      kernel = e0::make_gpu(shader);
    if (!kernels.empty()) {
      if (out.empty())
        throw std::runtime_error("--kernel-fixture requires --out");
      e0::atomic_json(
          out, e0::kernel_fixture_report(e0::read_json(kernels), *kernel));
    } else if (!fixture.empty()) {
      if (out.empty())
        throw std::runtime_error("--fixture requires --out");
      e0::atomic_json(out, e0::fixture_report(e0::read_json(fixture), *kernel));
    } else
      std::cout << e0::run_job(job, *kernel, stop).dump() << "\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
