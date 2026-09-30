#pragma once

#include "hello_dispatch.h"

#include <filesystem>

struct Flp2ForwardOptions {
  std::filesystem::path fixture;
  std::filesystem::path shader_hint;
};

// Load the tiny FLP2 fixture, run the CPU reference, and (on Windows) dispatch
// decode / RMSNorm / RoPE / forward when a D3D12 device exists.
// No device → BLOCKED, CPU tests still green. Never invents a dispatch log.
RunReport RunFlp2ForwardFixture(const Flp2ForwardOptions& options);
