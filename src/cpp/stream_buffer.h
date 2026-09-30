#pragma once

// Fase 4: chunk stream + host double buffer + working-set sample.
// English comments only. Never allocates the logical master in one shot.
// No CUDA. DirectML is not the trainer. Not a tok/s result.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

constexpr std::uint64_t kMiB = 1024ull * 1024ull;

// Microsoft Learn UWP Xbox foreground planning budgets (not measured here).
// SoT: docs/platform/uwp-resources.md — App 1 GB; Creators Game 5 GB.
constexpr std::uint64_t kAppPlanningBudgetMiB = 1024;
constexpr std::uint64_t kGamePlanningBudgetMiB = 5120;

struct WorkingSetSample {
  std::uint64_t current_bytes = 0;
  std::uint64_t peak_bytes = 0;
  bool ok = false;
  std::string source;
};

// Linux: /proc/self/status VmRSS + VmHWM. Windows: GetProcessMemoryInfo.
// Failure is honest — do not invent a Series S|X working-set.
bool SampleWorkingSet(WorkingSetSample& out, std::string& err);

struct WorkingSetTracker {
  std::uint64_t peak_bytes = 0;
  std::uint64_t last_bytes = 0;
  bool sampled = false;
  std::string source;
  std::string last_error;
};

void NoteWorkingSet(WorkingSetTracker& tracker);

struct DoubleBuffer {
  std::vector<std::uint8_t> slot[2];
};

// Two host slots only. The logical corpus is generated per chunk.
bool AllocDoubleBuffer(DoubleBuffer& buf, std::size_t chunk_bytes, std::string& err);
void ReleaseDoubleBuffer(DoubleBuffer& buf);

void FillChunk(std::uint8_t* dst, std::size_t bytes, std::uint64_t chunk_index);
std::uint64_t ChecksumChunk(const std::uint8_t* src, std::size_t bytes);

struct StreamPlan {
  std::string name = "stream_stress_app_1gb";
  std::string schema = "xbox-gpu-training.fixture.stream.v1";
  std::string designation = "App";
  std::uint64_t logical_bytes = 2ull * 1024ull * 1024ull * 1024ull;  // 2 GiB logical
  std::size_t chunk_bytes = static_cast<std::size_t>(16ull * kMiB);  // 16 MiB tiles
  std::uint32_t passes = 1;
  std::uint64_t budget_bytes = kAppPlanningBudgetMiB * kMiB;
  std::uint64_t game_budget_bytes = kGamePlanningBudgetMiB * kMiB;
  std::string note;
};

std::uint64_t StreamChunkCount(const StreamPlan& plan);
std::size_t StreamChunkBytes(const StreamPlan& plan, std::uint64_t chunk_index);
double BytesToMiB(std::uint64_t bytes);
