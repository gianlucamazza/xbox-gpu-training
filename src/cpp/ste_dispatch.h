#pragma once

#include "cpu_ste.h"
#include "hello_dispatch.h"

#include <cstdint>
#include <filesystem>

struct SteHostOptions {
  std::filesystem::path shader_hint;
  std::uint32_t steps = 1;
};

struct QatSmokeHostOptions {
  std::filesystem::path shader_hint;
  std::filesystem::path config;
  std::uint32_t steps = 0;
  bool dry_run = false;
  bool bit_width_set = false;
  FakeQuantScheme bit_width = FakeQuantScheme::TernaryAbsmean;
};

// CPU grad-check always. On Windows, dispatch FakeQuant / matmul_grad /
// relu2_grad / ste_backward when a D3D12 device exists.
// No device → BLOCKED after a green CPU table. Never invents a dispatch log.
RunReport RunSteGradCheckHost(const SteHostOptions& options);

// One (or N) AdamW/STE step on master fp32. GPU kernels compared when present.
// DirectML is not the optimizer.
RunReport RunSteTrainStepHost(const SteHostOptions& options);

// Fase 5 host QAT + WSD smoke. No new HLSL. GPU FakeQuant is not re-dispatched.
RunReport RunQatSmokeHost(const QatSmokeHostOptions& options);
