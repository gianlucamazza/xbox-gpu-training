#include "tensor.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#ifdef XGPU_UWP
#include <winrt/Windows.System.h>
#elif defined(_WIN32)
#include <windows.h>

#include <psapi.h>
#endif

namespace e0 {
void Kernel::observe() {
  uint64_t memory = 0;
#ifdef XGPU_UWP
  memory = winrt::Windows::System::MemoryManager::AppMemoryUsage();
#elif defined(_WIN32)
  PROCESS_MEMORY_COUNTERS counters{};
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
    throw std::runtime_error("memory measurement failed");
  memory = counters.PeakWorkingSetSize;
#else
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line))
    if (line.rfind("VmHWM:", 0) == 0)
      memory = std::stoull(line.substr(6)) * 1024;
#endif
  if (!memory)
    throw std::runtime_error("memory measurement unavailable");
  peak_memory_bytes = std::max(peak_memory_bytes, memory);
  if (peak_memory_bytes > (uint64_t(1) << 30))
    throw std::runtime_error("E0 measured memory exceeds 1 GiB budget");
}
namespace {
const Values empty;
Values compute(const Command &p, const Values &x, const Values &w,
               const Values &z, const Values &y, const Values &dy) {
  Values result(p.count);
  const uint32_t K = p.cols, O = p.out, T = p.seq, H = p.heads,
                 D = H ? K / H : 0;
  for (uint32_t n = 0; n < p.count; ++n) {
    float v = 0;
    switch (p.op) {
    case Op::Add:
      v = p.mode ? dy[n] : x[n] + w[n];
      break;
    case Op::Multiply:
      v = p.mode ? (p.mode == 1 ? dy[n] * w[n] : dy[n] * x[n]) : x[n] * w[n];
      break;
    case Op::Linear:
      if (p.mode == 0) {
        auto r = n / O, o = n % O;
        for (uint32_t k = 0; k < K; ++k)
          v += x[r * K + k] * w[o * K + k];
      } else if (p.mode == 1) {
        auto r = n / K, k = n % K;
        for (uint32_t o = 0; o < O; ++o)
          v += dy[r * O + o] * w[o * K + k];
      } else {
        auto o = n / K, k = n % K;
        for (uint32_t r = 0; r < p.rows; ++r)
          v += dy[r * O + o] * x[r * K + k];
      }
      break;
    case Op::Norm:
      if (p.mode == 2) {
        auto c = n;
        for (uint32_t r = 0; r < p.rows; ++r) {
          float ss = 0;
          for (uint32_t k = 0; k < K; ++k)
            ss += x[r * K + k] * x[r * K + k];
          v += dy[r * K + c] * x[r * K + c] / std::sqrt(ss / K + p.epsilon);
        }
      } else {
        auto r = n / K, c = n % K;
        float ss = 0, dot = 0;
        for (uint32_t k = 0; k < K; ++k) {
          ss += x[r * K + k] * x[r * K + k];
          if (p.mode)
            dot += dy[r * K + k] * w[k] * x[r * K + k];
        }
        float inv = 1 / std::sqrt(ss / K + p.epsilon);
        v = p.mode ? inv * (dy[n] * w[c] - x[n] * dot * inv * inv / K)
                   : x[n] * inv * w[c];
      }
      break;
    case Op::Rope: {
      auto c = n % K, row = n / K, pos = row % T, pair = c - c % 2;
      float angle = float(pos) * std::pow(10000.0f, -float(pair) / K);
      auto other = n + (c % 2 ? -1 : 1);
      v = p.mode ? (dy[n] * std::cos(angle) +
                    (c % 2 ? -dy[other] : dy[other]) * std::sin(angle))
                 : (x[n] * std::cos(angle) +
                    (c % 2 ? x[other] : -x[other]) * std::sin(angle));
      break;
    }
    case Op::Activation: {
      float a = x[n], f = 0, g = 0;
      if (p.aux == 0) {
        const float c = 0.3989422804014327f;
        f = 0.5f * a * (1 + std::erf(a * 0.7071067811865475f));
        g = 0.5f * (1 + std::erf(a * 0.7071067811865475f)) +
            a * c * std::exp(-0.5f * a * a);
      } else if (p.aux == 1) {
        f = std::max(a, 0.0f) * std::max(a, 0.0f);
        g = 2 * std::max(a, 0.0f);
      } else {
        float sig = 1 / (1 + std::exp(-a));
        f = a * sig;
        g = sig + a * sig * (1 - sig);
      }
      v = p.mode ? dy[n] * g : f;
      break;
    }
    case Op::Heads:
      if (p.mode == 0) {
        auto c = n % D, row = n / D, t = row % T, h = (row / T) % H,
             b = row / (T * H);
        v = x[(b * T + t) * 3 * K + p.aux * K + h * D + c];
      } else {
        auto c = n % (3 * K), r = n / (3 * K), b = r / T, t = r % T;
        if (c / K == p.aux) {
          c %= K;
          auto h = c / D;
          v = dy[((b * H + h) * T + t) * D + c % D];
        }
      }
      break;
    case Op::Unheads:
      if (p.mode == 0) {
        auto c = n % K, r = n / K, b = r / T, t = r % T, h = c / D;
        v = x[((b * H + h) * T + t) * D + c % D];
      } else {
        auto c = n % D, row = n / D, t = row % T, h = (row / T) % H,
             b = row / (T * H);
        v = dy[(b * T + t) * K + h * D + c];
      }
      break;
    case Op::Embed:
      if (p.mode == 0)
        v = w[uint32_t(x[n / K]) * K + n % K];
      else if (p.mode == 2) {
        auto token = n / K, c = n % K;
        for (uint32_t r = 0; r < p.rows; ++r)
          if (uint32_t(x[r]) == token)
            v += dy[r * K + c];
      }
      break;
    case Op::Slice:
      if (p.mode == 0) {
        auto r = n / O, c = n % O;
        v = x[r * K + p.aux * O + c];
      } else {
        auto r = n / K, c = n % K;
        if (c / O == p.aux)
          v = dy[r * O + c % O];
      }
      break;
    case Op::Scores: {
      if (p.mode == 0) {
        auto row = n / T, key = n % T, base = row - row % T;
        for (uint32_t c = 0; c < K; ++c)
          v += x[row * K + c] * w[(base + key) * K + c];
      } else if (p.mode == 1) {
        auto row = n / K, c = n % K, base = row - row % T;
        for (uint32_t j = 0; j < T; ++j)
          v += dy[row * T + j] * w[(base + j) * K + c];
      } else {
        auto row = n / K, c = n % K, key = row % T, base = row - key;
        for (uint32_t i = 0; i < T; ++i)
          v += dy[(base + i) * T + key] * x[(base + i) * K + c];
      }
      v /= std::sqrt(float(K));
      break;
    }
    case Op::Softmax: {
      auto row = n / T, key = n % T, limit = row % T;
      if (key > limit)
        break;
      if (p.mode == 0) {
        float maximum = -std::numeric_limits<float>::infinity(), sum = 0;
        for (uint32_t j = 0; j <= limit; ++j)
          maximum = std::max(maximum, x[row * T + j]);
        for (uint32_t j = 0; j <= limit; ++j)
          sum += std::exp(x[row * T + j] - maximum);
        v = std::exp(x[n] - maximum) / sum;
      } else {
        float dot = 0;
        for (uint32_t j = 0; j <= limit; ++j)
          dot += dy[row * T + j] * y[row * T + j];
        v = y[n] * (dy[n] - dot);
      }
      break;
    }
    case Op::Weighted: {
      if (p.mode == 0) {
        auto row = n / K, c = n % K, base = row - row % T;
        for (uint32_t j = 0; j <= row % T; ++j)
          v += x[row * T + j] * w[(base + j) * K + c];
      } else if (p.mode == 1) {
        auto row = n / T, key = n % T, base = row - row % T;
        if (key > row % T)
          break;
        for (uint32_t c = 0; c < K; ++c)
          v += dy[row * K + c] * w[(base + key) * K + c];
      } else {
        auto row = n / K, c = n % K, key = row % T, base = row - key;
        for (uint32_t i = key; i < T; ++i)
          v += x[(base + i) * T + key] * dy[(base + i) * K + c];
      }
      break;
    }
    case Op::CrossEntropy: {
      auto r = p.mode ? n / K : n, c = n % K;
      float maximum = -std::numeric_limits<float>::infinity(), sum = 0;
      for (uint32_t k = 0; k < K; ++k)
        maximum = std::max(maximum, x[r * K + k]);
      for (uint32_t k = 0; k < K; ++k)
        sum += std::exp(x[r * K + k] - maximum);
      v = p.mode ? dy[r] * (std::exp(x[n] - maximum) / sum -
                            (uint32_t(w[r]) == c ? 1 : 0))
                 : std::log(sum) - (x[r * K + uint32_t(w[r])] - maximum);
      break;
    }
    }
    result[n] = v;
  }
  return result;
}
const Values &host(const Tensor &t) {
  if (t && !t->on_host)
    throw std::runtime_error("CPU reference received a device tensor");
  return t ? t->host : empty;
}
} // namespace
Tensor Kernel::leaf(Values v) const {
  auto t = std::make_shared<Storage>();
  t->count = v.size();
  t->host = std::move(v);
  t->on_host = true;
  return t;
}
Values Kernel::run(const Command &p, const Values &x, const Values &w,
                   const Values &z, const Values &y, const Values &dy) {
  auto wrap = [&](const Values &v) { return v.empty() ? Tensor{} : leaf(v); };
  return read(run(p, wrap(x), wrap(w), wrap(z), wrap(y), wrap(dy)));
}
Tensor CpuKernel::run(const Command &p, const Tensor &x, const Tensor &w,
                      const Tensor &z, const Tensor &y, const Tensor &dy) {
  return leaf(compute(p, host(x), host(w), host(z), host(y), host(dy)));
}
std::vector<Values> CpuKernel::read(const std::vector<Tensor> &tensors) {
  std::vector<Values> out;
  for (const auto &t : tensors)
    out.push_back(host(t));
  return out;
}
int Graph::leaf(Values v, bool grad) {
  return leaf(kernel_.leaf(std::move(v)), grad);
}
int Graph::leaf(Tensor v, bool grad) {
  Node node;
  node.value = std::move(v);
  node.needs_grad = grad;
  nodes_.push_back(std::move(node));
  kernel_.observe();
  return int(nodes_.size() - 1);
}
int Graph::identity(int parent, Tensor v) {
  int i = leaf(std::move(v), nodes_[parent].needs_grad);
  nodes_[i].identity = true;
  nodes_[i].parents = {parent};
  return i;
}
int Graph::apply(Command p, std::vector<int> parents) {
  const Tensor none;
  const Tensor &a = parents.size() > 0 ? nodes_[parents[0]].value : none;
  const Tensor &b = parents.size() > 1 ? nodes_[parents[1]].value : none;
  const Tensor &c = parents.size() > 2 ? nodes_[parents[2]].value : none;
  auto value = kernel_.run(p, a, b, c, none, none);
  bool grad = false;
  for (int i : parents)
    grad |= nodes_[i].needs_grad;
  int i = leaf(std::move(value), grad);
  nodes_[i].command = p;
  nodes_[i].parents = std::move(parents);
  return i;
}
void Graph::backward(int root, float seed) {
  const Tensor none;
  nodes_[root].grad = kernel_.leaf(Values(size(nodes_[root].value), seed));
  for (int i = root; i >= 0; --i) {
    auto &node = nodes_[i];
    if (!node.grad)
      continue;
    for (size_t j = 0; j < node.parents.size(); ++j) {
      auto &parent = nodes_[node.parents[j]];
      if (!parent.needs_grad)
        continue;
      Tensor gradient;
      if (node.identity)
        gradient = node.grad;
      else {
        auto p = node.command;
        p.mode = uint32_t(j + 1);
        p.count = uint32_t(size(parent.value));
        gradient = kernel_.run(
            p, nodes_[node.parents[0]].value,
            node.parents.size() > 1 ? nodes_[node.parents[1]].value : none,
            node.parents.size() > 2 ? nodes_[node.parents[2]].value : none,
            node.value, node.grad);
      }
      if (!parent.grad)
        parent.grad = std::move(gradient);
      else {
        auto p = Command{Op::Add};
        p.count = uint32_t(size(gradient));
        parent.grad = kernel_.run(p, parent.grad, gradient, none, none, none);
      }
    }
    kernel_.observe();
  }
}
#ifndef _WIN32
std::unique_ptr<Kernel> make_gpu(const std::string &) {
  throw std::runtime_error(
      "BLOCKED: no D3D12 device; E0 never falls back to CPU");
}
#endif
} // namespace e0
