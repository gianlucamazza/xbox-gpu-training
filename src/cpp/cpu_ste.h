#pragma once

// Fase 3: ternary FakeQuant (absmean) + STE + host AdamW + tiny-net grad-check.
// Semantics: ADR 0002 / docs/ste-adamw.md. No CUDA. DirectML is not the optimizer.
// English comments only.

#include "cpu_matmul.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Grad-check (STE-identity vs central finite-diff). Chosen TBD, written in the PR.
// Pass: max-abs <= 1e-3, and max-rel <= 2e-2 when |analytic| >= 1e-2.
// Smaller analytic grads are abs-gated (central-diff noise dominates rel).
constexpr float kSteGradTolAbs = 1.0e-3f;
constexpr float kSteGradTolRel = 2.0e-2f;
constexpr float kSteGradRelSkipAbs = 1.0e-2f;
constexpr float kSteGradRelFloor = 1.0e-8f;
constexpr float kSteFiniteDiffH = 1.0e-4f;

// GPU kernel vs CPU (same gate as Fase 2 FLP2 when a D3D12 device exists).
constexpr float kSteGpuTolAbs = 1.0e-5f;
constexpr float kSteGpuTolRel = 1.0e-4f;

struct AdamWConfig {
  float lr = 1.0e-3f;
  float beta1 = 0.9f;
  float beta2 = 0.999f;
  float eps = 1.0e-8f;
  float weight_decay = 0.01f;
};

struct AdamWState {
  std::vector<float> m;
  std::vector<float> v;
  std::uint32_t t = 0;
};

void AdamWReset(AdamWState& st, std::size_t n);
void AdamWStep(const AdamWConfig& cfg, AdamWState& st, float* w, const float* g, std::size_t n);

enum class FakeQuantScheme {
  TernaryAbsmean = 0,
  Bits2Stub = 2,
  Bits4Stub = 4,
};

float AbsMean(const float* w, std::size_t n);

// Nearest integer, ties to even — matches HLSL round().
float RoundNearestEven(float x);

bool FakeQuantTernaryAbsmean(const float* w, std::size_t n, float* wq, float* mask, float& scale,
                             std::string& err);

// Fase 5 stubs — not on the Fase 3 train path.
bool FakeQuant2BitStub(const float* w, std::size_t n, float* wq, std::string& err);
bool FakeQuant4BitStub(const float* w, std::size_t n, float* wq, std::string& err);

bool FakeQuant(FakeQuantScheme scheme, const float* w, std::size_t n, float* wq, float* mask,
               float& scale, std::string& err);

void SteApply(const float* dwq, const float* mask, float* dw, std::size_t n);

void Relu2(const float* pre, float* y, std::size_t n);
void Relu2Grad(const float* pre, const float* dy, float* dpre, std::size_t n);

// y[b, o] = sum_i x[b, i] * w[o, i]   (W is [out, in], row-major)
void LinearForward(std::uint32_t batch, std::uint32_t in_f, std::uint32_t out_f, const float* x,
                   const float* w, float* y);

// dW[o, i] = sum_b dy[b, o] * x[b, i]
void LinearWeightGrad(std::uint32_t batch, std::uint32_t in_f, std::uint32_t out_f, const float* dy,
                      const float* x, float* dw);

// dx[b, i] = sum_o dy[b, o] * w[o, i]
void LinearInputGrad(std::uint32_t batch, std::uint32_t in_f, std::uint32_t out_f, const float* dy,
                     const float* w, float* dx);

struct TinySteNet {
  std::uint32_t B = 2;
  std::uint32_t In = 4;
  std::uint32_t H = 4;
  std::uint32_t Out = 3;
  std::vector<float> x;
  std::vector<float> W1;
  std::vector<float> W2;
  std::vector<float> t;
};

TinySteNet MakeDefaultTinySteNet();

struct TinySteTensors {
  std::vector<float> W1q;
  std::vector<float> W2q;
  std::vector<float> mask1;
  std::vector<float> mask2;
  std::vector<float> h_pre;
  std::vector<float> h;
  std::vector<float> y;
  std::vector<float> dW1;
  std::vector<float> dW2;
  float scale1 = 0.0f;
  float scale2 = 0.0f;
  float loss = 0.0f;
};

// identity_ste: Wq := W (the function STE claims to differentiate).
// Otherwise: ternary FakeQuant + STE clip on the master grads.
bool TinySteForwardBackward(const TinySteNet& net, bool identity_ste, TinySteTensors& io,
                            std::string& err);

float TinySteLossAt(const TinySteNet& net, bool identity_ste, const float* W1, const float* W2,
                    std::string& err);

struct GradCheckRow {
  std::string name;
  float analytic = 0.0f;
  float finite = 0.0f;
  float max_abs = 0.0f;
  float max_rel = 0.0f;
  bool pass = false;
};

struct SteCpuReport {
  bool ok = false;
  std::string line;
  std::string detail;
  float max_abs = 0.0f;
  float max_rel = 0.0f;
  float loss_before = 0.0f;
  float loss_after = 0.0f;
};

SteCpuReport RunSteGradCheck();
SteCpuReport RunSteTrainStep(std::uint32_t steps);
