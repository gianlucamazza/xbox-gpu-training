// xbox-gpu-training host.
// English comments only. Default path: DirectX 12 device + hello compute dispatch.
// --bench matmul writes a CSV vs the CPU reference. --cpu-ref is CPU-only.
// --forward-fixture runs Fase 2 FLP2 decode + tiny forward vs the CPU fixture.
// --grad-check / --train-step run Fase 3 STE + host AdamW.
// --smoke keeps the no-GPU compile check. No CUDA. DirectML is not the trainer.

#include "cpu_matmul.h"
#include "flp2_dispatch.h"
#include "hello_dispatch.h"
#include "matmul_dispatch.h"
#include "ste_dispatch.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

static void PrintBanner() {
  std::cout << "xbox-gpu-training host (Fase 3)\n";
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
  std::cout << "  xbox_gpu_host --forward-fixture [path]\n";
  std::cout << "  xbox_gpu_host --grad-check\n";
  std::cout << "  xbox_gpu_host --train-step [N]\n";
  std::cout << "  --smoke            Print the banner and exit 0 (no device create).\n";
  std::cout << "  --hello-compute    Create a D3D12 device and dispatch hello_compute once.\n";
  std::cout << "  --cpu-ref          Run the portable CPU GEMM tests (no GPU).\n";
  std::cout << "  --bench matmul     CPU baseline + GPU matmul when a D3D12 device exists.\n";
  std::cout << "                     Writes CSV (schema + status). No invented tok/s.\n";
  std::cout << "  --forward-fixture  Fase 2 FLP2 decode + RMSNorm + RoPE + tiny forward.\n";
  std::cout << "                     Default path: benchmarks/fixtures/tiny_flp2.json\n";
  std::cout << "  --grad-check       Fase 3 STE-identity finite-diff vs analytic (tiny net).\n";
  std::cout << "  --train-step [N]   Fase 3 AdamW/STE on master fp32. Acceptance is N=1.\n";
  std::cout << "  default            Same as --hello-compute.\n";
}

int main(int argc, char** argv) {
  bool smoke_only = false;
  bool hello = false;
  bool cpu_ref = false;
  bool bench = false;
  bool forward_fixture = false;
  bool grad_check = false;
  bool train_step = false;
  std::uint32_t train_steps = 1;
  std::string bench_name;
  std::filesystem::path shader_hint;
  std::filesystem::path out_csv;
  std::filesystem::path fixture_path;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--smoke") {
      smoke_only = true;
    } else if (arg == "--hello-compute") {
      hello = true;
    } else if (arg == "--cpu-ref") {
      cpu_ref = true;
    } else if (arg == "--forward-fixture") {
      forward_fixture = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        fixture_path = argv[++i];
      }
    } else if (arg == "--grad-check") {
      grad_check = true;
    } else if (arg == "--train-step") {
      train_step = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        try {
          const int n = std::stoi(argv[++i]);
          if (n < 1) {
            std::cerr << "--train-step requires N >= 1\n";
            return 1;
          }
          train_steps = static_cast<std::uint32_t>(n);
        } catch (const std::exception&) {
          std::cerr << "--train-step requires an integer N\n";
          return 1;
        }
      }
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

  if (grad_check) {
    SteHostOptions opt;
    opt.shader_hint = shader_hint;
    const RunReport report = RunSteGradCheckHost(opt);
    PrintReport(report);
    return report.status == RunStatus::Failed ? 1 : 0;
  }

  if (train_step) {
    SteHostOptions opt;
    opt.shader_hint = shader_hint;
    opt.steps = train_steps;
    const RunReport report = RunSteTrainStepHost(opt);
    PrintReport(report);
    return report.status == RunStatus::Failed ? 1 : 0;
  }

  if (forward_fixture) {
    Flp2ForwardOptions opt;
    opt.fixture = fixture_path;
    opt.shader_hint = shader_hint;
    const RunReport report = RunFlp2ForwardFixture(opt);
    PrintReport(report);
    return report.status == RunStatus::Failed ? 1 : 0;
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
