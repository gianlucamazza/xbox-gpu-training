#pragma once

// Portable CPU GEMM used as the Fase 1 numerical baseline.
// ggml is not vendored in this tree — see docs/ggml-baseline.md.
// English comments only. No CUDA. DirectML is not the trainer.

#include <cstddef>
#include <cstdint>
#include <string>

struct MatmulShape {
  std::uint32_t M = 0;  // rows of A and C
  std::uint32_t N = 0;  // cols of B and C
  std::uint32_t K = 0;  // cols of A / rows of B
};

inline std::size_t MatmulACount(MatmulShape s) {
  return static_cast<std::size_t>(s.M) * static_cast<std::size_t>(s.K);
}

inline std::size_t MatmulBCount(MatmulShape s) {
  return static_cast<std::size_t>(s.K) * static_cast<std::size_t>(s.N);
}

inline std::size_t MatmulCCount(MatmulShape s) {
  return static_cast<std::size_t>(s.M) * static_cast<std::size_t>(s.N);
}

// Row-major C[M, N] = A[M, K] * B[K, N].
// Same contraction the HLSL kernels implement. ggml_mul_mat can replace
// this body later without changing the host/CSV contract.
void CpuMatmulFP32(MatmulShape shape, const float* a, const float* b, float* c);

// IEEE binary16 bit patterns in the low 16 bits of each uint16_t.
// Products accumulate in FP32, then convert back — matches CSMainFP16.
void CpuMatmulFP16(MatmulShape shape, const std::uint16_t* a, const std::uint16_t* b,
                   std::uint16_t* c);

std::uint16_t FloatToHalf(float value);
float HalfToFloat(std::uint16_t bits);

struct MatmulError {
  float max_abs = 0.0f;
  float max_rel = 0.0f;
};

MatmulError CompareFP32(std::size_t count, const float* actual, const float* reference,
                        float rel_floor = 1.0e-8f);
MatmulError CompareFP16(std::size_t count, const std::uint16_t* actual,
                        const std::uint16_t* reference, float rel_floor = 1.0e-4f);

// Fase 1 chosen TBD tolerances (also written in the PR body).
// FP32: max-abs <= 1e-4 AND max-rel <= 1e-3
// FP16: max-abs <= 5e-2 AND max-rel <= 5e-2
constexpr float kMatmulTolAbsFP32 = 1.0e-4f;
constexpr float kMatmulTolRelFP32 = 1.0e-3f;
constexpr float kMatmulTolAbsFP16 = 5.0e-2f;
constexpr float kMatmulTolRelFP16 = 5.0e-2f;

bool WithinTolerance(const MatmulError& err, float tol_abs, float tol_rel);

struct CpuRefReport {
  bool ok = false;
  std::string line;
  std::string detail;
  float max_abs_fp32 = 0.0f;
  float max_rel_fp32 = 0.0f;
  float max_abs_fp16 = 0.0f;
  float max_rel_fp16 = 0.0f;
};

CpuRefReport RunCpuReferenceTests();
