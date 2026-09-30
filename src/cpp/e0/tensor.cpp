#include "tensor.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#ifdef XGPU_UWP
#include <winrt/Windows.System.h>
#elif defined(_WIN32)
#include <psapi.h>
#include <windows.h>
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
float probability(const Command &p, const Values &q, const Values &k,
                  uint32_t row, uint32_t key) {
  const auto d = p.cols, t = p.seq, i = row % t, base = row - i;
  float maximum = -std::numeric_limits<float>::infinity(), denominator = 0,
        target = 0;
  for (uint32_t j = 0; j <= i; ++j) {
    float score = 0;
    for (uint32_t c = 0; c < d; ++c)
      score += q[row * d + c] * k[(base + j) * d + c];
    score /= std::sqrt(float(d));
    maximum = std::max(maximum, score);
  }
  for (uint32_t j = 0; j <= i; ++j) {
    float score = 0;
    for (uint32_t c = 0; c < d; ++c)
      score += q[row * d + c] * k[(base + j) * d + c];
    const float term = std::exp(score / std::sqrt(float(d)) - maximum);
    denominator += term;
    if (j == key)
      target = term;
  }
  return target / denominator;
}
} // namespace
Values CpuKernel::run(const Command &p, const Values &x, const Values &w,
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
    case Op::Attention: {
      auto row = n / K, c = n % K, i = row % T, base = row - i;
      if (p.mode == 0) {
        for (uint32_t j = 0; j <= i; ++j)
          v += probability(p, x, w, row, j) * z[(base + j) * K + c];
      } else if (p.mode == 1) {
        for (uint32_t j = 0; j <= i; ++j) {
          float dot = 0;
          for (uint32_t k = 0; k < K; ++k)
            dot += dy[row * K + k] * (z[(base + j) * K + k] - y[row * K + k]);
          v += probability(p, x, w, row, j) * dot * w[(base + j) * K + c] /
               std::sqrt(float(K));
        }
      } else if (p.mode == 2) {
        for (uint32_t r = i; r < T; ++r) {
          auto query = base + r;
          float dot = 0;
          for (uint32_t k = 0; k < K; ++k)
            dot += dy[query * K + k] * (z[row * K + k] - y[query * K + k]);
          v += probability(p, x, w, query, i) * dot * x[query * K + c] /
               std::sqrt(float(K));
        }
      } else {
        for (uint32_t r = i; r < T; ++r)
          v += probability(p, x, w, base + r, i) * dy[(base + r) * K + c];
      }
      break;
    }
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
                 : std::log(sum) + maximum - x[r * K + uint32_t(w[r])];
      break;
    }
    }
    result[n] = v;
  }
  return result;
}
int Graph::leaf(Values v, bool grad) {
  Node node;
  node.value = std::move(v);
  node.needs_grad = grad;
  nodes_.push_back(std::move(node));
  kernel_.observe();
  return int(nodes_.size() - 1);
}
int Graph::identity(int parent, Values v) {
  int i = leaf(std::move(v), nodes_[parent].needs_grad);
  nodes_[i].identity = true;
  nodes_[i].parents = {parent};
  return i;
}
int Graph::apply(Command p, std::vector<int> parents) {
  const Values &a = parents.size() > 0 ? nodes_[parents[0]].value : empty;
  const Values &b = parents.size() > 1 ? nodes_[parents[1]].value : empty;
  const Values &c = parents.size() > 2 ? nodes_[parents[2]].value : empty;
  auto value = kernel_.run(p, a, b, c, empty, empty);
  bool grad = false;
  for (int i : parents)
    grad |= nodes_[i].needs_grad;
  int i = leaf(std::move(value), grad);
  nodes_[i].command = p;
  nodes_[i].parents = std::move(parents);
  return i;
}
void Graph::backward(int root, float seed) {
  nodes_[root].grad.assign(nodes_[root].value.size(), seed);
  for (int i = root; i >= 0; --i) {
    auto &node = nodes_[i];
    if (node.grad.empty())
      continue;
    for (size_t j = 0; j < node.parents.size(); ++j) {
      auto &parent = nodes_[node.parents[j]];
      if (!parent.needs_grad)
        continue;
      Values gradient;
      if (node.identity)
        gradient = node.grad;
      else {
        auto p = node.command;
        p.mode = uint32_t(j + 1);
        p.count = uint32_t(parent.value.size());
        gradient = kernel_.run(
            p, nodes_[node.parents[0]].value,
            node.parents.size() > 1 ? nodes_[node.parents[1]].value : empty,
            node.parents.size() > 2 ? nodes_[node.parents[2]].value : empty,
            node.value, node.grad);
      }
      if (parent.grad.empty())
        parent.grad = std::move(gradient);
      else {
        auto p = Command{Op::Add};
        p.count = uint32_t(gradient.size());
        parent.grad =
            kernel_.run(p, parent.grad, gradient, empty, empty, empty);
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
