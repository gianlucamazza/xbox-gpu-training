// xbox-gpu-training host.
// English comments only. Default path: DirectX 12 device + hello compute dispatch.
// --bench matmul writes a CSV vs the CPU reference. --cpu-ref is CPU-only.
// --forward-fixture runs Fase 2 FLP2 decode + tiny forward vs the CPU fixture.
// --grad-check / --train-step run Fase 3 STE + host AdamW.
// --stream-stress runs Fase 4 chunk stream + double buffer under --budget-mb.
// --qat-smoke runs Fase 5 QAT + WSD + isolated cooldowns (host).
// --smoke keeps the no-GPU compile check. No CUDA. DirectML is not the trainer.

#include "cpu_matmul.h"
#include "flp2_dispatch.h"
#include "hello_dispatch.h"
#include "matmul_dispatch.h"
#include "qat_schedule.h"
#include "ste_dispatch.h"
#include "stream_stress.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

static void PrintBanner() {
  std::cout << "xbox-gpu-training host (Fase 5)\n";
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
  std::cout << "  xbox_gpu_host --stream-stress [--budget-mb 1024]\n";
  std::cout << "  xbox_gpu_host --qat-smoke [--steps N] [--config path] [--bit-width ternary|2|4]\n";
  std::cout << "                    [--dry-run]\n";
  std::cout << "  --smoke            Print the banner and exit 0 (no device create).\n";
  std::cout << "  --hello-compute    Create a D3D12 device and dispatch hello_compute once.\n";
  std::cout << "  --cpu-ref          Run the portable CPU GEMM tests (no GPU).\n";
  std::cout << "  --bench matmul     CPU baseline + GPU matmul when a D3D12 device exists.\n";
  std::cout << "                     Writes CSV (schema + status). No invented tok/s.\n";
  std::cout << "  --forward-fixture  Fase 2 FLP2 decode + RMSNorm + RoPE + tiny forward.\n";
  std::cout << "                     Default path: benchmarks/fixtures/tiny_flp2.json\n";
  std::cout << "  --grad-check       Fase 3 STE-identity finite-diff vs analytic (tiny net).\n";
  std::cout << "  --train-step [N]   Fase 3 AdamW/STE on master fp32. Acceptance is N=1.\n";
  std::cout << "  --stream-stress    Fase 4 chunk stream + double buffer. App ~1 GB planning.\n";
  std::cout << "  --budget-mb N      Planning budget in MiB (default 1024). App, not Game.\n";
  std::cout << "  --chunk-mb N       Override fixture tile size (optional).\n";
  std::cout << "  --logical-mb N     Override fixture logical corpus (optional).\n";
  std::cout << "  --qat-smoke        Fase 5 QAT + WSD + isolated cooldown host loop.\n";
  std::cout << "  --steps N          Override schedule smoke.steps (default 16).\n";
  std::cout << "  --config path      QAT/WSD JSON (default examples/qat-wsd-smoke.json).\n";
  std::cout << "  --bit-width W      ternary (default) | 2 | 4. 2/4-bit are host FakeQuant.\n";
  std::cout << "  --dry-run          Validate schedule + print LR table; no train step.\n";
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
  bool stream_stress = false;
  bool qat_smoke = false;
  bool dry_run = false;
  bool bit_width_set = false;
  bool steps_set = false;
  QatBitWidth bit_width = QatBitWidth::Ternary;
  std::uint32_t train_steps = 1;
  std::uint64_t budget_mb = 0;
  std::uint64_t chunk_mb = 0;
  std::uint64_t logical_mb = 0;
  bool budget_mb_set = false;
  std::string bench_name;
  std::filesystem::path shader_hint;
  std::filesystem::path out_csv;
  std::filesystem::path fixture_path;
  std::filesystem::path qat_config;

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
    } else if (arg == "--qat-smoke") {
      qat_smoke = true;
    } else if (arg == "--dry-run") {
      dry_run = true;
    } else if (arg == "--steps" && i + 1 < argc) {
      try {
        const int n = std::stoi(argv[++i]);
        if (n < 1) {
          std::cerr << "--steps requires N >= 1\n";
          return 1;
        }
        train_steps = static_cast<std::uint32_t>(n);
        steps_set = true;
      } catch (const std::exception&) {
        std::cerr << "--steps requires an integer N\n";
        return 1;
      }
    } else if (arg == "--config" && i + 1 < argc) {
      qat_config = argv[++i];
    } else if (arg == "--bit-width" && i + 1 < argc) {
      std::string bw_err;
      if (!ParseQatBitWidth(argv[++i], bit_width, bw_err)) {
        std::cerr << bw_err << "\n";
        return 1;
      }
      bit_width_set = true;
    } else if (arg == "--stream-stress") {
      stream_stress = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        fixture_path = argv[++i];
      }
    } else if (arg == "--budget-mb" && i + 1 < argc) {
      try {
        const unsigned long n = std::stoul(argv[++i]);
        if (n < 1 || n > 65536ul) {
          std::cerr << "--budget-mb must be in 1..65536\n";
          return 1;
        }
        budget_mb = static_cast<std::uint64_t>(n);
        budget_mb_set = true;
      } catch (const std::exception&) {
        std::cerr << "--budget-mb requires an integer MiB value\n";
        return 1;
      }
    } else if (arg == "--chunk-mb" && i + 1 < argc) {
      try {
        const unsigned long n = std::stoul(argv[++i]);
        if (n < 1 || n > 512ul) {
          std::cerr << "--chunk-mb must be in 1..512\n";
          return 1;
        }
        chunk_mb = static_cast<std::uint64_t>(n);
      } catch (const std::exception&) {
        std::cerr << "--chunk-mb requires an integer MiB value\n";
        return 1;
      }
    } else if (arg == "--logical-mb" && i + 1 < argc) {
      try {
        const unsigned long n = std::stoul(argv[++i]);
        if (n < 1 || n > 65536ul) {
          std::cerr << "--logical-mb must be in 1..65536\n";
          return 1;
        }
        logical_mb = static_cast<std::uint64_t>(n);
      } catch (const std::exception&) {
        std::cerr << "--logical-mb requires an integer MiB value\n";
        return 1;
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
  if (budget_mb_set && !stream_stress) {
    std::cerr << "--budget-mb requires --stream-stress\n";
    return 1;
  }
  if ((chunk_mb > 0 || logical_mb > 0) && !stream_stress) {
    std::cerr << "--chunk-mb / --logical-mb require --stream-stress\n";
    return 1;
  }
  if ((dry_run || bit_width_set || !qat_config.empty() || (steps_set && !train_step)) &&
      !qat_smoke) {
    std::cerr << "--qat-smoke required for --dry-run / --config / --bit-width / --steps\n";
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

  if (stream_stress) {
    StreamStressOptions opt;
    opt.fixture = fixture_path;
    opt.budget_mb = budget_mb_set ? budget_mb : kAppPlanningBudgetMiB;
    opt.chunk_mb = chunk_mb;
    opt.logical_mb = logical_mb;
    const RunReport report = RunStreamStress(opt);
    PrintReport(report);
    return report.status == RunStatus::Failed ? 1 : 0;
  }

  if (grad_check) {
    SteHostOptions opt;
    opt.shader_hint = shader_hint;
    const RunReport report = RunSteGradCheckHost(opt);
    PrintReport(report);
    return report.status == RunStatus::Failed ? 1 : 0;
  }

  if (qat_smoke) {
    QatSmokeHostOptions opt;
    opt.shader_hint = shader_hint;
    opt.config = qat_config;
    opt.steps = steps_set ? train_steps : 0;
    opt.dry_run = dry_run;
    opt.bit_width_set = bit_width_set;
    if (bit_width == QatBitWidth::Bits2) {
      opt.bit_width = FakeQuantScheme::Bits2;
    } else if (bit_width == QatBitWidth::Bits4) {
      opt.bit_width = FakeQuantScheme::Bits4;
    } else {
      opt.bit_width = FakeQuantScheme::TernaryAbsmean;
    }
    const RunReport report = RunQatSmokeHost(opt);
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
