#include "cpu_ste.h"

#include <algorithm>
#include <cfenv>
#include <cmath>
#include <iomanip>
#include <sstream>

void AdamWReset(AdamWState& st, std::size_t n) {
  st.m.assign(n, 0.0f);
  st.v.assign(n, 0.0f);
  st.t = 0;
}

void AdamWStep(const AdamWConfig& cfg, AdamWState& st, float* w, const float* g, std::size_t n) {
  if (st.m.size() != n || st.v.size() != n) {
    AdamWReset(st, n);
  }
  st.t += 1;
  const float b1t = 1.0f - std::pow(cfg.beta1, static_cast<float>(st.t));
  const float b2t = 1.0f - std::pow(cfg.beta2, static_cast<float>(st.t));
  const float inv_b1 = 1.0f / b1t;
  const float inv_b2 = 1.0f / b2t;
  for (std::size_t i = 0; i < n; ++i) {
    const float gi = g[i];
    st.m[i] = cfg.beta1 * st.m[i] + (1.0f - cfg.beta1) * gi;
    st.v[i] = cfg.beta2 * st.v[i] + (1.0f - cfg.beta2) * gi * gi;
    const float mhat = st.m[i] * inv_b1;
    const float vhat = st.v[i] * inv_b2;
    const float adam = mhat / (std::sqrt(vhat) + cfg.eps);
    w[i] = w[i] - cfg.lr * (adam + cfg.weight_decay * w[i]);
  }
}

float AbsMean(const float* w, std::size_t n) {
  if (n == 0) {
    return 1.0f;
  }
  float acc = 0.0f;
  for (std::size_t i = 0; i < n; ++i) {
    acc += std::fabs(w[i]);
  }
  const float s = acc / static_cast<float>(n);
  return s == 0.0f ? 1.0f : s;
}

float RoundNearestEven(float x) {
  const int prev = std::fegetround();
  std::fesetround(FE_TONEAREST);
  const float r = std::nearbyint(x);
  std::fesetround(prev);
  return r;
}

bool FakeQuantTernaryAbsmean(const float* w, std::size_t n, float* wq, float* mask, float& scale,
                             std::string& err) {
  if (w == nullptr || wq == nullptr || n == 0) {
    err = "FakeQuant ternary: empty weight";
    return false;
  }
  scale = AbsMean(w, n);
  for (std::size_t i = 0; i < n; ++i) {
    const float nrm = w[i] / scale;
    float q = RoundNearestEven(nrm);
    if (q > 1.0f) {
      q = 1.0f;
    } else if (q < -1.0f) {
      q = -1.0f;
    }
    wq[i] = q * scale;
    if (mask != nullptr) {
      mask[i] = (std::fabs(nrm) <= 1.0f) ? 1.0f : 0.0f;
    }
  }
  return true;
}

bool FakeQuant2BitStub(const float* w, std::size_t n, float* wq, std::string& err) {
  (void)w;
  (void)n;
  (void)wq;
  err = "FakeQuant 2-bit is a Fase 5 stub — not used for Fase 3 acceptance";
  return false;
}

bool FakeQuant4BitStub(const float* w, std::size_t n, float* wq, std::string& err) {
  (void)w;
  (void)n;
  (void)wq;
  err = "FakeQuant 4-bit is a Fase 5 stub — not used for Fase 3 acceptance";
  return false;
}

bool FakeQuant(FakeQuantScheme scheme, const float* w, std::size_t n, float* wq, float* mask,
               float& scale, std::string& err) {
  switch (scheme) {
    case FakeQuantScheme::TernaryAbsmean:
      return FakeQuantTernaryAbsmean(w, n, wq, mask, scale, err);
    case FakeQuantScheme::Bits2Stub:
      return FakeQuant2BitStub(w, n, wq, err);
    case FakeQuantScheme::Bits4Stub:
      return FakeQuant4BitStub(w, n, wq, err);
  }
  err = "FakeQuant: unknown scheme";
  return false;
}

void SteApply(const float* dwq, const float* mask, float* dw, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) {
    dw[i] = dwq[i] * mask[i];
  }
}

void Relu2(const float* pre, float* y, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) {
    const float r = pre[i] > 0.0f ? pre[i] : 0.0f;
    y[i] = r * r;
  }
}

void Relu2Grad(const float* pre, const float* dy, float* dpre, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) {
    dpre[i] = (pre[i] > 0.0f ? 2.0f * pre[i] : 0.0f) * dy[i];
  }
}

void LinearForward(std::uint32_t batch, std::uint32_t in_f, std::uint32_t out_f, const float* x,
                   const float* w, float* y) {
  for (std::uint32_t b = 0; b < batch; ++b) {
    for (std::uint32_t o = 0; o < out_f; ++o) {
      float acc = 0.0f;
      for (std::uint32_t i = 0; i < in_f; ++i) {
        acc += x[static_cast<std::size_t>(b) * in_f + i] *
               w[static_cast<std::size_t>(o) * in_f + i];
      }
      y[static_cast<std::size_t>(b) * out_f + o] = acc;
    }
  }
}

void LinearWeightGrad(std::uint32_t batch, std::uint32_t in_f, std::uint32_t out_f, const float* dy,
                      const float* x, float* dw) {
  const std::size_t wn = static_cast<std::size_t>(out_f) * in_f;
  for (std::size_t i = 0; i < wn; ++i) {
    dw[i] = 0.0f;
  }
  for (std::uint32_t o = 0; o < out_f; ++o) {
    for (std::uint32_t i = 0; i < in_f; ++i) {
      float acc = 0.0f;
      for (std::uint32_t b = 0; b < batch; ++b) {
        acc += dy[static_cast<std::size_t>(b) * out_f + o] *
               x[static_cast<std::size_t>(b) * in_f + i];
      }
      dw[static_cast<std::size_t>(o) * in_f + i] = acc;
    }
  }
}

void LinearInputGrad(std::uint32_t batch, std::uint32_t in_f, std::uint32_t out_f, const float* dy,
                     const float* w, float* dx) {
  for (std::uint32_t b = 0; b < batch; ++b) {
    for (std::uint32_t i = 0; i < in_f; ++i) {
      float acc = 0.0f;
      for (std::uint32_t o = 0; o < out_f; ++o) {
        acc += dy[static_cast<std::size_t>(b) * out_f + o] *
               w[static_cast<std::size_t>(o) * in_f + i];
      }
      dx[static_cast<std::size_t>(b) * in_f + i] = acc;
    }
  }
}

TinySteNet MakeDefaultTinySteNet() {
  TinySteNet n;
  n.B = 2;
  n.In = 4;
  n.H = 4;
  n.Out = 3;
  // Values stay off half-integers after /scale so C++ nearbyint and HLSL round agree.
  // One |W/s| > 1 entry (W1[2,0] = 1.35) exercises the STE clip.
  n.x = {1.0f, 0.25f, -0.5f, 0.75f, 0.5f, -1.0f, 0.25f, 0.125f};
  n.W1 = {0.40f, -0.20f, 0.80f, 0.10f, -0.60f, 0.30f, 0.15f, 0.90f,
          1.35f, -0.40f, 0.05f, -0.70f, 0.25f, 0.35f, -0.15f, 0.45f};
  n.W2 = {0.50f, -0.30f, 0.20f, 0.80f, -0.40f, 0.60f, 0.10f, -0.20f, 0.70f, 0.15f, -0.55f, 0.30f};
  n.t = {0.10f, -0.20f, 0.30f, 0.00f, 0.40f, -0.10f};
  return n;
}

bool TinySteForwardBackward(const TinySteNet& net, bool identity_ste, TinySteTensors& io,
                            std::string& err) {
  const std::size_t n1 = static_cast<std::size_t>(net.H) * net.In;
  const std::size_t n2 = static_cast<std::size_t>(net.Out) * net.H;
  const std::size_t nh = static_cast<std::size_t>(net.B) * net.H;
  const std::size_t ny = static_cast<std::size_t>(net.B) * net.Out;
  if (net.x.size() != static_cast<std::size_t>(net.B) * net.In || net.W1.size() != n1 ||
      net.W2.size() != n2 || net.t.size() != ny) {
    err = "tiny STE net: shape mismatch";
    return false;
  }

  io.W1q.resize(n1);
  io.W2q.resize(n2);
  io.mask1.assign(n1, 1.0f);
  io.mask2.assign(n2, 1.0f);
  io.h_pre.resize(nh);
  io.h.resize(nh);
  io.y.resize(ny);
  io.dW1.resize(n1);
  io.dW2.resize(n2);

  if (identity_ste) {
    io.W1q = net.W1;
    io.W2q = net.W2;
    io.scale1 = AbsMean(net.W1.data(), n1);
    io.scale2 = AbsMean(net.W2.data(), n2);
  } else {
    if (!FakeQuantTernaryAbsmean(net.W1.data(), n1, io.W1q.data(), io.mask1.data(), io.scale1,
                                 err) ||
        !FakeQuantTernaryAbsmean(net.W2.data(), n2, io.W2q.data(), io.mask2.data(), io.scale2,
                                 err)) {
      return false;
    }
  }

  LinearForward(net.B, net.In, net.H, net.x.data(), io.W1q.data(), io.h_pre.data());
  Relu2(io.h_pre.data(), io.h.data(), nh);
  LinearForward(net.B, net.H, net.Out, io.h.data(), io.W2q.data(), io.y.data());

  io.loss = 0.0f;
  std::vector<float> dy(ny);
  for (std::size_t i = 0; i < ny; ++i) {
    dy[i] = io.y[i] - net.t[i];
    io.loss += 0.5f * dy[i] * dy[i];
  }

  LinearWeightGrad(net.B, net.H, net.Out, dy.data(), io.h.data(), io.dW2.data());
  std::vector<float> dh(nh);
  LinearInputGrad(net.B, net.H, net.Out, dy.data(), io.W2q.data(), dh.data());
  std::vector<float> dh_pre(nh);
  Relu2Grad(io.h_pre.data(), dh.data(), dh_pre.data(), nh);
  LinearWeightGrad(net.B, net.In, net.H, dh_pre.data(), net.x.data(), io.dW1.data());

  if (!identity_ste) {
    SteApply(io.dW1.data(), io.mask1.data(), io.dW1.data(), n1);
    SteApply(io.dW2.data(), io.mask2.data(), io.dW2.data(), n2);
  }
  return true;
}

float TinySteLossAt(const TinySteNet& net, bool identity_ste, const float* W1, const float* W2,
                    std::string& err) {
  TinySteNet tmp = net;
  tmp.W1.assign(W1, W1 + net.W1.size());
  tmp.W2.assign(W2, W2 + net.W2.size());
  TinySteTensors io;
  if (!TinySteForwardBackward(tmp, identity_ste, io, err)) {
    return 0.0f;
  }
  return io.loss;
}

namespace {

bool TernaryCodesOk(const std::vector<float>& wq, float scale, std::string& err) {
  if (scale <= 0.0f) {
    err = "FakeQuant: non-positive scale";
    return false;
  }
  for (float v : wq) {
    const float q = v / scale;
    if (!(std::fabs(q) < 1.0e-5f || std::fabs(q - 1.0f) < 1.0e-5f ||
          std::fabs(q + 1.0f) < 1.0e-5f)) {
      err = "FakeQuant: code not in {-1,0,+1}";
      return false;
    }
  }
  return true;
}

void AppendRow(std::ostringstream& os, const GradCheckRow& row) {
  os << std::left << std::setw(10) << row.name << std::right << std::scientific
     << std::setprecision(4) << "  " << std::setw(12) << row.analytic << "  " << std::setw(12)
     << row.finite << "  " << std::setw(12) << row.max_abs << "  " << std::setw(12) << row.max_rel
     << "  " << (row.pass ? "pass" : "FAIL") << "\n";
}

}  // namespace

SteCpuReport RunSteGradCheck() {
  SteCpuReport report;
  std::ostringstream detail;
  detail << std::scientific << std::setprecision(4);
  detail << "Fase 3 STE-identity grad-check (tiny relu2 MLP)\n";
  detail << "finite-diff h=" << kSteFiniteDiffH << " of unquantized L; STE claims this derivative\n";
  detail << "tolerance max-abs " << kSteGradTolAbs << " max-rel " << kSteGradTolRel
         << " (rel enforced when |analytic|>= " << kSteGradRelSkipAbs << ")\n";
  detail << "FakeQuant discrete Q is not finite-diff'd (piecewise constant); see ADR 0002\n";

  TinySteNet net = MakeDefaultTinySteNet();
  std::string err;
  TinySteTensors io;
  if (!TinySteForwardBackward(net, true, io, err)) {
    report.ok = false;
    report.line = "FAILED: grad-check forward";
    report.detail = err;
    return report;
  }

  TinySteTensors qio;
  if (!TinySteForwardBackward(net, false, qio, err)) {
    report.ok = false;
    report.line = "FAILED: FakeQuant forward";
    report.detail = err;
    return report;
  }
  if (!TernaryCodesOk(qio.W1q, qio.scale1, err) || !TernaryCodesOk(qio.W2q, qio.scale2, err)) {
    report.ok = false;
    report.line = "FAILED: FakeQuant codes";
    report.detail = err;
    return report;
  }
  detail << "FakeQuant ternary absmean: scale1=" << qio.scale1 << " scale2=" << qio.scale2 << "\n";
  detail << "STE clip |W/s|<=1: W1 zeros="
         << std::count(qio.mask1.begin(), qio.mask1.end(), 0.0f)
         << " W2 zeros=" << std::count(qio.mask2.begin(), qio.mask2.end(), 0.0f) << "\n";

  std::string stub_err;
  if (FakeQuant2BitStub(net.W1.data(), net.W1.size(), io.W1q.data(), stub_err)) {
    report.ok = false;
    report.line = "FAILED: 2-bit stub should not succeed";
    report.detail = stub_err;
    return report;
  }
  detail << "2/4-bit FakeQuant stubs present (Fase 5): " << stub_err << "\n";

  std::vector<float> W1 = net.W1;
  std::vector<float> W2 = net.W2;
  std::vector<GradCheckRow> rows;
  auto check_one = [&](const char* prefix, std::size_t idx, bool is_w1, float analytic) {
    GradCheckRow row;
    row.name = std::string(prefix) + "[" + std::to_string(idx) + "]";
    row.analytic = analytic;
    float* slot = is_w1 ? &W1[idx] : &W2[idx];
    const float saved = *slot;
    *slot = saved + kSteFiniteDiffH;
    const float lp = TinySteLossAt(net, true, W1.data(), W2.data(), err);
    *slot = saved - kSteFiniteDiffH;
    const float lm = TinySteLossAt(net, true, W1.data(), W2.data(), err);
    *slot = saved;
    if (!err.empty() && lp == 0.0f && lm == 0.0f) {
      row.pass = false;
      rows.push_back(row);
      return;
    }
    row.finite = (lp - lm) / (2.0f * kSteFiniteDiffH);
    const MatmulError e = CompareFP32(1, &row.finite, &row.analytic, kSteGradRelFloor);
    row.max_abs = e.max_abs;
    row.max_rel = e.max_rel;
    const bool rel_ok = std::fabs(row.analytic) < kSteGradRelSkipAbs || e.max_rel <= kSteGradTolRel;
    row.pass = e.max_abs <= kSteGradTolAbs && rel_ok;
    rows.push_back(row);
  };

  err.clear();
  for (std::size_t i = 0; i < io.dW1.size(); ++i) {
    check_one("W1", i, true, io.dW1[i]);
  }
  for (std::size_t i = 0; i < io.dW2.size(); ++i) {
    check_one("W2", i, false, io.dW2[i]);
  }

  detail << "param       analytic      finite-diff      max-abs      max-rel  gate\n";
  bool all_ok = err.empty();
  for (const GradCheckRow& row : rows) {
    AppendRow(detail, row);
    report.max_abs = std::max(report.max_abs, row.max_abs);
    report.max_rel = std::max(report.max_rel, row.max_rel);
    all_ok = all_ok && row.pass;
  }
  detail << "worst max-abs " << report.max_abs << " max-rel " << report.max_rel << "\n";
  detail << "xllama was not modified. Not a tok/s result. Not a quality metric. "
            "Not a console result.";

  report.ok = all_ok;
  report.line = all_ok ? "STATUS: grad-check ok" : "FAILED: grad-check mismatch";
  report.detail = detail.str();
  return report;
}

SteCpuReport RunSteTrainStep(std::uint32_t steps) {
  SteCpuReport report;
  if (steps < 1) {
    report.ok = false;
    report.line = "FAILED: --train-step requires N >= 1";
    report.detail = "Fase 3 acceptance is N=1. No quality loop.";
    return report;
  }

  TinySteNet net = MakeDefaultTinySteNet();
  std::string err;
  TinySteTensors io;
  if (!TinySteForwardBackward(net, false, io, err)) {
    report.ok = false;
    report.line = "FAILED: train-step forward";
    report.detail = err;
    return report;
  }
  report.loss_before = io.loss;

  AdamWConfig cfg;
  AdamWState st1;
  AdamWState st2;
  AdamWReset(st1, net.W1.size());
  AdamWReset(st2, net.W2.size());

  float max_dw = 0.0f;
  float max_dw_delta = 0.0f;
  for (std::uint32_t s = 0; s < steps; ++s) {
    if (s > 0) {
      if (!TinySteForwardBackward(net, false, io, err)) {
        report.ok = false;
        report.line = "FAILED: train-step forward";
        report.detail = err;
        return report;
      }
    }
    for (float g : io.dW1) {
      max_dw = std::max(max_dw, std::fabs(g));
    }
    for (float g : io.dW2) {
      max_dw = std::max(max_dw, std::fabs(g));
    }
    std::vector<float> w1_before = net.W1;
    std::vector<float> w2_before = net.W2;
    AdamWStep(cfg, st1, net.W1.data(), io.dW1.data(), net.W1.size());
    AdamWStep(cfg, st2, net.W2.data(), io.dW2.data(), net.W2.size());
    for (std::size_t i = 0; i < net.W1.size(); ++i) {
      max_dw_delta = std::max(max_dw_delta, std::fabs(net.W1[i] - w1_before[i]));
    }
    for (std::size_t i = 0; i < net.W2.size(); ++i) {
      max_dw_delta = std::max(max_dw_delta, std::fabs(net.W2[i] - w2_before[i]));
    }
  }

  if (!TinySteForwardBackward(net, false, io, err)) {
    report.ok = false;
    report.line = "FAILED: train-step post forward";
    report.detail = err;
    return report;
  }
  report.loss_after = io.loss;

  std::ostringstream detail;
  detail << std::scientific << std::setprecision(4);
  detail << "Fase 3 one AdamW/STE step on master fp32 (tiny relu2 MLP)\n";
  detail << "FakeQuant: ternary absmean. STE clip |W/s|<=1. AdamW on master only.\n";
  detail << "lr=" << cfg.lr << " beta1=" << cfg.beta1 << " beta2=" << cfg.beta2
         << " eps=" << cfg.eps << " wd=" << cfg.weight_decay << "\n";
  detail << "steps=" << steps << " (acceptance is 1)\n";
  detail << "loss_before " << report.loss_before << " loss_after " << report.loss_after << "\n";
  detail << "max|dW| " << max_dw << " max|delta W| " << max_dw_delta << "\n";
  detail << "scale1 " << io.scale1 << " scale2 " << io.scale2 << "\n";
  detail << "DirectML is not the optimizer. Not a tok/s result. Not a quality curve. "
            "Not a console result.";

  report.ok = true;
  report.line = "STATUS: train-step ok";
  report.detail = detail.str();
  return report;
}
