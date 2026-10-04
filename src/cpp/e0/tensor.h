#pragma once
#include "../gpu_wait.h"
#include "constants.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace e0 {
using Values = std::vector<float>;
enum class Op : uint32_t {
  Add,
  Linear,
  Norm,
  Rope,
  Activation,
  Heads,
  Unheads,
  Embed,
  Slice,
  Multiply,
  CrossEntropy,
  Scores,
  Softmax,
  Weighted
};
struct Command {
  Op op;
  uint32_t mode = 0; // 0 forward; 1..3 gradient for that input
  uint32_t count = 0;
  uint32_t rows = 0, cols = 0, out = 0, batch = 0, seq = 0, heads = 0, aux = 0;
  float epsilon = constants::kRmsNormEps;
};
void validate_command(const Command &, const std::array<size_t, 5> &);

// Backend-owned device memory; released to the backend when the last tensor
// referencing it goes away.
struct DeviceBuffer {
  virtual ~DeviceBuffer() = default;
};
// Shared tensor storage. Host data is kept for leaves (uploaded lazily, once);
// GPU results live only on the device until read.
struct Storage {
  size_t count = 0;
  Values host;
  bool on_host = false;
  std::shared_ptr<DeviceBuffer> device;
};
using Tensor = std::shared_ptr<Storage>;
inline size_t size(const Tensor &t) { return t ? t->count : 0; }
struct Kernel {
  virtual ~Kernel() = default;
  Tensor leaf(Values) const;
  // Records one operation; inputs may be null (empty). No host round trip.
  virtual Tensor run(const Command &, const Tensor &, const Tensor &,
                     const Tensor &, const Tensor &, const Tensor &) = 0;
  // Synchronises once for all tensors and returns their values.
  virtual std::vector<Values> read(const std::vector<Tensor> &) = 0;
  Values read(const Tensor &t) { return read(std::vector<Tensor>{t})[0]; }
  // Host-in/host-out convenience used by per-operation fixtures.
  Values run(const Command &, const Values &, const Values &, const Values &,
             const Values &, const Values &);
  virtual bool hardware() const { return false; }
  virtual std::string adapter() const { return "CPU reference"; }
  void observe();
  GpuRuntimeFault runtime_fault;
  bool poisoned() const { return bool(runtime_fault); }
  uint64_t completed_fence = 0;
  std::function<void(const std::string &, uint64_t, uint64_t)>
      progress_callback;
  std::function<void(uint64_t, const std::string &)> gpu_progress;
  // Published before WaitForGpu blocks. requested 0 is never a begin.
  std::function<void(uint64_t, const std::string &)> gpu_wait;
  std::function<void()> gpu_wait_end;
  // Installed only by a worker with an independent process-exit watchdog.
  std::function<void()> published_fence_stall;
  virtual void inject_runtime_fault(const std::string &) {
    throw std::runtime_error("runtime fault probes require the GPU backend");
  }
  void require_healthy() const {
    if (poisoned())
      throw std::runtime_error(runtime_fault.kind + ": " + runtime_fault.error);
  }
  uint64_t dispatches = 0, transfer_bytes = 0, peak_memory_bytes = 0;
  double gpu_seconds = 0;
};
struct CpuKernel final : Kernel {
  using Kernel::read;
  using Kernel::run;
  Tensor run(const Command &, const Tensor &, const Tensor &, const Tensor &,
             const Tensor &, const Tensor &) override;
  std::vector<Values> read(const std::vector<Tensor> &) override;
};
std::unique_ptr<Kernel> make_gpu(const std::string &shader);
struct Node {
  Tensor value, grad;
  Command command{Op::Add};
  std::vector<int> parents;
  bool needs_grad = false;
  bool identity = false;
};
class Graph {
public:
  explicit Graph(Kernel &kernel) : kernel_(kernel) {}
  int leaf(Values value, bool grad = false);
  int leaf(Tensor value, bool grad = false);
  int identity(int parent, Tensor value);
  int apply(Command command, std::vector<int> parents);
  void backward(int root, float seed);
  Node &operator[](int i) { return nodes_.at(i); }
  const Node &operator[](int i) const { return nodes_.at(i); }

private:
  Kernel &kernel_;
  std::vector<Node> nodes_;
};
} // namespace e0
