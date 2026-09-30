#include "stream_buffer.h"

#include <cstdio>
#include <fstream>
#include <new>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#endif

bool SampleWorkingSet(WorkingSetSample& out, std::string& err) {
  out = WorkingSetSample{};

#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS_EX pmc{};
  pmc.cb = sizeof(pmc);
  if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                            sizeof(pmc))) {
    err = "GetProcessMemoryInfo failed";
    return false;
  }
  out.ok = true;
  out.current_bytes = static_cast<std::uint64_t>(pmc.WorkingSetSize);
  out.peak_bytes = static_cast<std::uint64_t>(pmc.PeakWorkingSetSize);
  out.source = "GetProcessMemoryInfo WorkingSetSize/PeakWorkingSetSize";
  return true;
#else
  std::ifstream in("/proc/self/status");
  if (!in) {
    err = "could not open /proc/self/status";
    return false;
  }
  std::uint64_t rss_kb = 0;
  std::uint64_t hwm_kb = 0;
  bool have_rss = false;
  bool have_hwm = false;
  std::string line;
  while (std::getline(in, line)) {
    unsigned long kb = 0;
    if (!have_rss && std::sscanf(line.c_str(), "VmRSS: %lu", &kb) == 1) {
      rss_kb = kb;
      have_rss = true;
    } else if (!have_hwm && std::sscanf(line.c_str(), "VmHWM: %lu", &kb) == 1) {
      hwm_kb = kb;
      have_hwm = true;
    }
  }
  if (!have_rss) {
    err = "VmRSS missing from /proc/self/status";
    return false;
  }
  out.ok = true;
  out.current_bytes = rss_kb * 1024ull;
  out.peak_bytes = (have_hwm ? hwm_kb : rss_kb) * 1024ull;
  out.source = have_hwm ? "/proc/self/status VmRSS+VmHWM" : "/proc/self/status VmRSS";
  return true;
#endif
}

void NoteWorkingSet(WorkingSetTracker& tracker) {
  WorkingSetSample sample;
  std::string err;
  if (!SampleWorkingSet(sample, err)) {
    tracker.last_error = err;
    return;
  }
  tracker.sampled = true;
  tracker.last_bytes = sample.current_bytes;
  tracker.source = sample.source;
  if (sample.current_bytes > tracker.peak_bytes) {
    tracker.peak_bytes = sample.current_bytes;
  }
  if (sample.peak_bytes > tracker.peak_bytes) {
    tracker.peak_bytes = sample.peak_bytes;
  }
}

bool AllocDoubleBuffer(DoubleBuffer& buf, std::size_t chunk_bytes, std::string& err) {
  ReleaseDoubleBuffer(buf);
  if (chunk_bytes == 0) {
    err = "chunk_bytes must be > 0";
    return false;
  }
  try {
    buf.slot[0].assign(chunk_bytes, 0);
    buf.slot[1].assign(chunk_bytes, 0);
  } catch (const std::bad_alloc&) {
    ReleaseDoubleBuffer(buf);
    err = "double-buffer alloc failed (std::bad_alloc) for 2 x " + std::to_string(chunk_bytes) +
          " bytes";
    return false;
  }
  return true;
}

void ReleaseDoubleBuffer(DoubleBuffer& buf) {
  std::vector<std::uint8_t>().swap(buf.slot[0]);
  std::vector<std::uint8_t>().swap(buf.slot[1]);
}

void FillChunk(std::uint8_t* dst, std::size_t bytes, std::uint64_t chunk_index) {
  if (!dst || bytes == 0) {
    return;
  }
  const std::uint32_t seed =
      static_cast<std::uint32_t>(chunk_index * 0x9E3779B9ull + 1ull);
  std::size_t i = 0;
  while (i + 4 <= bytes) {
    const std::uint32_t word = seed + static_cast<std::uint32_t>(i) * 0x85EBCA6Bu;
    dst[i + 0] = static_cast<std::uint8_t>(word);
    dst[i + 1] = static_cast<std::uint8_t>(word >> 8);
    dst[i + 2] = static_cast<std::uint8_t>(word >> 16);
    dst[i + 3] = static_cast<std::uint8_t>(word >> 24);
    i += 4;
  }
  for (; i < bytes; ++i) {
    dst[i] = static_cast<std::uint8_t>(seed + static_cast<std::uint32_t>(i));
  }
}

std::uint64_t ChecksumChunk(const std::uint8_t* src, std::size_t bytes) {
  std::uint64_t h = 1469598103934665603ull;
  if (!src) {
    return h;
  }
  std::size_t i = 0;
  while (i + 4 <= bytes) {
    const std::uint32_t word = static_cast<std::uint32_t>(src[i]) |
                               (static_cast<std::uint32_t>(src[i + 1]) << 8) |
                               (static_cast<std::uint32_t>(src[i + 2]) << 16) |
                               (static_cast<std::uint32_t>(src[i + 3]) << 24);
    h ^= word;
    h *= 1099511628211ull;
    i += 4;
  }
  for (; i < bytes; ++i) {
    h ^= src[i];
    h *= 1099511628211ull;
  }
  return h;
}

std::uint64_t StreamChunkCount(const StreamPlan& plan) {
  if (plan.chunk_bytes == 0 || plan.logical_bytes == 0) {
    return 0;
  }
  return (plan.logical_bytes + static_cast<std::uint64_t>(plan.chunk_bytes) - 1ull) /
         static_cast<std::uint64_t>(plan.chunk_bytes);
}

std::size_t StreamChunkBytes(const StreamPlan& plan, std::uint64_t chunk_index) {
  const std::uint64_t count = StreamChunkCount(plan);
  if (chunk_index >= count) {
    return 0;
  }
  const std::uint64_t offset = chunk_index * static_cast<std::uint64_t>(plan.chunk_bytes);
  if (offset >= plan.logical_bytes) {
    return 0;
  }
  const std::uint64_t remain = plan.logical_bytes - offset;
  if (remain >= static_cast<std::uint64_t>(plan.chunk_bytes)) {
    return plan.chunk_bytes;
  }
  return static_cast<std::size_t>(remain);
}

double BytesToMiB(std::uint64_t bytes) {
  return static_cast<double>(bytes) / static_cast<double>(kMiB);
}
