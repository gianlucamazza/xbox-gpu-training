#pragma once

#include "hello_dispatch.h"
#include "stream_buffer.h"

#include <cstdint>
#include <filesystem>

struct StreamStressOptions {
  std::filesystem::path fixture;
  std::uint64_t budget_mb = kAppPlanningBudgetMiB;
  std::uint64_t chunk_mb = 0;    // 0 = fixture / default
  std::uint64_t logical_mb = 0;  // 0 = fixture / default
};

// Stream a logical corpus larger than the App planning budget through a
// 2-slot host double buffer. Optional GPU ping-pong copies when D3D12 exists.
// Never invents tok/s or Series S|X numbers. Console AppContainer is unvalidated
// on desktop RAM.
RunReport RunStreamStress(const StreamStressOptions& options);
