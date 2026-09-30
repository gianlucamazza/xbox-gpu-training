#pragma once

#include "hello_dispatch.h"

#include <filesystem>
#include <string>

struct MatmulBenchOptions {
  std::filesystem::path out_csv;
  std::filesystem::path shader_hint;
};

// Run CPU reference tests, optionally dispatch HLSL matmul on a D3D12 device,
// and write a CSV under benchmarks/results/. Never invents tok/s.
// No device → status=blocked, CPU rows still written, exit is not Failed.
RunReport RunMatmulBench(const MatmulBenchOptions& options);
