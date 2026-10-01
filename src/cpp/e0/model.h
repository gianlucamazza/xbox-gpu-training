#pragma once
#include "tensor.h"
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>

namespace e0 {
using Json = nlohmann::json;
inline std::string path_text(const std::filesystem::path &path) {
  auto text = path.u8string();
  return {text.begin(), text.end()};
}
struct Config {
  uint32_t d = 0, layers = 0, heads = 0, ff = 0, vocab = 256, ctx = 0;
  std::string mlp, format, policy;
  float delta = 0.5f;
  bool qk_norm = false;
  explicit Config(const Json &);
};
struct Parameter {
  std::string name;
  uint32_t rows = 0, cols = 0;
  bool norm = false;
  Values value, first, second;
};
struct Quantized {
  Values value, scales;
  std::vector<uint32_t> symbols;
};
float stored_fp16(float value);
Quantized quantize(const Values &, uint32_t rows, uint32_t cols,
                   const std::string &fmt, const std::string &policy,
                   float delta);
class Model {
public:
  Config config;
  std::vector<Parameter> parameters;
  explicit Model(const Json &job);
  Json tensors() const;
  Json optimizer_state() const;
  Json checkpoint(uint64_t step, const Json &job) const;
  void restore(const Json &state, const Json &job, uint64_t &step);
  float apply_gradients(const std::vector<Values> &, float lr, float wd,
                        uint64_t step);
  Json step(Kernel &, const Values &tokens, const Values &targets,
            uint32_t batch, float lr, float wd, uint64_t optimizer_step,
            bool update = true, bool details = false);

private:
  std::vector<Tensor> effective_weights(Kernel &) const;
  int forward(Graph &, const std::vector<Tensor> &effective, const Values &,
              uint32_t batch, std::vector<int> &master_ids);
};
std::string sha256_file(const std::filesystem::path &);
void atomic_json(const std::filesystem::path &, const Json &);
Json read_json(const std::filesystem::path &);
Json fixture_report(const Json &, Kernel &);
Json optimizer_fixture_report(const Json &);
Json kernel_fixture_report(const Json &, Kernel &);
Json run_job(const std::filesystem::path &, Kernel &, uint64_t stop_after = 0);
} // namespace e0
