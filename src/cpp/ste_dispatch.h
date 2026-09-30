#pragma once

#include "hello_dispatch.h"

#include <cstdint>
#include <filesystem>

struct SteHostOptions {
  std::filesystem::path shader_hint;
  std::uint32_t steps = 1;
};

// CPU grad-check always. On Windows, dispatch FakeQuant / matmul_grad /
// relu2_grad / ste_backward when a D3D12 device exists.
// No device → BLOCKED after a green CPU table. Never invents a dispatch log.
RunReport RunSteGradCheckHost(const SteHostOptions& options);

// One (or N) AdamW/STE step on master fp32. GPU kernels compared when present.
// DirectML is not the optimizer.
RunReport RunSteTrainStepHost(const SteHostOptions& options);
