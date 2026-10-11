#pragma once
#include "../gpu_wait.h"
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <system_error>

// LocalState/lifecycle.json overrides. Missing keys keep documented defaults.
// Fractional seconds keep millisecond precision (0.5 → 500 ms). Poll is
// clamped so a sub-millisecond value cannot busy-loop the ready wait.
inline constexpr uint64_t kMinPollIntervalMs = 50;

inline HostIdleConfig load_host_idle_config(const nlohmann::json &j) {
  HostIdleConfig config;
  if (!j.is_object()) {
    if (config.poll_interval_ms < kMinPollIntervalMs)
      config.poll_interval_ms = kMinPollIntervalMs;
    return config;
  }
  auto seconds = [&](const char *key, uint64_t fallback_ms) -> uint64_t {
    if (!j.contains(key) || !j.at(key).is_number())
      return fallback_ms;
    const auto value = j.at(key).get<double>();
    if (value < 0)
      return fallback_ms;
    return static_cast<uint64_t>(std::llround(value * 1000.0));
  };
  config.idle_exit_ms = seconds("idle_exit_seconds", config.idle_exit_ms);
  config.heartbeat_stale_ms =
      seconds("heartbeat_stale_seconds", config.heartbeat_stale_ms);
  config.poll_interval_ms =
      seconds("poll_interval_seconds", config.poll_interval_ms);
  if (config.poll_interval_ms < kMinPollIntervalMs)
    config.poll_interval_ms = kMinPollIntervalMs;
  return config;
}

// LocalState/instance.json. Not part of floppylm.worker.v1 (additionalProperties
// is false on that schema). The host tool uses this for instance start time
// and identity. started_at is UTC ISO-8601.
inline nlohmann::json instance_document(std::string instance_id,
                                        std::string started_at, uint64_t pid) {
  return {{"schema", "xgpu.instance.v1"},
          {"instance_id", std::move(instance_id)},
          {"started_at", std::move(started_at)},
          {"pid", pid}};
}

// Consume LocalState/inbox/shutdown once the worker has decided to exit on
// it. Leaving the marker would make the next launch quit immediately.
inline bool consume_shutdown_marker(const std::filesystem::path &path) {
  std::error_code exists_error;
  if (!std::filesystem::exists(path, exists_error))
    return false;
  std::error_code remove_error;
  std::filesystem::remove(path, remove_error);
  return !remove_error;
}
