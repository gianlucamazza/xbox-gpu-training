#pragma once

// CPU reference for Fase 2 FLP2 decode + RMSNorm + RoPE + tiny forward.
// Reconstructs the documented scalar decode contract (conceptual FloppyLM
// reference). Does not unpack a binary FLP2 envelope (magic/header/rANS).
// English comments only. No CUDA. DirectML is not the trainer.

#include "cpu_matmul.h"

#include <cstdint>
#include <string>
#include <vector>

// Chosen TBD tolerances (ADR 0004 forward gate, written in the PR).
constexpr float kFlp2TolAbs = 1.0e-5f;
constexpr float kFlp2TolRel = 1.0e-4f;
constexpr float kFlp2RelFloor = 1.0e-8f;

struct Flp2Config {
  std::uint32_t seq = 0;
  std::uint32_t d = 0;
  std::uint32_t n_heads = 0;
  std::uint32_t d_ff = 0;
  std::uint32_t vocab = 0;
  std::uint32_t levels = 3;
  float eps = 1.0e-6f;
  float rope_theta = 10000.0f;
};

struct Flp2Coded {
  std::uint32_t rows = 0;
  std::uint32_t cols = 0;
  std::uint32_t levels = 3;
  std::vector<std::uint32_t> symbols;
  std::vector<float> scales;
};

struct Flp2Fixture {
  std::string schema;
  std::string name;
  Flp2Config cfg;
  std::vector<std::uint32_t> tokens;
  Flp2Coded emb;
  Flp2Coded qkv;
  Flp2Coded proj;
  Flp2Coded fc;
  Flp2Coded fc2;
  std::vector<float> norm1;
  std::vector<float> norm2;
  std::vector<float> norm;
  std::vector<float> expected_logits;
};

inline std::uint32_t Flp2HeadDim(const Flp2Config& c) { return c.d / c.n_heads; }

bool Flp2Decode(const Flp2Coded& coded, std::vector<float>& weight, std::string& err);

void Flp2RmsNorm(std::uint32_t count, std::uint32_t dim, float eps, const float* x,
                 const float* gamma, float* y);

void Flp2Rope(std::uint32_t seq, std::uint32_t heads, std::uint32_t head_dim, float theta,
              const float* x, float* y);

bool Flp2Forward(const Flp2Fixture& fx, std::vector<float>& logits, std::string& err);

struct Flp2CpuReport {
  bool ok = false;
  std::string line;
  std::string detail;
  float max_abs = 0.0f;
  float max_rel = 0.0f;
};

Flp2CpuReport RunFlp2CpuReference(const Flp2Fixture& fx);
