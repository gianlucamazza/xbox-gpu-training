#pragma once
#include "../gpu_wait.h"
#include <nlohmann/json.hpp>

// LocalState/lifecycle.json overrides. Missing keys keep documented defaults.
inline HostIdleConfig load_host_idle_config(const nlohmann::json &j) {
  HostIdleConfig config;
  if (!j.is_object())
    return config;
  auto seconds = [&](const char *key, uint64_t fallback_ms) {
    if (!j.contains(key) || !j.at(key).is_number())
      return fallback_ms;
    const auto value = j.at(key).get<double>();
    if (value < 0)
      return fallback_ms;
    return uint64_t(value) * 1000ull;
  };
  config.idle_exit_ms = seconds("idle_exit_seconds", config.idle_exit_ms);
  config.heartbeat_stale_ms =
      seconds("heartbeat_stale_seconds", config.heartbeat_stale_ms);
  config.poll_interval_ms =
      seconds("poll_interval_seconds", config.poll_interval_ms);
  return config;
}
