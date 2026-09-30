#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace e0 {
using Values = std::vector<float>;
enum class Op : uint32_t { Add, Linear, Norm, Rope, Activation, Heads, Unheads, Embed, Attention, Slice, Multiply, CrossEntropy, Scores, Softmax, Weighted };
struct Command {
  Op op;
  uint32_t mode = 0; // 0 forward; 1..3 gradient for that input
  uint32_t count = 0;
  uint32_t rows = 0, cols = 0, out = 0, batch = 0, seq = 0, heads = 0, aux = 0;
  float epsilon = 1.1920928955078125e-7f;
};
struct Kernel {
  virtual ~Kernel() = default;
  virtual Values run(const Command&, const Values&, const Values&, const Values&, const Values&, const Values&) = 0;
  virtual bool hardware() const { return false; }
  virtual std::string adapter() const { return "CPU reference"; }
  uint64_t dispatches = 0;
};
struct CpuKernel final : Kernel {
  Values run(const Command&, const Values&, const Values&, const Values&, const Values&, const Values&) override;
};
std::unique_ptr<Kernel> make_gpu(const std::string& shader);
struct Node {
  Values value, grad;
  Command command{Op::Add};
  std::vector<int> parents;
  bool needs_grad = false;
  bool identity = false;
};
class Graph {
public:
  explicit Graph(Kernel& kernel) : kernel_(kernel) {}
  int leaf(Values value, bool grad = false);
  int identity(int parent, Values value);
  int apply(Command command, std::vector<int> parents);
  void backward(int root, float seed);
  Node& operator[](int i) { return nodes_.at(i); }
  const Node& operator[](int i) const { return nodes_.at(i); }
private:
  Kernel& kernel_;
  std::vector<Node> nodes_;
};
}
