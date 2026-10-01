// The native E0 constants, tensor layout and capabilities against the pinned
// floppylm contract (contracts/floppylm/schemas/values/floppylm.e0.constants.v1.json).
#include "../constants.h"
#include "../model.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string &what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
  }
}
std::string text(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "cannot read %s\n", path.c_str());
    std::exit(EXIT_FAILURE);
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}
// Shape expressions of the published layout: "1", "vocab", "d", "d_ff", "k*x".
uint32_t evaluate(const std::string &expr, const e0::Config &c) {
  const std::map<std::string, uint32_t> sizes{
      {"vocab", c.vocab}, {"d", c.d}, {"d_ff", c.ff}};
  const auto star = expr.find('*');
  if (star != std::string::npos)
    return uint32_t(std::stoul(expr.substr(0, star))) *
           evaluate(expr.substr(star + 1), c);
  return expr == "1" ? 1 : sizes.at(expr);
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: e0_constants_test <repository root>\n");
    return EXIT_FAILURE;
  }
  const std::string root = argv[1];
  const auto contract = e0::Json::parse(
      text(root + "/contracts/floppylm/schemas/values/floppylm.e0.constants.v1.json"));
  namespace k = e0::constants;
  const auto &codec = contract.at("codec");
  check(k::kFp16Min == codec.at("fp16_min").get<float>(), "fp16_min");
  check(k::kFp16Max == codec.at("fp16_max").get<float>(), "fp16_max");
  check(k::kLogStepsPerOctave == codec.at("log_steps_per_octave").get<int>(),
        "log_steps_per_octave");
  check(k::kMult2bit == codec.at("mult_2bit").get<float>(), "mult_2bit");
  check(k::kMult4bit == codec.at("mult_4bit").get<float>(), "mult_4bit");
  const auto &model = contract.at("model");
  check(k::kRopeTheta == model.at("rope_theta").get<float>(), "rope_theta");
  check(k::kRmsNormEps == model.at("rmsnorm_eps").get<float>(), "rmsnorm_eps");
  check(model.at("gelu_approximate") == "none",
        "only the exact (erf) GELU is implemented");
  const auto &opt = contract.at("optimizer");
  check(k::kAdamBeta1 == opt.at("adamw_betas").at(0).get<double>(), "beta1");
  check(k::kAdamBeta2 == opt.at("adamw_betas").at(1).get<double>(), "beta2");
  check(k::kAdamEps == opt.at("adamw_eps").get<float>(), "adamw_eps");
  check(k::kGradClip == opt.at("grad_clip").get<float>(), "grad_clip");
  check(k::kGradClipEps == opt.at("grad_clip_eps").get<float>(), "grad_clip_eps");
  const auto &schedule = contract.at("schedule");
  check(k::kBranches == schedule.at("branches").get<unsigned>(), "branches");
  check(k::kWarmupFrac == schedule.at("warmup_frac").get<double>(), "warmup_frac");
  check(k::kCooldownFrac == schedule.at("cooldown_frac").get<double>(),
        "cooldown_frac");

  // The shader cannot include constants.h and must keep its bytes (bit-identity
  // rule), so its RoPE literal is checked as text.
  char literal[64];
  std::snprintf(literal, sizeof literal, "pow(%.1ff,", double(k::kRopeTheta));
  check(text(root + "/src/hlsl/e0_tensor.hlsl").find(literal) != std::string::npos,
        std::string("shader RoPE literal ") + literal);

  // Tensor layout of the real Model for every MLP, from the golden fixture config.
  auto fixture = e0::Json::parse(text(
      root + "/contracts/floppylm/fixtures/valid/floppylm.e0.fixture.v1--tiny.json"));
  for (const auto &mlp : model.at("mlps")) {
    auto config = fixture.at("config");
    config["mlp"] = mlp;
    std::vector<e0::Model::Layout> expected;
    const e0::Config c(config);
    for (const auto &entry : model.at("tensor_layout")) {
      const auto rows = entry.at("rows").is_object()
                            ? entry.at("rows").at(mlp.get<std::string>())
                            : entry.at("rows");
      const auto shape = [&](const std::string &name) {
        expected.emplace_back(name, evaluate(rows.get<std::string>(), c),
                              evaluate(entry.at("cols").get<std::string>(), c),
                              entry.at("norm").get<bool>());
      };
      std::string name = entry.at("name");
      if (!entry.at("per_block").get<bool>()) {
        shape(name);
        continue;
      }
      for (uint32_t l = 0; l < c.layers; ++l) {
        auto block = name;
        block.replace(block.find("{i}"), 3, std::to_string(l));
        shape(block);
      }
    }
    check(e0::Model::layout(c) == expected,
          "tensor layout for mlp " + mlp.get<std::string>());
  }

  // Every capability is a value the contract defines.
  const auto caps = e0::capabilities();
  for (const auto &[key, values] :
       {std::pair{"core_fmt", codec.at("formats")},
        std::pair{"emb_fmt", codec.at("formats")},
        std::pair{"mlp", model.at("mlps")},
        std::pair{"scale_policy", codec.at("scale_policies")}})
    for (const auto &v : caps.at(key))
      check(std::find(values.begin(), values.end(), v) != values.end(),
            std::string("capability ") + key + "=" + v.get<std::string>());

  if (failures)
    return EXIT_FAILURE;
  std::puts("e0 constants match floppylm");
  return EXIT_SUCCESS;
}
