#include "model.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace e0 {
Config::Config(const Json &c) {
  d = c.at("d");
  layers = c.at("n_layers");
  heads = c.at("n_heads");
  ff = c.at("d_ff");
  vocab = c.at("vocab");
  ctx = c.at("ctx");
  mlp = c.at("mlp");
  format = c.at("core_fmt");
  policy = c.at("scale_policy");
  delta = c.at("delta");
  qk_norm = c.at("qk_norm");
  if (!d || !layers || !heads || !ff || !ctx || vocab != 256 || d % heads ||
      (d / heads) % 2)
    throw std::runtime_error("invalid E0 byte-model dimensions");
  if (c.at("emb_fmt") != "4bit" || (format != "ternary" && format != "2bit") ||
      (mlp != "gelu" && mlp != "relu2" && mlp != "swiglu") ||
      (policy != "row16" && policy != "row8log" && policy != "tensor16") ||
      !std::isfinite(delta) || delta < 0 || delta >= 4)
    throw std::runtime_error("unsupported E0 recipe");
}
float stored_fp16(float v) {
  if (!std::isfinite(v) || std::abs(v) > 65504.0f)
    throw std::runtime_error("fp16 overflow/nonfinite");
  uint32_t bits;
  std::memcpy(&bits, &v, 4);
  const uint32_t sign = bits & 0x80000000u;
  int exponent = int((bits >> 23) & 255) - 127;
  if (exponent < -25)
    return std::copysign(0.0f, v);
  uint32_t mantissa = (bits & 0x7fffffu) | 0x800000u;
  const int shift = exponent < -14 ? (-exponent - 1) : 13;
  const uint32_t mask = (uint32_t(1) << shift) - 1, halfway = uint32_t(1)
                                                              << (shift - 1);
  uint32_t rounded = mantissa >> shift;
  const uint32_t remainder = mantissa & mask;
  if (remainder > halfway || (remainder == halfway && (rounded & 1)))
    ++rounded;
  float result =
      std::ldexp(float(rounded), exponent < -14 ? -24 : exponent - 10);
  return sign ? -result : result;
}
namespace {
float positive_scale(float s) {
  if (!std::isfinite(s) || s < 0 || s > 65504)
    throw std::runtime_error("invalid scale");
  return stored_fp16(s > 0 ? std::max(s, 6.103515625e-5f) : 0);
}
} // namespace
Quantized quantize(const Values &w, uint32_t rows, uint32_t cols,
                   const std::string &fmt, const std::string &policy,
                   float delta) {
  if (w.size() != uint64_t(rows) * cols || !rows || !cols)
    throw std::runtime_error("quantized shape mismatch");
  const bool ternary = fmt == "ternary";
  const float half = ternary ? 1.0f : (fmt == "2bit" ? 1.5f : 7.5f);
  if (!ternary && fmt != "2bit" && fmt != "4bit")
    throw std::runtime_error("invalid quantizer");
  Quantized out;
  out.scales.resize(rows);
  out.symbols.resize(w.size());
  out.value.resize(w.size());
  Values means(rows), raw(rows);
  for (uint32_t r = 0; r < rows; ++r) {
    float sum = 0;
    for (uint32_t c = 0; c < cols; ++c) {
      float a = std::abs(w[r * cols + c]);
      if (!std::isfinite(a))
        throw std::runtime_error("nonfinite master weight");
      sum += a;
    }
    means[r] = sum / cols;
  }
  if (policy == "tensor16") {
    float sum = 0;
    for (float a : w)
      sum += std::abs(a);
    std::fill(means.begin(), means.end(), sum / float(w.size()));
  }
  uint64_t total_count = 0;
  float total_kept = 0;
  for (uint32_t r = 0; r < rows; ++r) {
    if (ternary) {
      float kept = 0;
      uint32_t cnt = 0;
      for (uint32_t c = 0; c < cols; ++c)
        if (std::abs(w[r * cols + c]) > delta * means[r]) {
          kept += std::abs(w[r * cols + c]);
          ++cnt;
        }
      raw[r] = cnt ? kept / cnt : 0;
      total_kept += kept;
      total_count += cnt;
    } else
      raw[r] = means[r] * (fmt == "4bit" ? 0.42f : 1.135f);
  }
  if (ternary && policy == "tensor16")
    std::fill(raw.begin(), raw.end(),
              total_count ? total_kept / float(total_count) : 0);
  if (policy == "row16" || policy == "tensor16")
    for (uint32_t r = 0; r < rows; ++r)
      out.scales[r] = positive_scale(raw[r]);
  else if (policy == "row8log") {
    float base = positive_scale(*std::max_element(raw.begin(), raw.end()));
    for (uint32_t r = 0; r < rows; ++r) {
      if (raw[r] == 0 || base == 0)
        out.scales[r] = 0;
      else {
        float code =
            std::nearbyint(std::log2(std::max(raw[r], 1e-30f) / base) * 16) +
            128;
        code = std::clamp(code, 1.0f, 255.0f);
        out.scales[r] = base * std::exp2((code - 128) / 16);
      }
    }
  } else
    throw std::runtime_error("invalid scale policy");
  for (uint32_t r = 0; r < rows; ++r)
    for (uint32_t c = 0; c < cols; ++c) {
      auto i = r * cols + c;
      float q;
      if (ternary)
        q = std::abs(w[i]) > delta * means[r] ? (w[i] > 0 ? 1.0f : -1.0f) : 0;
      else
        q = std::clamp(
            std::floor(out.scales[r] > 0 ? w[i] / out.scales[r] : 0) + 0.5f,
            -half, half);
      out.symbols[i] = uint32_t(std::nearbyint(q + half));
      out.value[i] = (float(out.symbols[i]) - half) * out.scales[r];
    }
  return out;
}
Model::Model(const Json &job) : config(job.at("config")) {
  std::vector<std::tuple<std::string, uint32_t, uint32_t, bool>> expected;
  expected.emplace_back("emb.weight", config.vocab, config.d, false);
  for (uint32_t l = 0; l < config.layers; ++l) {
    const auto prefix = "blocks." + std::to_string(l) + ".";
    expected.emplace_back(prefix + "norm1.weight", 1, config.d, true);
    expected.emplace_back(prefix + "qkv.weight", 3 * config.d, config.d, false);
    expected.emplace_back(prefix + "proj.weight", config.d, config.d, false);
    expected.emplace_back(prefix + "norm2.weight", 1, config.d, true);
    expected.emplace_back(prefix + "fc.weight",
                          config.mlp == "swiglu" ? 2 * config.ff : config.ff,
                          config.d, false);
    expected.emplace_back(prefix + "fc2.weight", config.d, config.ff, false);
  }
  expected.emplace_back("norm.weight", 1, config.d, true);
  const auto &tensors = job.at("tensors");
  if (tensors.size() != expected.size())
    throw std::runtime_error("incorrect parameter count");
  for (size_t i = 0; i < expected.size(); ++i) {
    auto [name, r, c, norm] = expected[i];
    const auto &t = tensors.at(i);
    if (t.at("name") != name || t.at("rows") != r || t.at("cols") != c ||
        t.at("norm") != norm)
      throw std::runtime_error("tensor metadata mismatch: " + name);
    Parameter p{name, r, c, norm, t.at("values").get<Values>(), {}, {}};
    if (p.value.size() != uint64_t(r) * c || p.value.size() > UINT32_MAX)
      throw std::runtime_error("tensor size mismatch");
    for (float v : p.value)
      if (!std::isfinite(v))
        throw std::runtime_error("nonfinite parameter");
    p.first.resize(p.value.size());
    p.second.resize(p.value.size());
    parameters.push_back(std::move(p));
  }
}
Json Model::tensors() const {
  Json out = Json::array();
  for (const auto &p : parameters)
    out.push_back({{"name", p.name},
                   {"rows", p.rows},
                   {"cols", p.cols},
                   {"norm", p.norm},
                   {"values", p.value}});
  return out;
}
Json Model::optimizer_state() const {
  Json moments = Json::array();
  for (const auto &p : parameters)
    moments.push_back(
        {{"name", p.name}, {"first", p.first}, {"second", p.second}});
  return {{"tensors", tensors()}, {"moments", moments}};
}
Json fixture_report(const Json &f, Kernel &kernel) {
  kernel.dispatches = kernel.transfer_bytes = kernel.peak_memory_bytes = 0;
  kernel.gpu_seconds = 0;
  kernel.observe();
  Model model(f);
  auto result = model.step(kernel, f.at("tokens").get<Values>(),
                           f.at("targets").get<Values>(), f.at("batch"),
                           f.at("lr"), f.at("wd"), 1, true, true);
  Json coded = Json::array();
  Model initial(f);
  for (const auto &p : initial.parameters)
    if (!p.norm) {
      auto q = quantize(p.value, p.rows, p.cols,
                        p.name == "emb.weight" ? "4bit" : model.config.format,
                        model.config.policy, model.config.delta);
      coded.push_back(
          {{"name", p.name}, {"symbols", q.symbols}, {"scales", q.scales}});
    }
  result["quantization"] = coded;
  return result;
}
Json kernel_fixture_report(const Json &fixture, Kernel &kernel) {
  kernel.dispatches = kernel.transfer_bytes = kernel.peak_memory_bytes = 0;
  kernel.gpu_seconds = 0;
  kernel.observe();
  Json results = Json::array();
  for (const auto &item : fixture.at("cases")) {
    const auto &spec = item.at("command");
    const auto op = spec.at("op").get<uint32_t>();
    if (op > uint32_t(Op::Weighted))
      throw std::runtime_error("invalid kernel fixture operation");
    Command command{Op(op)};
    command.mode = spec.value("mode", 0u);
    command.count = spec.at("count");
    command.rows = spec.value("rows", 0u);
    command.cols = spec.value("cols", 0u);
    command.out = spec.value("out", 0u);
    command.batch = spec.value("batch", 0u);
    command.seq = spec.value("seq", 0u);
    command.heads = spec.value("heads", 0u);
    command.aux = spec.value("aux", 0u);
    command.epsilon = spec.value("epsilon", 1.1920928955078125e-7f);
    if (!command.count || command.count > (1u << 24) || command.mode > 3)
      throw std::runtime_error("invalid kernel fixture command");
    const auto &inputs = item.at("inputs");
    if (inputs.size() != 5)
      throw std::runtime_error("kernel fixture requires five input buffers");
    std::vector<Values> buffers;
    for (const auto &input : inputs)
      buffers.push_back(input.get<Values>());
    const auto before = kernel.dispatches;
    auto values = kernel.run(command, buffers[0], buffers[1], buffers[2],
                             buffers[3], buffers[4]);
    results.push_back({{"id", item.at("id")},
                       {"values", values},
                       {"dispatches", kernel.dispatches - before}});
  }
  return {{"schema", "floppylm.e0.kernels.result.v1"},
          {"cases", results},
          {"hardware_gpu", kernel.hardware()},
          {"adapter", kernel.adapter()},
          {"dispatches", kernel.dispatches},
          {"gpu_seconds", kernel.gpu_seconds},
          {"transfer_bytes", kernel.transfer_bytes},
          {"peak_memory_bytes", kernel.peak_memory_bytes}};
}
Json optimizer_fixture_report(const Json &f) {
  Model model(f);
  Json results = Json::array();
  for (const auto &s : f.at("steps")) {
    std::vector<Values> gradients;
    for (size_t i = 0; i < model.parameters.size(); ++i) {
      const auto &g = s.at("gradients").at(i);
      if (g.at("name") != model.parameters[i].name)
        throw std::runtime_error("optimizer fixture gradient order mismatch");
      gradients.push_back(g.at("values").get<Values>());
    }
    float norm =
        model.apply_gradients(gradients, s.at("lr"), s.at("wd"), s.at("step"));
    auto result = model.optimizer_state();
    result["gradient_norm"] = norm;
    results.push_back(result);
  }
  return {
      {"purpose", "functional"}, {"optimizer_host", true}, {"steps", results}};
}
Json Model::checkpoint(uint64_t step, const Json &job) const {
  Json moments = Json::array();
  for (const auto &p : parameters)
    moments.push_back(
        {{"name", p.name}, {"first", p.first}, {"second", p.second}});
  return {
      {"schema", "floppylm.checkpoint.v1"},
      {"job_id", job.at("job_id")},
      {"config", job.at("config")},
      {"spec", job.at("spec")},
      {"data", job.at("data")},
      {"indices", job.at("indices")},
      {"step", step},
      {"stream_position", step * job.at("spec").at("batch").get<uint64_t>()},
      {"tensors", tensors()},
      {"moments", moments},
      {"initialization_sha256", job.at("initialization_sha256")}};
}
void Model::restore(const Json &ckpt, const Json &job, uint64_t &step) {
  for (const char *key :
       {"job_id", "config", "spec", "data", "indices", "initialization_sha256"})
    if (ckpt.at(key) != job.at(key))
      throw std::runtime_error(std::string("checkpoint mismatch: ") + key);
  if (ckpt.at("schema") != "floppylm.checkpoint.v1")
    throw std::runtime_error("checkpoint schema mismatch");
  Model restored(
      Json{{"config", ckpt.at("config")}, {"tensors", ckpt.at("tensors")}});
  if (ckpt.at("moments").size() != parameters.size())
    throw std::runtime_error("checkpoint moment count mismatch");
  for (size_t i = 0; i < parameters.size(); ++i) {
    const auto &m = ckpt.at("moments").at(i);
    auto &p = restored.parameters[i];
    if (m.at("name") != p.name)
      throw std::runtime_error("checkpoint moment name mismatch");
    p.first = m.at("first").get<Values>();
    p.second = m.at("second").get<Values>();
    if (p.first.size() != p.value.size() || p.second.size() != p.value.size())
      throw std::runtime_error("checkpoint moment shape mismatch");
    for (float v : p.first)
      if (!std::isfinite(v))
        throw std::runtime_error("nonfinite first moment");
    for (float v : p.second)
      if (!std::isfinite(v) || v < 0)
        throw std::runtime_error("invalid second moment");
  }
  step = ckpt.at("step");
  if (ckpt.at("stream_position") !=
      step * job.at("spec").at("batch").get<uint64_t>())
    throw std::runtime_error("checkpoint stream mismatch");
  parameters = std::move(restored.parameters);
}
int Model::forward(Graph &g, const Values &tokens, uint32_t batch,
                   std::vector<int> &masters) {
  const auto &c = config;
  uint32_t R = batch * c.ctx;
  std::vector<int> weights;
  for (const auto &p : parameters) {
    int master = g.leaf(p.value, true);
    masters.push_back(master);
    Values value;
    if (p.norm) {
      value = p.value;
      for (float &v : value)
        v = stored_fp16(v);
    } else
      value = quantize(p.value, p.rows, p.cols,
                       p.name == "emb.weight" ? "4bit" : c.format, c.policy,
                       c.delta)
                  .value;
    weights.push_back(g.identity(master, std::move(value)));
  }
  auto cmd = [&](Op op, uint32_t count, uint32_t rows, uint32_t cols,
                 uint32_t out = 0, uint32_t aux = 0) {
    Command p{op};
    p.count = count;
    p.rows = rows;
    p.cols = cols;
    p.out = out;
    p.batch = batch;
    p.seq = c.ctx;
    p.heads = c.heads;
    p.aux = aux;
    return p;
  };
  auto norm = [&](int x, int w, uint32_t rows, uint32_t dim) {
    return g.apply(cmd(Op::Norm, rows * dim, rows, dim), {x, w});
  };
  auto linear = [&](int x, int w, uint32_t in, uint32_t out) {
    return g.apply(cmd(Op::Linear, R * out, R, in, out), {x, w});
  };
  int token = g.leaf(tokens),
      x = g.apply(cmd(Op::Embed, R * c.d, R, c.d), {token, weights[0]});
  for (uint32_t l = 0; l < c.layers; ++l) {
    auto n = 1 + l * 6;
    int qkv = linear(norm(x, weights[n], R, c.d), weights[n + 1], c.d, 3 * c.d);
    int heads[3];
    for (uint32_t i = 0; i < 3; ++i)
      heads[i] = g.apply(cmd(Op::Heads, R * c.d, R, c.d, 0, i), {qkv});
    const uint32_t hd = c.d / c.heads, hr = R * c.heads;
    if (c.qk_norm) {
      int ones = g.leaf(Values(hd, 1));
      heads[0] = norm(heads[0], ones, hr, hd);
      heads[1] = norm(heads[1], ones, hr, hd);
    }
    heads[0] = g.apply(cmd(Op::Rope, R * c.d, hr, hd), {heads[0]});
    heads[1] = g.apply(cmd(Op::Rope, R * c.d, hr, hd), {heads[1]});
    int scores =
        g.apply(cmd(Op::Scores, hr * c.ctx, hr, hd), {heads[0], heads[1]});
    int probabilities =
        g.apply(cmd(Op::Softmax, hr * c.ctx, hr, c.ctx), {scores});
    int a =
        g.apply(cmd(Op::Weighted, R * c.d, hr, hd), {probabilities, heads[2]});
    a = g.apply(cmd(Op::Unheads, R * c.d, R, c.d), {a});
    x = g.apply(cmd(Op::Add, R * c.d, R, c.d),
                {x, linear(a, weights[n + 2], c.d, c.d)});
    const uint32_t up = c.mlp == "swiglu" ? 2 * c.ff : c.ff;
    int h = linear(norm(x, weights[n + 3], R, c.d), weights[n + 4], c.d, up);
    if (c.mlp == "swiglu") {
      int a = g.apply(cmd(Op::Slice, R * c.ff, R, up, c.ff, 0), {h});
      int b = g.apply(cmd(Op::Slice, R * c.ff, R, up, c.ff, 1), {h});
      a = g.apply(cmd(Op::Activation, R * c.ff, R, c.ff, 0, 2), {a});
      h = g.apply(cmd(Op::Multiply, R * c.ff, R, c.ff), {a, b});
    } else
      h = g.apply(
          cmd(Op::Activation, R * c.ff, R, c.ff, 0, c.mlp == "relu2" ? 1 : 0),
          {h});
    x = g.apply(cmd(Op::Add, R * c.d, R, c.d),
                {x, linear(h, weights[n + 5], c.ff, c.d)});
  }
  return linear(norm(x, weights.back(), R, c.d), weights[0], c.d, c.vocab);
}
float Model::apply_gradients(const std::vector<Values> &gradients, float lr,
                             float wd, uint64_t t) {
  if (!std::isfinite(lr) || lr < 0 || !std::isfinite(wd) || wd < 0)
    throw std::runtime_error("invalid optimizer lr/decay");
  if (gradients.size() != parameters.size() || !t)
    throw std::runtime_error("invalid optimizer gradient count/step");
  double norm_squared = 0;
  for (size_t i = 0; i < parameters.size(); ++i) {
    if (gradients[i].size() != parameters[i].value.size())
      throw std::runtime_error("optimizer gradient shape mismatch");
    for (float v : gradients[i]) {
      if (!std::isfinite(v))
        throw std::runtime_error("nonfinite gradient");
      norm_squared += double(v) * v;
    }
  }
  const float norm = float(std::sqrt(norm_squared)),
              clip = std::min(1.0f, 1.0f / (norm + 1e-6f));
  const double correction1 = 1 - std::pow(0.9, double(t)),
               correction2 = 1 - std::pow(0.95, double(t));
  for (size_t i = 0; i < parameters.size(); ++i) {
    auto &q = parameters[i];
    for (size_t j = 0; j < q.value.size(); ++j) {
      const float a = gradients[i][j] * clip;
      q.first[j] = 0.9f * q.first[j] + 0.1f * a;
      q.second[j] = 0.95f * q.second[j] + 0.05f * a * a;
      q.value[j] *= 1 - lr * (q.norm ? 0 : wd);
      q.value[j] -=
          float(double(lr) / correction1) * q.first[j] /
          (std::sqrt(q.second[j]) / float(std::sqrt(correction2)) + 1e-8f);
      if (!std::isfinite(q.value[j]))
        throw std::runtime_error("nonfinite optimizer update");
    }
  }
  return norm;
}
Json Model::step(Kernel &kernel, const Values &tokens, const Values &targets,
                 uint32_t batch, float lr, float wd, uint64_t t, bool update,
                 bool details) {
  if (!batch || tokens.size() != uint64_t(batch) * config.ctx ||
      targets.size() != tokens.size() || !t)
    throw std::runtime_error("invalid training batch/step");
  for (float v : tokens)
    if (v < 0 || v >= config.vocab || v != std::floor(v))
      throw std::runtime_error("invalid input token");
  for (float v : targets)
    if (v < 0 || v >= config.vocab || v != std::floor(v))
      throw std::runtime_error("invalid target token");
  std::vector<Values> collected(parameters.size());
  Values all_logits;
  double sum = 0;
  for (uint32_t sample = 0; sample < batch; ++sample) {
    Values sample_tokens(tokens.begin() + sample * config.ctx,
                         tokens.begin() + (sample + 1) * config.ctx);
    Values sample_targets(targets.begin() + sample * config.ctx,
                          targets.begin() + (sample + 1) * config.ctx);
    Graph g(kernel);
    std::vector<int> ids;
    int logits = forward(g, sample_tokens, 1, ids);
    Command p{Op::CrossEntropy};
    p.count = config.ctx;
    p.rows = p.count;
    p.cols = config.vocab;
    int loss = g.apply(p, {logits, g.leaf(sample_targets)});
    sum += std::accumulate(g[loss].value.begin(), g[loss].value.end(), 0.0);
    if (!std::isfinite(sum))
      throw std::runtime_error("nonfinite loss");
    g.backward(loss, 1.0f / float(tokens.size()));
    if (details)
      all_logits.insert(all_logits.end(), g[logits].value.begin(),
                        g[logits].value.end());
    for (size_t i = 0; i < ids.size(); ++i) {
      if (collected[i].empty())
        collected[i] = std::move(g[ids[i]].grad);
      else
        for (size_t j = 0; j < collected[i].size(); ++j)
          collected[i][j] += g[ids[i]].grad[j];
    }
  }
  double norm_squared = 0;
  for (const auto &values : collected)
    for (float v : values) {
      if (!std::isfinite(v))
        throw std::runtime_error("nonfinite gradient");
      norm_squared += double(v) * v;
    }
  float norm = float(std::sqrt(norm_squared)),
        clip = std::min(1.0f, 1.0f / (norm + 1e-6f));
  Json gradients = Json::array();
  if (details)
    for (size_t i = 0; i < parameters.size(); ++i)
      gradients.push_back(
          {{"name", parameters[i].name}, {"values", collected[i]}});
  if (update)
    apply_gradients(collected, lr, wd, t);
  Json report = {{"loss", sum / tokens.size()},
                 {"gradient_norm", norm},
                 {"clip", clip},
                 {"hardware_gpu", kernel.hardware()},
                 {"adapter", kernel.adapter()},
                 {"dispatches", kernel.dispatches},
                 {"gpu_seconds", kernel.gpu_seconds},
                 {"transfer_bytes", kernel.transfer_bytes},
                 {"peak_memory_bytes", kernel.peak_memory_bytes}};
  if (details) {
    report["logits"] = all_logits;
    report["gradients"] = gradients;
    report["tensors"] = tensors();
  }
  return report;
}
} // namespace e0
