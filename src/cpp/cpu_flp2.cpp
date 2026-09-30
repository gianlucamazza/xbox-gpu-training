#include "cpu_flp2.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

bool Flp2Decode(const Flp2Coded& coded, std::vector<float>& weight, std::string& err) {
  if (coded.levels < 2) {
    err = "FLP2 decode: levels must be >= 2";
    return false;
  }
  const std::size_t n =
      static_cast<std::size_t>(coded.rows) * static_cast<std::size_t>(coded.cols);
  if (coded.symbols.size() != n || coded.scales.size() != coded.rows) {
    err = "FLP2 decode: symbol/scale size mismatch";
    return false;
  }
  const float halfv = (static_cast<float>(coded.levels) - 1.0f) * 0.5f;
  weight.resize(n);
  for (std::uint32_t r = 0; r < coded.rows; ++r) {
    const float scale = coded.scales[r];
    for (std::uint32_t c = 0; c < coded.cols; ++c) {
      const std::size_t i = static_cast<std::size_t>(r) * coded.cols + c;
      const std::uint32_t sym = coded.symbols[i];
      if (sym >= coded.levels) {
        err = "FLP2 decode: symbol out of range";
        return false;
      }
      weight[i] = (static_cast<float>(sym) - halfv) * scale;
    }
  }
  return true;
}

void Flp2RmsNorm(std::uint32_t count, std::uint32_t dim, float eps, const float* x,
                 const float* gamma, float* y) {
  for (std::uint32_t row = 0; row < count; ++row) {
    float acc = 0.0f;
    const float* xr = x + static_cast<std::size_t>(row) * dim;
    float* yr = y + static_cast<std::size_t>(row) * dim;
    for (std::uint32_t i = 0; i < dim; ++i) {
      acc += xr[i] * xr[i];
    }
    const float inv = 1.0f / std::sqrt(acc / static_cast<float>(dim) + eps);
    for (std::uint32_t i = 0; i < dim; ++i) {
      yr[i] = xr[i] * inv * gamma[i];
    }
  }
}

void Flp2Rope(std::uint32_t seq, std::uint32_t heads, std::uint32_t head_dim, float theta,
              const float* x, float* y) {
  for (std::uint32_t t = 0; t < seq; ++t) {
    for (std::uint32_t h = 0; h < heads; ++h) {
      const std::size_t base =
          (static_cast<std::size_t>(t) * heads + h) * static_cast<std::size_t>(head_dim);
      for (std::uint32_t p = 0; p < head_dim / 2; ++p) {
        const float freq =
            std::pow(theta, -(static_cast<float>(2 * p) / static_cast<float>(head_dim)));
        const float ang = static_cast<float>(t) * freq;
        const float c = std::cos(ang);
        const float s = std::sin(ang);
        const float x0 = x[base + 2 * p];
        const float x1 = x[base + 2 * p + 1];
        y[base + 2 * p] = x0 * c - x1 * s;
        y[base + 2 * p + 1] = x0 * s + x1 * c;
      }
    }
  }
}

namespace {

bool Linear(const std::vector<float>& x, std::uint32_t rows, std::uint32_t k,
            const std::vector<float>& w, std::uint32_t out, std::vector<float>& y,
            std::string& err) {
  if (x.size() != static_cast<std::size_t>(rows) * k ||
      w.size() != static_cast<std::size_t>(out) * k) {
    err = "linear: shape mismatch";
    return false;
  }
  y.assign(static_cast<std::size_t>(rows) * out, 0.0f);
  for (std::uint32_t r = 0; r < rows; ++r) {
    for (std::uint32_t o = 0; o < out; ++o) {
      float acc = 0.0f;
      for (std::uint32_t i = 0; i < k; ++i) {
        acc += x[static_cast<std::size_t>(r) * k + i] * w[static_cast<std::size_t>(o) * k + i];
      }
      y[static_cast<std::size_t>(r) * out + o] = acc;
    }
  }
  return true;
}

void CausalAttn(std::uint32_t seq, std::uint32_t heads, std::uint32_t dh, const float* q,
                const float* k, const float* v, float* out) {
  const float scale = 1.0f / std::sqrt(static_cast<float>(dh));
  const std::size_t stride = static_cast<std::size_t>(heads) * dh;
  for (std::uint32_t t = 0; t < seq; ++t) {
    for (std::uint32_t h = 0; h < heads; ++h) {
      std::vector<float> scores(t + 1);
      float m = -1.0e30f;
      for (std::uint32_t s = 0; s <= t; ++s) {
        float dot = 0.0f;
        for (std::uint32_t i = 0; i < dh; ++i) {
          dot += q[(static_cast<std::size_t>(t) * stride) + h * dh + i] *
                 k[(static_cast<std::size_t>(s) * stride) + h * dh + i];
        }
        scores[s] = dot * scale;
        if (scores[s] > m) {
          m = scores[s];
        }
      }
      float sum = 0.0f;
      for (std::uint32_t s = 0; s <= t; ++s) {
        scores[s] = std::exp(scores[s] - m);
        sum += scores[s];
      }
      for (std::uint32_t i = 0; i < dh; ++i) {
        float acc = 0.0f;
        for (std::uint32_t s = 0; s <= t; ++s) {
          acc += (scores[s] / sum) * v[(static_cast<std::size_t>(s) * stride) + h * dh + i];
        }
        out[(static_cast<std::size_t>(t) * stride) + h * dh + i] = acc;
      }
    }
  }
}

}  // namespace

bool Flp2Forward(const Flp2Fixture& fx, std::vector<float>& logits, std::string& err) {
  const Flp2Config& c = fx.cfg;
  if (c.seq == 0 || c.d == 0 || c.n_heads == 0 || c.d % c.n_heads != 0 ||
      (c.d / c.n_heads) % 2 != 0 || c.vocab == 0 || fx.tokens.size() != c.seq) {
    err = "forward: invalid tiny-fixture config";
    return false;
  }
  for (std::uint32_t t : fx.tokens) {
    if (t >= c.vocab) {
      err = "forward: token id out of vocab";
      return false;
    }
  }

  std::vector<float> w_emb;
  std::vector<float> w_qkv;
  std::vector<float> w_proj;
  std::vector<float> w_fc;
  std::vector<float> w_fc2;
  if (!Flp2Decode(fx.emb, w_emb, err) || !Flp2Decode(fx.qkv, w_qkv, err) ||
      !Flp2Decode(fx.proj, w_proj, err) || !Flp2Decode(fx.fc, w_fc, err) ||
      !Flp2Decode(fx.fc2, w_fc2, err)) {
    return false;
  }

  std::vector<float> hidden(static_cast<std::size_t>(c.seq) * c.d);
  for (std::uint32_t t = 0; t < c.seq; ++t) {
    const std::uint32_t id = fx.tokens[t];
    for (std::uint32_t d = 0; d < c.d; ++d) {
      hidden[static_cast<std::size_t>(t) * c.d + d] =
          w_emb[static_cast<std::size_t>(id) * c.d + d];
    }
  }

  std::vector<float> xn(hidden.size());
  Flp2RmsNorm(c.seq, c.d, c.eps, hidden.data(), fx.norm1.data(), xn.data());

  std::vector<float> qkv;
  if (!Linear(xn, c.seq, c.d, w_qkv, 3 * c.d, qkv, err)) {
    return false;
  }

  const std::uint32_t dh = Flp2HeadDim(c);
  std::vector<float> q(static_cast<std::size_t>(c.seq) * c.d);
  std::vector<float> k(q.size());
  std::vector<float> v(q.size());
  for (std::uint32_t t = 0; t < c.seq; ++t) {
    for (std::uint32_t d = 0; d < c.d; ++d) {
      const std::size_t base = static_cast<std::size_t>(t) * (3 * c.d);
      q[static_cast<std::size_t>(t) * c.d + d] = qkv[base + d];
      k[static_cast<std::size_t>(t) * c.d + d] = qkv[base + c.d + d];
      v[static_cast<std::size_t>(t) * c.d + d] = qkv[base + 2 * c.d + d];
    }
  }

  std::vector<float> q_r(q.size());
  std::vector<float> k_r(k.size());
  Flp2Rope(c.seq, c.n_heads, dh, c.rope_theta, q.data(), q_r.data());
  Flp2Rope(c.seq, c.n_heads, dh, c.rope_theta, k.data(), k_r.data());

  std::vector<float> attn(q.size());
  CausalAttn(c.seq, c.n_heads, dh, q_r.data(), k_r.data(), v.data(), attn.data());

  std::vector<float> proj;
  if (!Linear(attn, c.seq, c.d, w_proj, c.d, proj, err)) {
    return false;
  }
  for (std::size_t i = 0; i < hidden.size(); ++i) {
    hidden[i] += proj[i];
  }

  Flp2RmsNorm(c.seq, c.d, c.eps, hidden.data(), fx.norm2.data(), xn.data());
  std::vector<float> ff;
  if (!Linear(xn, c.seq, c.d, w_fc, c.d_ff, ff, err)) {
    return false;
  }
  for (float& h : ff) {
    const float r = h > 0.0f ? h : 0.0f;
    h = r * r;
  }
  std::vector<float> ff2;
  if (!Linear(ff, c.seq, c.d_ff, w_fc2, c.d, ff2, err)) {
    return false;
  }
  for (std::size_t i = 0; i < hidden.size(); ++i) {
    hidden[i] += ff2[i];
  }

  Flp2RmsNorm(c.seq, c.d, c.eps, hidden.data(), fx.norm.data(), xn.data());
  if (!Linear(xn, c.seq, c.d, w_emb, c.vocab, logits, err)) {
    return false;
  }
  return true;
}

namespace {

bool HandOpTests(std::string& detail, float& worst_abs, float& worst_rel) {
  std::ostringstream os;
  os << std::scientific << std::setprecision(4);

  {
    Flp2Coded coded;
    coded.rows = 1;
    coded.cols = 3;
    coded.levels = 3;
    coded.symbols = {0, 1, 2};
    coded.scales = {0.5f};
    std::vector<float> w;
    std::string err;
    if (!Flp2Decode(coded, w, err)) {
      detail = err;
      return false;
    }
    const float expect[] = {-0.5f, 0.0f, 0.5f};
    const MatmulError e = CompareFP32(3, w.data(), expect, kFlp2RelFloor);
    worst_abs = std::max(worst_abs, e.max_abs);
    worst_rel = std::max(worst_rel, e.max_rel);
    if (!WithinTolerance(e, kFlp2TolAbs, kFlp2TolRel)) {
      detail = "decode ternary hand failed";
      return false;
    }
    os << "decode ternary: max-abs " << e.max_abs << " max-rel " << e.max_rel << "\n";
  }

  {
    Flp2Coded coded;
    coded.rows = 1;
    coded.cols = 4;
    coded.levels = 4;
    coded.symbols = {0, 1, 2, 3};
    coded.scales = {1.0f};
    std::vector<float> w;
    std::string err;
    if (!Flp2Decode(coded, w, err)) {
      detail = err;
      return false;
    }
    const float expect[] = {-1.5f, -0.5f, 0.5f, 1.5f};
    const MatmulError e = CompareFP32(4, w.data(), expect, kFlp2RelFloor);
    worst_abs = std::max(worst_abs, e.max_abs);
    worst_rel = std::max(worst_rel, e.max_rel);
    if (!WithinTolerance(e, kFlp2TolAbs, kFlp2TolRel)) {
      detail = "decode 2-bit hand failed";
      return false;
    }
    os << "decode 2-bit: max-abs " << e.max_abs << " max-rel " << e.max_rel << "\n";
  }

  {
    const float x[] = {3.0f, 4.0f};
    const float g[] = {1.0f, 1.0f};
    float y[2] = {};
    Flp2RmsNorm(1, 2, 0.0f, x, g, y);
    const float inv = 1.0f / std::sqrt(12.5f);
    const float expect[] = {3.0f * inv, 4.0f * inv};
    const MatmulError e = CompareFP32(2, y, expect, kFlp2RelFloor);
    worst_abs = std::max(worst_abs, e.max_abs);
    worst_rel = std::max(worst_rel, e.max_rel);
    if (!WithinTolerance(e, kFlp2TolAbs, kFlp2TolRel)) {
      detail = "rmsnorm hand failed";
      return false;
    }
    os << "rmsnorm 3-4: max-abs " << e.max_abs << " max-rel " << e.max_rel << "\n";
  }

  {
    const float x[] = {1.0f, 0.0f, 1.0f, 0.0f};
    float y[4] = {};
    Flp2Rope(1, 1, 4, 10000.0f, x, y);
    const float expect[] = {1.0f, 0.0f, 1.0f, 0.0f};
    const MatmulError e = CompareFP32(4, y, expect, kFlp2RelFloor);
    worst_abs = std::max(worst_abs, e.max_abs);
    worst_rel = std::max(worst_rel, e.max_rel);
    if (!WithinTolerance(e, kFlp2TolAbs, kFlp2TolRel)) {
      detail = "rope t=0 identity failed";
      return false;
    }
    os << "rope t=0: max-abs " << e.max_abs << " max-rel " << e.max_rel << "\n";
  }

  detail += os.str();
  return true;
}

}  // namespace

Flp2CpuReport RunFlp2CpuReference(const Flp2Fixture& fx) {
  Flp2CpuReport report;
  std::ostringstream detail;
  detail << std::scientific << std::setprecision(4);
  detail << "FLP2 CPU reference (scalar decode + RMSNorm + RoPE + tiny relu2 forward)\n";
  detail << "tolerance max-abs " << kFlp2TolAbs << " max-rel " << kFlp2TolRel << "\n";
  detail << "fixture: " << fx.name << " schema " << fx.schema << "\n";
  detail << "binary FLP2 envelope (rANS) is not unpacked here; see docs/flp2-forward.md\n";

  std::string hand;
  if (!HandOpTests(hand, report.max_abs, report.max_rel)) {
    report.ok = false;
    report.line = "FAILED: flp2 cpu-ref hand ops";
    report.detail = hand;
    return report;
  }
  detail << hand;

  std::vector<float> logits;
  std::string err;
  if (!Flp2Forward(fx, logits, err)) {
    report.ok = false;
    report.line = "FAILED: flp2 cpu-ref forward";
    report.detail = err;
    return report;
  }

  if (fx.expected_logits.size() != logits.size()) {
    report.ok = false;
    report.line = "FAILED: flp2 fixture expected size";
    detail << "expected " << fx.expected_logits.size() << " logits, got " << logits.size() << "\n";
    report.detail = detail.str();
    return report;
  }

  const MatmulError errv =
      CompareFP32(logits.size(), logits.data(), fx.expected_logits.data(), kFlp2RelFloor);
  report.max_abs = std::max(report.max_abs, errv.max_abs);
  report.max_rel = std::max(report.max_rel, errv.max_rel);
  detail << "fixture forward: max-abs " << errv.max_abs << " max-rel " << errv.max_rel << "\n";
  if (!WithinTolerance(errv, kFlp2TolAbs, kFlp2TolRel)) {
    report.ok = false;
    report.line = "FAILED: flp2 fixture mismatch";
    report.detail = detail.str();
    return report;
  }

  report.ok = true;
  report.line = "STATUS: flp2 cpu-ref ok";
  detail << "worst max-abs " << report.max_abs << " max-rel " << report.max_rel << "\n";
  detail << "xllama was not modified. Not a tok/s result. Not a GPU result. Not a console result.";
  report.detail = detail.str();
  return report;
}
