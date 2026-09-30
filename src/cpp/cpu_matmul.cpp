#include "cpu_matmul.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>

void CpuMatmulFP32(MatmulShape shape, const float* a, const float* b, float* c) {
  for (std::uint32_t i = 0; i < shape.M; ++i) {
    for (std::uint32_t j = 0; j < shape.N; ++j) {
      float acc = 0.0f;
      for (std::uint32_t k = 0; k < shape.K; ++k) {
        acc += a[static_cast<std::size_t>(i) * shape.K + k] *
               b[static_cast<std::size_t>(k) * shape.N + j];
      }
      c[static_cast<std::size_t>(i) * shape.N + j] = acc;
    }
  }
}

void CpuMatmulFP16(MatmulShape shape, const std::uint16_t* a, const std::uint16_t* b,
                   std::uint16_t* c) {
  for (std::uint32_t i = 0; i < shape.M; ++i) {
    for (std::uint32_t j = 0; j < shape.N; ++j) {
      float acc = 0.0f;
      for (std::uint32_t k = 0; k < shape.K; ++k) {
        const float av = HalfToFloat(a[static_cast<std::size_t>(i) * shape.K + k]);
        const float bv = HalfToFloat(b[static_cast<std::size_t>(k) * shape.N + j]);
        acc += av * bv;
      }
      c[static_cast<std::size_t>(i) * shape.N + j] = FloatToHalf(acc);
    }
  }
}

float HalfToFloat(std::uint16_t bits) {
  const std::uint32_t sign = (static_cast<std::uint32_t>(bits & 0x8000u) << 16);
  const std::uint32_t exp = (bits >> 10) & 0x1fu;
  const std::uint32_t man = bits & 0x3ffu;
  std::uint32_t out = 0;
  if (exp == 0) {
    if (man == 0) {
      out = sign;
    } else {
      float den = std::ldexp(static_cast<float>(man) / 1024.0f, -14);
      std::uint32_t den_bits = 0;
      std::memcpy(&den_bits, &den, sizeof(den_bits));
      out = (den_bits & 0x7fffffffu) | sign;
    }
  } else if (exp == 31) {
    out = sign | 0x7f800000u | (man << 13);
  } else {
    out = sign | (static_cast<std::uint32_t>(exp + (127 - 15)) << 23) | (man << 13);
  }
  float value = 0.0f;
  std::memcpy(&value, &out, sizeof(value));
  return value;
}

std::uint16_t FloatToHalf(float value) {
  std::uint32_t f = 0;
  std::memcpy(&f, &value, sizeof(f));
  const std::uint16_t sign = static_cast<std::uint16_t>((f >> 16) & 0x8000u);
  const std::uint32_t abs_bits = f & 0x7fffffffu;

  if (abs_bits > 0x7f800000u) {
    const std::uint16_t nan_payload = static_cast<std::uint16_t>((f >> 13) & 0x3ffu);
    return static_cast<std::uint16_t>(sign | 0x7c00u | nan_payload | 0x200u);
  }
  if (abs_bits >= 0x47800000u) {
    return static_cast<std::uint16_t>(sign | 0x7c00u);
  }
  if (abs_bits >= 0x38800000u) {
    const std::uint32_t rounded = abs_bits + 0x00001000u;
    const std::uint16_t exp = static_cast<std::uint16_t>(((rounded >> 23) - 112) << 10);
    const std::uint16_t man = static_cast<std::uint16_t>((rounded >> 13) & 0x3ffu);
    return static_cast<std::uint16_t>(sign | exp | man);
  }
  if (abs_bits >= 0x33000000u) {
    const std::int32_t exp = static_cast<std::int32_t>((abs_bits >> 23) & 0xffu) - 127;
    std::uint32_t man = (abs_bits & 0x7fffffu) | 0x800000u;
    const std::uint32_t shift = static_cast<std::uint32_t>(14 - exp);
    const std::uint32_t half = (man + (1u << (shift - 1))) >> shift;
    return static_cast<std::uint16_t>(sign | half);
  }
  return sign;
}

MatmulError CompareFP32(std::size_t count, const float* actual, const float* reference,
                        float rel_floor) {
  MatmulError err;
  for (std::size_t i = 0; i < count; ++i) {
    const float a = actual[i];
    const float r = reference[i];
    const float abs_err = std::fabs(a - r);
    const float denom = std::max(std::fabs(r), rel_floor);
    const float rel_err = abs_err / denom;
    if (abs_err > err.max_abs) {
      err.max_abs = abs_err;
    }
    if (rel_err > err.max_rel) {
      err.max_rel = rel_err;
    }
  }
  return err;
}

MatmulError CompareFP16(std::size_t count, const std::uint16_t* actual,
                        const std::uint16_t* reference, float rel_floor) {
  std::vector<float> actual_f(count);
  std::vector<float> reference_f(count);
  for (std::size_t i = 0; i < count; ++i) {
    actual_f[i] = HalfToFloat(actual[i]);
    reference_f[i] = HalfToFloat(reference[i]);
  }
  return CompareFP32(count, actual_f.data(), reference_f.data(), rel_floor);
}

bool WithinTolerance(const MatmulError& err, float tol_abs, float tol_rel) {
  return err.max_abs <= tol_abs && err.max_rel <= tol_rel;
}

namespace {

void FillIota(std::vector<float>& v, float start, float step) {
  for (std::size_t i = 0; i < v.size(); ++i) {
    v[i] = start + step * static_cast<float>(i);
  }
}

void ToHalf(const std::vector<float>& in, std::vector<std::uint16_t>& out) {
  out.resize(in.size());
  for (std::size_t i = 0; i < in.size(); ++i) {
    out[i] = FloatToHalf(in[i]);
  }
}

bool CheckHalfRoundtrip(std::string& detail) {
  const float samples[] = {0.0f, 1.0f, -1.0f, 0.5f, 2.0f, 0.25f, -0.5f, 8.0f};
  float worst = 0.0f;
  for (float s : samples) {
    const float back = HalfToFloat(FloatToHalf(s));
    worst = std::max(worst, std::fabs(back - s));
    if (std::fabs(back - s) > 1.0e-3f) {
      std::ostringstream os;
      os << "half roundtrip failed for " << s << " -> " << back;
      detail = os.str();
      return false;
    }
  }
  (void)worst;
  return true;
}

}  // namespace

CpuRefReport RunCpuReferenceTests() {
  CpuRefReport report;
  std::ostringstream detail;
  detail << std::scientific << std::setprecision(4);
  detail << "CPU reference interface (portable GEMM; ggml not vendored)\n";
  detail << "tolerance FP32 max-abs " << kMatmulTolAbsFP32 << " max-rel " << kMatmulTolRelFP32
         << "\n";
  detail << "tolerance FP16 max-abs " << kMatmulTolAbsFP16 << " max-rel " << kMatmulTolRelFP16
         << "\n";

  std::string half_err;
  if (!CheckHalfRoundtrip(half_err)) {
    report.ok = false;
    report.line = "FAILED: cpu-ref half roundtrip";
    report.detail = half_err;
    return report;
  }

  // Hand-computed 2x3 * 3x2.
  {
    const MatmulShape shape{2, 2, 3};
    const float a[] = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f};
    const float b[] = {7.f, 8.f, 9.f, 10.f, 11.f, 12.f};
    const float expect[] = {58.f, 64.f, 139.f, 154.f};
    float c[4] = {};
    CpuMatmulFP32(shape, a, b, c);
    const MatmulError err = CompareFP32(4, c, expect);
    report.max_abs_fp32 = std::max(report.max_abs_fp32, err.max_abs);
    report.max_rel_fp32 = std::max(report.max_rel_fp32, err.max_rel);
    if (!WithinTolerance(err, kMatmulTolAbsFP32, kMatmulTolRelFP32)) {
      report.ok = false;
      report.line = "FAILED: cpu-ref hand 2x3x2";
      detail << "hand 2x3*3x2 max-abs " << err.max_abs << " max-rel " << err.max_rel << "\n";
      report.detail = detail.str();
      return report;
    }
    detail << "hand 2x3*3x2: max-abs " << err.max_abs << " max-rel " << err.max_rel << "\n";
  }

  // Identity * values.
  {
    const MatmulShape shape{4, 4, 4};
    std::vector<float> a(16, 0.0f);
    std::vector<float> b(16, 0.0f);
    std::vector<float> c(16, 0.0f);
    for (int i = 0; i < 4; ++i) {
      a[static_cast<std::size_t>(i * 4 + i)] = 1.0f;
      for (int j = 0; j < 4; ++j) {
        b[static_cast<std::size_t>(i * 4 + j)] = static_cast<float>(i * 4 + j + 1);
      }
    }
    CpuMatmulFP32(shape, a.data(), b.data(), c.data());
    const MatmulError err = CompareFP32(16, c.data(), b.data());
    report.max_abs_fp32 = std::max(report.max_abs_fp32, err.max_abs);
    report.max_rel_fp32 = std::max(report.max_rel_fp32, err.max_rel);
    if (!WithinTolerance(err, kMatmulTolAbsFP32, kMatmulTolRelFP32)) {
      report.ok = false;
      report.line = "FAILED: cpu-ref identity";
      report.detail = detail.str();
      return report;
    }
    detail << "identity 4x4: max-abs " << err.max_abs << " max-rel " << err.max_rel << "\n";
  }

  // All-ones: C[i,j] = K.
  {
    const MatmulShape shape{8, 8, 8};
    std::vector<float> a(64, 1.0f);
    std::vector<float> b(64, 1.0f);
    std::vector<float> c(64, 0.0f);
    std::vector<float> expect(64, 8.0f);
    CpuMatmulFP32(shape, a.data(), b.data(), c.data());
    const MatmulError err = CompareFP32(64, c.data(), expect.data());
    report.max_abs_fp32 = std::max(report.max_abs_fp32, err.max_abs);
    report.max_rel_fp32 = std::max(report.max_rel_fp32, err.max_rel);
    if (!WithinTolerance(err, kMatmulTolAbsFP32, kMatmulTolRelFP32)) {
      report.ok = false;
      report.line = "FAILED: cpu-ref ones";
      report.detail = detail.str();
      return report;
    }
    detail << "ones 8x8x8: max-abs " << err.max_abs << " max-rel " << err.max_rel << "\n";
  }

  // Repeatability / non-multiple-of-8 tile.
  {
    const MatmulShape shape{7, 5, 9};
    std::vector<float> a(MatmulACount(shape));
    std::vector<float> b(MatmulBCount(shape));
    std::vector<float> c1(MatmulCCount(shape));
    std::vector<float> c2(MatmulCCount(shape));
    FillIota(a, -1.0f, 0.02f);
    FillIota(b, 0.5f, -0.01f);
    CpuMatmulFP32(shape, a.data(), b.data(), c1.data());
    CpuMatmulFP32(shape, a.data(), b.data(), c2.data());
    const MatmulError err = CompareFP32(c1.size(), c1.data(), c2.data());
    report.max_abs_fp32 = std::max(report.max_abs_fp32, err.max_abs);
    report.max_rel_fp32 = std::max(report.max_rel_fp32, err.max_rel);
    if (!WithinTolerance(err, kMatmulTolAbsFP32, kMatmulTolRelFP32)) {
      report.ok = false;
      report.line = "FAILED: cpu-ref 7x5x9 repeat";
      report.detail = detail.str();
      return report;
    }
    detail << "repeat 7x5x9: max-abs " << err.max_abs << " max-rel " << err.max_rel << "\n";
  }

  // FP16 ones: C = K, exactly representable.
  {
    const MatmulShape shape{4, 4, 4};
    std::vector<std::uint16_t> a(16, FloatToHalf(1.0f));
    std::vector<std::uint16_t> b(16, FloatToHalf(1.0f));
    std::vector<std::uint16_t> c(16, 0);
    std::vector<std::uint16_t> expect(16, FloatToHalf(4.0f));
    CpuMatmulFP16(shape, a.data(), b.data(), c.data());
    const MatmulError err = CompareFP16(16, c.data(), expect.data());
    report.max_abs_fp16 = std::max(report.max_abs_fp16, err.max_abs);
    report.max_rel_fp16 = std::max(report.max_rel_fp16, err.max_rel);
    if (!WithinTolerance(err, kMatmulTolAbsFP16, kMatmulTolRelFP16)) {
      report.ok = false;
      report.line = "FAILED: cpu-ref fp16 ones";
      report.detail = detail.str();
      return report;
    }
    detail << "fp16 ones 4x4x4: max-abs " << err.max_abs << " max-rel " << err.max_rel << "\n";
  }

  // FP16 vs FP32 reference on a small random-ish tile.
  {
    const MatmulShape shape{8, 8, 8};
    std::vector<float> af(64);
    std::vector<float> bf(64);
    FillIota(af, -0.5f, 0.015f);
    FillIota(bf, 0.25f, -0.01f);
    std::vector<std::uint16_t> a;
    std::vector<std::uint16_t> b;
    ToHalf(af, a);
    ToHalf(bf, b);
    for (std::size_t i = 0; i < af.size(); ++i) {
      af[i] = HalfToFloat(a[i]);
      bf[i] = HalfToFloat(b[i]);
    }
    std::vector<float> cref(64);
    CpuMatmulFP32(shape, af.data(), bf.data(), cref.data());
    std::vector<std::uint16_t> c16(64);
    CpuMatmulFP16(shape, a.data(), b.data(), c16.data());
    std::vector<float> c16f(64);
    for (std::size_t i = 0; i < 64; ++i) {
      c16f[i] = HalfToFloat(c16[i]);
    }
    const MatmulError err = CompareFP32(64, c16f.data(), cref.data(), 1.0e-4f);
    report.max_abs_fp16 = std::max(report.max_abs_fp16, err.max_abs);
    report.max_rel_fp16 = std::max(report.max_rel_fp16, err.max_rel);
    if (!WithinTolerance(err, kMatmulTolAbsFP16, kMatmulTolRelFP16)) {
      report.ok = false;
      report.line = "FAILED: cpu-ref fp16 vs fp32";
      detail << "fp16 vs fp32 8x8x8: max-abs " << err.max_abs << " max-rel " << err.max_rel << "\n";
      report.detail = detail.str();
      return report;
    }
    detail << "fp16 vs fp32 8x8x8: max-abs " << err.max_abs << " max-rel " << err.max_rel << "\n";
  }

  report.ok = true;
  report.line = "STATUS: cpu-ref ok";
  detail << "worst FP32 max-abs " << report.max_abs_fp32 << " max-rel " << report.max_rel_fp32
         << "\n";
  detail << "worst FP16 max-abs " << report.max_abs_fp16 << " max-rel " << report.max_rel_fp16
         << "\n";
  detail << "ggml: not vendored (portable CPU GEMM; see docs/ggml-baseline.md)\n";
  detail << "not a tok/s result; not a GPU result; not a console result";
  report.detail = detail.str();
  return report;
}
