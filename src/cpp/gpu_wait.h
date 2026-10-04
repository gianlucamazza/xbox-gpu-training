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

// Published-fence deadline. This does not call into D3D. The GPU thread can
// block inside GetCompletedValue or ExecuteCommandLists and never return to
// BoundedGpuWait; the heartbeat thread samples the fence it already published.
struct PublishedFenceSample {
  bool job_active = false;
  uint64_t completed_fence = 0;
  uint64_t now_ms = 0;
  // Appended so existing positional samples stay "no GPU wait in flight".
  bool gpu_wait_in_flight = false;
  uint64_t requested_fence = 0;
};
struct PublishedFenceWatch {
  bool tracking = false;
  uint64_t fence = 0;
  uint64_t since_ms = 0;
  bool in_flight = false;
  uint64_t requested = 0;
  bool observe(const PublishedFenceSample &sample, uint64_t deadline_ms,
               GpuRuntimeFault &fault) {
    if (!sample.job_active) {
      tracking = false;
      return false;
    }
    const bool waiting =
        sample.gpu_wait_in_flight && sample.requested_fence > sample.completed_fence;
    if (!tracking || sample.completed_fence != fence || waiting != in_flight ||
        sample.requested_fence != requested || sample.now_ms < since_ms) {
      tracking = true;
      fence = sample.completed_fence;
      in_flight = waiting;
      requested = sample.requested_fence;
      since_ms = sample.now_ms;
      return false;
    }
    const auto elapsed = sample.now_ms - since_ms;
    if (elapsed < deadline_ms)
      return false;
    // A frozen published fence is not a GPU wait unless this sample entered one.
    if (waiting)
      fault = {"gpu_wait_timeout", "GPU fence deadline exceeded",
               sample.requested_fence, fence, elapsed};
    else
      fault = {"progress_stall",
               "published progress frozen without an in-flight GPU request", 0,
               fence, elapsed};
    return true;
  }
};
