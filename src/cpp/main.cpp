// xbox-gpu-training host.
// English comments only. Default path: DirectX 12 device + hello compute dispatch.
// --smoke keeps the no-GPU compile check. No CUDA. DirectML is not the trainer.

#include "hello_dispatch.h"

#include <filesystem>
#include <iostream>
#include <string>

static void PrintBanner() {
  std::cout << "xbox-gpu-training host (Fase 0)\n";
  std::cout << "Target path: DirectX 12 compute shaders (HLSL). No CUDA.\n";
  std::cout << "DirectML is inference/forward-focused on console and is not the trainer.\n";
}

static void PrintReport(const RunReport& report) {
  std::cout << report.line << "\n";
  if (!report.detail.empty()) {
    std::cout << report.detail << "\n";
  }
}

int main(int argc, char** argv) {
  bool smoke_only = false;
  std::filesystem::path shader_hint;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--smoke") {
      smoke_only = true;
    } else if (arg == "--hello-compute") {
      // default path; accepted for explicit scripts
    } else if (arg == "--shader" && i + 1 < argc) {
      shader_hint = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      PrintBanner();
      std::cout << "\nUsage: xbox_gpu_host [--smoke] [--hello-compute] [--shader <cso-or-hlsl>]\n";
      std::cout << "  --smoke          Print the banner and exit 0 (no device create).\n";
      std::cout << "  --hello-compute  Create a D3D12 device and dispatch hello_compute once.\n";
      std::cout << "  default          Same as --hello-compute.\n";
      return 0;
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      return 1;
    }
  }

  PrintBanner();
  if (smoke_only) {
    std::cout << "STATUS: smoke (no D3D12 device create)\n";
    return 0;
  }

  const RunReport report = RunHelloCompute(shader_hint);
  PrintReport(report);
  return report.status == RunStatus::Failed ? 1 : 0;
}
