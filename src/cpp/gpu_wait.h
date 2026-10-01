#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>

// Portable synchronization policy; Win32 is only an adapter to this facade.
struct GpuRuntimeFault {
  std::string kind, error;
  uint64_t requested_fence = 0, completed_fence = 0, elapsed_ms = 0;
  explicit operator bool() const { return !kind.empty(); }
};
struct GpuWaitApi {
  std::function<uint64_t()> now_ms, completed;
  // Empty strings mean success. All errors retain their native HRESULT/code.
  std::function<std::string()> device_error;
  std::function<std::string(uint64_t)> signal, arm;
  enum class Result { Wake, Timeout, Failed, Unexpected };
  std::function<Result(uint32_t)> wait;
  std::function<std::string()> wait_error;
};
inline bool BoundedGpuWait(GpuWaitApi &api, uint64_t requested,
                           GpuRuntimeFault &fault,
                           uint64_t deadline_ms = 600000) {
  if (fault)
    return false; // Quarantine is permanent for this device lifetime.
  const auto begin = api.now_ms();
  uint64_t completed = 0;
  auto fail = [&](const std::string &kind, const std::string &error) {
    fault = {kind, error, requested, completed, api.now_ms() - begin};
    return false;
  };
  auto healthy = [&]() {
    const auto error = api.device_error();
    if (!error.empty())
      return fail("gpu_device_removed", error);
    completed = api.completed();
    if (completed == std::numeric_limits<uint64_t>::max())
      return fail("gpu_device_removed", "fence reported UINT64_MAX");
    return true;
  };
  if (!healthy())
    return false;
  auto error = api.signal(requested);
  if (!error.empty())
    return fail("gpu_fence_error", error);
  if (!healthy())
    return false;
  if (completed >= requested)
    return true;
  error = api.arm(requested);
  if (!error.empty())
    return fail("gpu_fence_error", error);
  for (;;) {
    const auto elapsed = api.now_ms() - begin;
    if (elapsed >= deadline_ms)
      return fail("gpu_wait_timeout", "GPU fence deadline exceeded");
    const auto result = api.wait(
        static_cast<uint32_t>(std::min<uint64_t>(250, deadline_ms - elapsed)));
    if (result == GpuWaitApi::Result::Failed)
      return fail("gpu_wait_failed", api.wait_error());
    if (result == GpuWaitApi::Result::Unexpected)
      return fail("gpu_wait_failed",
                  "unexpected wait result: " + api.wait_error());
    if (!healthy())
      return false;
    if (completed >= requested)
      return true;
  }
}
