// xbox-gpu-training host.
// English comments only. Default path: DirectX 12 device + hello compute dispatch.
// --bench matmul writes a CSV vs the CPU reference. --cpu-ref is CPU-only.
// --smoke keeps the no-GPU compile check. No CUDA. DirectML is not the trainer.

#include "cpu_matmul.h"
#include "hello_dispatch.h"
#include "matmul_dispatch.h"

#include <filesystem>
#include <iostream>
#include <string>

static void PrintBanner() {
  std::cout << "xbox-gpu-training host (Fase 1)\n";
  std::cout << "Target path: DirectX 12 compute shaders (HLSL). No CUDA.\n";
  std::cout << "DirectML is inference/forward-focused on console and is not the trainer.\n";
}

static void PrintReport(const RunReport& report) {
  std::cout << report.line << "\n";
  if (!report.detail.empty()) {
    std::cout << report.detail << "\n";
  }
}

static void PrintUsage() {
  PrintBanner();
  std::cout << "\nUsage:\n";
  std::cout << "  xbox_gpu_host [--smoke] [--hello-compute] [--shader <cso-or-hlsl>]\n";
  std::cout << "  xbox_gpu_host --cpu-ref\n";
  std::cout << "  xbox_gpu_host --bench matmul [--out <csv>] [--shader <cso-or-hlsl>]\n";
  std::cout << "  --smoke          Print the banner and exit 0 (no device create).\n";
  std::cout << "  --hello-compute  Create a D3D12 device and dispatch hello_compute once.\n";
  std::cout << "  --cpu-ref        Run the portable CPU GEMM tests (no GPU).\n";
  std::cout << "  --bench matmul   CPU baseline + GPU matmul when a D3D12 device exists.\n";
  std::cout << "                   Writes CSV (schema + status). No invented tok/s.\n";
  std::cout << "  default          Same as --hello-compute.\n";
}

int main(int argc, char** argv) {
  bool smoke_only = false;
  bool hello = false;
  bool cpu_ref = false;
  bool bench = false;
  std::string bench_name;
  std::filesystem::path shader_hint;
  std::filesystem::path out_csv;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--smoke") {
      smoke_only = true;
    } else if (arg == "--hello-compute") {
      hello = true;
    } else if (arg == "--cpu-ref") {
      cpu_ref = true;
    } else if (arg == "--bench") {
      bench = true;
      if (i + 1 < argc) {
        bench_name = argv[++i];
      }
    } else if (arg == "--out" && i + 1 < argc) {
      out_csv = argv[++i];
    } else if (arg == "--shader" && i + 1 < argc) {
      shader_hint = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      PrintUsage();
      return 0;
    } else {
      std::cerr << "unknown argument: " << arg << "\n";
      return 1;
    }
  }

  if (bench && bench_name != "matmul") {
    std::cerr << "--bench requires name 'matmul' (got '" << bench_name << "')\n";
    return 1;
  }

  PrintBanner();
  if (smoke_only) {
    std::cout << "STATUS: smoke (no D3D12 device create)\n";
    return 0;
  }

  if (cpu_ref && !bench) {
    const CpuRefReport cpu = RunCpuReferenceTests();
    std::cout << cpu.line << "\n";
    if (!cpu.detail.empty()) {
      std::cout << cpu.detail << "\n";
    }
    return cpu.ok ? 0 : 1;
  }

  if (bench) {
    MatmulBenchOptions opt;
    opt.out_csv = out_csv;
    opt.shader_hint = shader_hint;
    const RunReport report = RunMatmulBench(opt);
    PrintReport(report);
    return report.status == RunStatus::Failed ? 1 : 0;
  }

  (void)hello;
  const RunReport report = RunHelloCompute(shader_hint);
  PrintReport(report);
  return report.status == RunStatus::Failed ? 1 : 0;
}
