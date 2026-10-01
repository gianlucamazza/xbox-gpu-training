#pragma once
// E0 semantic constants. floppylm owns their values (floppylm.e0.constants.v1,
// vendored in contracts/floppylm/schemas/values/); the dashboard ctest checks
// that every value here equals the pinned contract. Changing one is a
// floppylm change first, and the bit-identity rule applies.
#include <cstdint>

namespace e0::constants {
// Codec.
inline constexpr float kFp16Min = 6.103515625e-5f;
inline constexpr float kFp16Max = 65504.0f;
inline constexpr int kLogStepsPerOctave = 16;
inline constexpr float kMult2bit = 1.135f;
inline constexpr float kMult4bit = 0.42f;
// Model. The shader repeats kRopeTheta as a literal (e0_tensor.hlsl); the
// ctest checks that literal too.
inline constexpr float kRopeTheta = 10000.0f;
inline constexpr float kRmsNormEps = 1.1920928955078125e-7f;
// Optimizer. Bias corrections use the double betas; moment updates use their
// float forms, whose complements must equal the historical literals.
inline constexpr double kAdamBeta1 = 0.9;
inline constexpr double kAdamBeta2 = 0.95;
inline constexpr float kAdamBeta1f = float(kAdamBeta1);
inline constexpr float kAdamBeta2f = float(kAdamBeta2);
inline constexpr float kAdamOneMinusBeta1 = float(1 - kAdamBeta1);
inline constexpr float kAdamOneMinusBeta2 = float(1 - kAdamBeta2);
static_assert(kAdamOneMinusBeta1 == 0.1f && kAdamOneMinusBeta2 == 0.05f);
inline constexpr float kAdamEps = 1e-8f;
inline constexpr float kGradClip = 1.0f;
inline constexpr float kGradClipEps = 1e-6f;
// WSD schedule.
inline constexpr unsigned kBranches = 3;
inline constexpr double kWarmupFrac = 0.02;
inline constexpr double kCooldownFrac = 0.1;
} // namespace e0::constants
