#include "stream_stress.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <new>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr const char* kSchema = "xbox-gpu-training.fixture.stream.v1";

std::string ReadAll(const std::filesystem::path& path, std::string& err) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    err = "could not open " + path.u8string();
    return {};
  }
  std::ostringstream os;
  os << in.rdbuf();
  return os.str();
}

bool ExtractQuoted(const std::string& json, const char* key, std::string& out) {
  const std::string pat = std::string("\"") + key + "\"";
  auto pos = json.find(pat);
  if (pos == std::string::npos) {
    return false;
  }
  pos = json.find(':', pos + pat.size());
  if (pos == std::string::npos) {
    return false;
  }
  ++pos;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
    ++pos;
  }
  if (pos >= json.size() || json[pos] != '"') {
    return false;
  }
  ++pos;
  out.clear();
  while (pos < json.size() && json[pos] != '"') {
    if (json[pos] == '\\' && pos + 1 < json.size()) {
      out.push_back(json[pos + 1]);
      pos += 2;
      continue;
    }
    out.push_back(json[pos]);
    ++pos;
  }
  return true;
}

bool ExtractU64(const std::string& json, const char* key, std::uint64_t& out) {
  const std::string pat = std::string("\"") + key + "\"";
  auto pos = json.find(pat);
  if (pos == std::string::npos) {
    return false;
  }
  pos = json.find(':', pos + pat.size());
  if (pos == std::string::npos) {
    return false;
  }
  ++pos;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
    ++pos;
  }
  if (pos >= json.size()) {
    return false;
  }
  try {
    std::size_t taken = 0;
    const unsigned long long v = std::stoull(json.substr(pos), &taken, 10);
    if (taken == 0) {
      return false;
    }
    out = static_cast<std::uint64_t>(v);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

std::filesystem::path ResolveStreamFixture(const std::filesystem::path& hint) {
  std::vector<std::filesystem::path> candidates;
  auto push = [&](std::filesystem::path p) {
    if (p.empty()) {
      return;
    }
    std::error_code ec;
    p = std::filesystem::absolute(p, ec);
    if (ec) {
      return;
    }
    if (std::find(candidates.begin(), candidates.end(), p) == candidates.end()) {
      candidates.push_back(std::move(p));
    }
  };

  push(hint);
  const std::string leaf =
      hint.empty() ? std::string("stream_stress.json") : hint.filename().string();

  std::vector<std::filesystem::path> roots;
  auto add_root = [&](std::filesystem::path p) {
    std::error_code ec;
    p = std::filesystem::absolute(p, ec);
    if (!ec && std::find(roots.begin(), roots.end(), p) == roots.end()) {
      roots.push_back(std::move(p));
    }
  };
  add_root(std::filesystem::current_path());
  std::error_code ec;
  std::filesystem::path walk = std::filesystem::current_path(ec);
  for (int i = 0; i < 6 && !ec; ++i) {
    add_root(walk);
    const auto parent = walk.parent_path();
    if (parent == walk) {
      break;
    }
    walk = parent;
  }

  for (const auto& root : roots) {
    if (!hint.empty()) {
      push(root / hint);
    }
    push(root / "fixtures" / leaf);
    push(root / "benchmarks" / "fixtures" / leaf);
    push(root / leaf);
  }

  for (const auto& path : candidates) {
    std::error_code exists_ec;
    if (std::filesystem::is_regular_file(path, exists_ec) && !exists_ec) {
      return path;
    }
  }
  return {};
}

bool LoadStreamFixture(const std::filesystem::path& path, StreamPlan& plan, std::string& err) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec) || ec) {
    err = path.u8string() + " is not a regular file";
    return false;
  }
  const std::string json = ReadAll(path, err);
  if (json.empty()) {
    if (err.empty()) {
      err = path.u8string() + " is empty";
    }
    return false;
  }
  std::string schema;
  if (!ExtractQuoted(json, "schema", schema) || schema != kSchema) {
    err = "unsupported or missing schema (want " + std::string(kSchema) + ")";
    return false;
  }
  plan.schema = schema;
  ExtractQuoted(json, "name", plan.name);
  ExtractQuoted(json, "designation", plan.designation);
  ExtractQuoted(json, "note", plan.note);
  std::uint64_t v = 0;
  if (ExtractU64(json, "logical_bytes", v)) {
    plan.logical_bytes = v;
  }
  if (ExtractU64(json, "chunk_bytes", v)) {
    plan.chunk_bytes = static_cast<std::size_t>(v);
  }
  if (ExtractU64(json, "passes", v)) {
    if (v < 1 || v > 16) {
      err = "passes must be in 1..16";
      return false;
    }
    plan.passes = static_cast<std::uint32_t>(v);
  }
  if (ExtractU64(json, "planning_budget_mb", v)) {
    plan.budget_bytes = v * kMiB;
  }
  if (ExtractU64(json, "game_designation_budget_mb", v)) {
    plan.game_budget_bytes = v * kMiB;
  }
  if (plan.designation.empty()) {
    plan.designation = "App";
  }
  return true;
}

struct CpuStreamResult {
  bool ok = false;
  bool oom = false;
  std::uint64_t checksum = 0;
  std::uint64_t chunks_seen = 0;
  std::string err;
};

CpuStreamResult RunHostDoubleBuffer(const StreamPlan& plan, WorkingSetTracker& ws) {
  CpuStreamResult result;
  DoubleBuffer buf;
  if (!AllocDoubleBuffer(buf, plan.chunk_bytes, result.err)) {
    result.oom = result.err.find("bad_alloc") != std::string::npos;
    return result;
  }
  NoteWorkingSet(ws);

  const std::uint64_t count = StreamChunkCount(plan);
  std::uint64_t mix = 0;
  try {
    for (std::uint32_t pass = 0; pass < plan.passes; ++pass) {
      for (std::uint64_t chunk = 0; chunk < count; ++chunk) {
        const int slot = static_cast<int>(chunk & 1ull);
        const std::size_t n = StreamChunkBytes(plan, chunk);
        FillChunk(buf.slot[static_cast<std::size_t>(slot)].data(), n, chunk + pass * count);
        const std::uint64_t sum =
            ChecksumChunk(buf.slot[static_cast<std::size_t>(slot)].data(), n);
        mix ^= sum + 0x9E3779B97F4A7C15ull * (chunk + 1);
        ++result.chunks_seen;
        if ((chunk & 7ull) == 0ull) {
          NoteWorkingSet(ws);
        }
      }
    }
  } catch (const std::bad_alloc&) {
    ReleaseDoubleBuffer(buf);
    result.oom = true;
    result.err = "std::bad_alloc while filling a stream chunk";
    return result;
  }

  NoteWorkingSet(ws);
  ReleaseDoubleBuffer(buf);
  NoteWorkingSet(ws);
  result.ok = true;
  result.checksum = mix;
  return result;
}

std::string FmtMiB(std::uint64_t bytes) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(2) << BytesToMiB(bytes);
  return os.str();
}

}  // namespace

#ifndef _WIN32

RunReport RunStreamStress(const StreamStressOptions& options) {
  RunReport report;
  StreamPlan plan;
  std::string fixture_desc = "built-in stream_stress_app_1gb";

  const auto resolved = ResolveStreamFixture(options.fixture);
  if (!resolved.empty()) {
    std::string err;
    if (!LoadStreamFixture(resolved, plan, err)) {
      report.status = RunStatus::Failed;
      report.line = "FAILED: stream-stress fixture";
      report.detail = err;
      return report;
    }
    fixture_desc = resolved.u8string();
  }

  if (options.budget_mb > 0) {
    plan.budget_bytes = options.budget_mb * kMiB;
  }
  if (options.chunk_mb > 0) {
    plan.chunk_bytes = static_cast<std::size_t>(options.chunk_mb * kMiB);
  }
  if (options.logical_mb > 0) {
    plan.logical_bytes = options.logical_mb * kMiB;
  }

  if (plan.chunk_bytes == 0 || plan.logical_bytes == 0) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: stream-stress plan";
    report.detail = "chunk_bytes and logical_bytes must be > 0";
    return report;
  }
  if (plan.chunk_bytes > 512ull * kMiB) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: stream-stress plan";
    report.detail = "chunk_bytes > 512 MiB refused (would fight the App planning budget)";
    return report;
  }

  WorkingSetTracker ws;
  NoteWorkingSet(ws);
  const CpuStreamResult cpu = RunHostDoubleBuffer(plan, ws);

  std::ostringstream detail;
  detail << "Fase 4 memory stream stress\n"
         << "fixture: " << fixture_desc << "\n"
         << "schema: " << plan.schema << "\n"
         << "designation: " << plan.designation
         << " (planning; do not assume Game designation in an App package)\n"
         << "planning_budget_mb: " << (plan.budget_bytes / kMiB) << " (App ~1 GB)\n"
         << "game_designation_budget_mb: " << (plan.game_budget_bytes / kMiB)
         << " (Creators Game ~5 GB; documented; not used)\n"
         << "logical_bytes: " << plan.logical_bytes << " (never allocated as one tensor)\n"
         << "chunk_bytes: " << plan.chunk_bytes << "\n"
         << "double_buffer_slots: 2\n"
         << "passes: " << plan.passes << "\n"
         << "chunks_per_pass: " << StreamChunkCount(plan) << "\n"
         << "chunks_seen: " << cpu.chunks_seen << "\n";

  if (cpu.oom) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: stream-stress OOM";
    detail << "oom: " << cpu.err << "\n";
    if (ws.sampled) {
      detail << "peak_working_set_mb: " << FmtMiB(ws.peak_bytes) << "\n"
             << "peak_working_set_bytes: " << ws.peak_bytes << "\n"
             << "working_set_source: " << ws.source << "\n";
    }
    detail << "console_appcontainer: UNVALIDATED (desktop RAM only; debugger can mask OOM; "
              "non-debug package is the gate)\n"
           << "gpu_double_buffer: BLOCKED: no D3D12 device\n"
           << "Not a tok/s result. Not a Series S|X number. xllama not modified.";
    report.detail = detail.str();
    return report;
  }
  if (!cpu.ok) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: stream-stress";
    report.detail = cpu.err;
    return report;
  }

  const bool have_ws = ws.sampled;
  const bool under = have_ws && ws.peak_bytes <= plan.budget_bytes;
  const bool breach = have_ws && ws.peak_bytes > plan.budget_bytes;

  detail << "checksum: 0x" << std::hex << cpu.checksum << std::dec << "\n";
  if (have_ws) {
    detail << "peak_working_set_mb: " << FmtMiB(ws.peak_bytes) << "\n"
           << "peak_working_set_bytes: " << ws.peak_bytes << "\n"
           << "last_working_set_mb: " << FmtMiB(ws.last_bytes) << "\n"
           << "working_set_source: " << ws.source << "\n"
           << "under_app_planning_budget: " << (under ? "yes" : "NO (measured breach)") << "\n";
  } else {
    detail << "peak_working_set_mb: unavailable (" << ws.last_error << ")\n"
           << "under_app_planning_budget: unknown\n";
  }
  detail << "console_appcontainer: UNVALIDATED (desktop RAM only; debugger can mask OOM; "
            "non-debug package is the gate)\n"
         << "gpu_double_buffer: BLOCKED: no D3D12 device. Dispatch log not invented.\n"
         << "Not a tok/s result. Not a Series S|X number. xllama not modified.";

  if (breach) {
    report.status = RunStatus::Ok;
    report.line = "STATUS: stream-stress budget breach";
  } else {
    report.status = RunStatus::Ok;
    report.line = "STATUS: stream-stress ok";
  }
  report.detail = detail.str();
  return report;
}

#else

#include "dx12_device.h"

#include <windows.h>

namespace {

Microsoft::WRL::ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* device, UINT64 bytes,
                                                    D3D12_HEAP_TYPE heap, D3D12_RESOURCE_FLAGS flags,
                                                    D3D12_RESOURCE_STATES state, HRESULT& hr) {
  D3D12_HEAP_PROPERTIES props{};
  props.Type = heap;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = bytes;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = flags;
  Microsoft::WRL::ComPtr<ID3D12Resource> resource;
  hr = device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                       IID_PPV_ARGS(&resource));
  return resource;
}

struct GpuStreamResult {
  bool attempted = false;
  bool ok = false;
  bool blocked = false;
  bool failed = false;
  std::string message;
  std::string adapter;
  bool warp = false;
  std::uint64_t copies = 0;
};

GpuStreamResult RunGpuDoubleBuffer(const StreamPlan& plan, DoubleBuffer& host,
                                   WorkingSetTracker& ws) {
  GpuStreamResult gpu;
  gpu.attempted = true;

  Dx12CreateResult created = CreateDx12Device();
  if (!created.ok) {
    gpu.blocked = true;
    gpu.message = "BLOCKED: no D3D12 device (" + created.message + ")";
    return gpu;
  }

  Dx12Device& ctx = created.ctx;
  gpu.adapter = ctx.adapter_name;
  gpu.warp = ctx.warp;

  const UINT64 chunk = static_cast<UINT64>(plan.chunk_bytes);
  HRESULT hr = S_OK;
  Microsoft::WRL::ComPtr<ID3D12Resource> upload[2];
  Microsoft::WRL::ComPtr<ID3D12Resource> def[2];
  for (int i = 0; i < 2; ++i) {
    upload[i] = CreateBuffer(ctx.device.Get(), chunk, D3D12_HEAP_TYPE_UPLOAD,
                             D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, hr);
    if (FAILED(hr)) {
      gpu.failed = true;
      gpu.message = "CreateCommittedResource UPLOAD[" + std::to_string(i) + "] failed (" +
                    HrHex(hr) + ")";
      DestroyDx12Device(ctx);
      return gpu;
    }
    def[i] = CreateBuffer(ctx.device.Get(), chunk, D3D12_HEAP_TYPE_DEFAULT,
                          D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, hr);
    if (FAILED(hr)) {
      gpu.failed = true;
      gpu.message = "CreateCommittedResource DEFAULT[" + std::to_string(i) + "] failed (" +
                    HrHex(hr) + ")";
      DestroyDx12Device(ctx);
      return gpu;
    }
  }
  NoteWorkingSet(ws);

  const std::uint64_t count = StreamChunkCount(plan);
  // Double-buffer copies: fill slot, CopyBufferRegion, overlap the next host
  // fill with the in-flight copy, WaitForGpu before reusing a slot.
  auto copy_slot = [&](int slot, std::size_t bytes, std::string& err) -> bool {
    void* mapped = nullptr;
    hr = upload[slot]->Map(0, nullptr, &mapped);
    if (FAILED(hr) || !mapped) {
      err = "UPLOAD map failed (" + HrHex(hr) + ")";
      return false;
    }
    if (bytes > 0) {
      std::memcpy(mapped, host.slot[static_cast<std::size_t>(slot)].data(), bytes);
    }
    upload[slot]->Unmap(0, nullptr);

    hr = ctx.allocator->Reset();
    if (FAILED(hr)) {
      err = "allocator Reset failed (" + HrHex(hr) + ")";
      return false;
    }
    hr = ctx.list->Reset(ctx.allocator.Get(), nullptr);
    if (FAILED(hr)) {
      err = "list Reset failed (" + HrHex(hr) + ")";
      return false;
    }
    ctx.list->CopyBufferRegion(def[slot].Get(), 0, upload[slot].Get(), 0, bytes);
    hr = ctx.list->Close();
    if (FAILED(hr)) {
      err = "list Close failed (" + HrHex(hr) + ")";
      return false;
    }
    ID3D12CommandList* lists[] = {ctx.list.Get()};
    ctx.queue->ExecuteCommandLists(1, lists);
    return true;
  };

  std::string err;
  bool inflight = false;
  for (std::uint32_t pass = 0; pass < plan.passes; ++pass) {
    for (std::uint64_t chunk_i = 0; chunk_i < count; ++chunk_i) {
      const int slot = static_cast<int>(chunk_i & 1ull);
      const std::size_t n = StreamChunkBytes(plan, chunk_i);
      FillChunk(host.slot[static_cast<std::size_t>(slot)].data(), n, chunk_i + pass * count);
      if (inflight) {
        if (!WaitForGpu(ctx, err)) {
          gpu.failed = true;
          gpu.message = err;
          DestroyDx12Device(ctx);
          return gpu;
        }
        inflight = false;
      }
      if (!copy_slot(slot, n, err)) {
        gpu.failed = true;
        gpu.message = err;
        DestroyDx12Device(ctx);
        return gpu;
      }
      inflight = true;
      ++gpu.copies;
    }
  }
  if (inflight && !WaitForGpu(ctx, err)) {
    gpu.failed = true;
    gpu.message = err;
    DestroyDx12Device(ctx);
    return gpu;
  }

  NoteWorkingSet(ws);
  DestroyDx12Device(ctx);
  gpu.ok = true;
  gpu.message = gpu.warp ? "WARP software adapter (not Series S|X)" : "hardware adapter";
  return gpu;
}

}  // namespace

RunReport RunStreamStress(const StreamStressOptions& options) {
  RunReport report;
  StreamPlan plan;
  std::string fixture_desc = "built-in stream_stress_app_1gb";

  const auto resolved = ResolveStreamFixture(options.fixture);
  if (!resolved.empty()) {
    std::string err;
    if (!LoadStreamFixture(resolved, plan, err)) {
      report.status = RunStatus::Failed;
      report.line = "FAILED: stream-stress fixture";
      report.detail = err;
      return report;
    }
    fixture_desc = resolved.u8string();
  }

  if (options.budget_mb > 0) {
    plan.budget_bytes = options.budget_mb * kMiB;
  }
  if (options.chunk_mb > 0) {
    plan.chunk_bytes = static_cast<std::size_t>(options.chunk_mb * kMiB);
  }
  if (options.logical_mb > 0) {
    plan.logical_bytes = options.logical_mb * kMiB;
  }

  if (plan.chunk_bytes == 0 || plan.logical_bytes == 0) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: stream-stress plan";
    report.detail = "chunk_bytes and logical_bytes must be > 0";
    return report;
  }
  if (plan.chunk_bytes > 512ull * kMiB) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: stream-stress plan";
    report.detail = "chunk_bytes > 512 MiB refused (would fight the App planning budget)";
    return report;
  }

  WorkingSetTracker ws;
  NoteWorkingSet(ws);
  const CpuStreamResult cpu = RunHostDoubleBuffer(plan, ws);

  std::ostringstream detail;
  detail << "Fase 4 memory stream stress\n"
         << "fixture: " << fixture_desc << "\n"
         << "schema: " << plan.schema << "\n"
         << "designation: " << plan.designation
         << " (planning; do not assume Game designation in an App package)\n"
         << "planning_budget_mb: " << (plan.budget_bytes / kMiB) << " (App ~1 GB)\n"
         << "game_designation_budget_mb: " << (plan.game_budget_bytes / kMiB)
         << " (Creators Game ~5 GB; documented; not used)\n"
         << "logical_bytes: " << plan.logical_bytes << " (never allocated as one tensor)\n"
         << "chunk_bytes: " << plan.chunk_bytes << "\n"
         << "double_buffer_slots: 2\n"
         << "passes: " << plan.passes << "\n"
         << "chunks_per_pass: " << StreamChunkCount(plan) << "\n"
         << "chunks_seen: " << cpu.chunks_seen << "\n";

  if (cpu.oom) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: stream-stress OOM";
    detail << "oom: " << cpu.err << "\n";
    if (ws.sampled) {
      detail << "peak_working_set_mb: " << FmtMiB(ws.peak_bytes) << "\n"
             << "peak_working_set_bytes: " << ws.peak_bytes << "\n"
             << "working_set_source: " << ws.source << "\n";
    }
    detail << "console_appcontainer: UNVALIDATED (desktop RAM only; debugger can mask OOM; "
              "non-debug package is the gate)\n"
           << "Not a tok/s result. Not a Series S|X number. xllama not modified.";
    report.detail = detail.str();
    return report;
  }
  if (!cpu.ok) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: stream-stress";
    report.detail = cpu.err;
    return report;
  }

  DoubleBuffer gpu_host;
  std::string gpu_alloc_err;
  GpuStreamResult gpu;
  if (!AllocDoubleBuffer(gpu_host, plan.chunk_bytes, gpu_alloc_err)) {
    gpu.failed = true;
    gpu.message = gpu_alloc_err;
  } else {
    gpu = RunGpuDoubleBuffer(plan, gpu_host, ws);
    ReleaseDoubleBuffer(gpu_host);
  }
  NoteWorkingSet(ws);

  const bool have_ws = ws.sampled;
  const bool under = have_ws && ws.peak_bytes <= plan.budget_bytes;
  const bool breach = have_ws && ws.peak_bytes > plan.budget_bytes;

  detail << "checksum: 0x" << std::hex << cpu.checksum << std::dec << "\n";
  if (have_ws) {
    detail << "peak_working_set_mb: " << FmtMiB(ws.peak_bytes) << "\n"
           << "peak_working_set_bytes: " << ws.peak_bytes << "\n"
           << "last_working_set_mb: " << FmtMiB(ws.last_bytes) << "\n"
           << "working_set_source: " << ws.source << "\n"
           << "under_app_planning_budget: " << (under ? "yes" : "NO (measured breach)") << "\n";
  } else {
    detail << "peak_working_set_mb: unavailable (" << ws.last_error << ")\n"
           << "under_app_planning_budget: unknown\n";
  }
  detail << "console_appcontainer: UNVALIDATED (desktop RAM only; debugger can mask OOM; "
            "non-debug package is the gate)\n";
  if (gpu.ok) {
    detail << "gpu_double_buffer: copied " << gpu.copies << " chunks on " << gpu.adapter
           << (gpu.warp ? " (WARP software adapter)" : "") << " — 2 UPLOAD + 2 DEFAULT tiles\n"
           << "note: WARP/desktop D3D12 is not Xbox Series S|X. No new HLSL kernel.\n";
  } else if (gpu.blocked) {
    detail << "gpu_double_buffer: " << gpu.message << ". Dispatch log not invented.\n";
  } else if (gpu.failed) {
    detail << "gpu_double_buffer: FAILED " << gpu.message << "\n";
  } else {
    detail << "gpu_double_buffer: not attempted\n";
  }
  detail << "Not a tok/s result. Not a Series S|X number. xllama not modified.";

  if (gpu.failed) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: stream-stress gpu double-buffer";
  } else if (breach) {
    report.status = RunStatus::Ok;
    report.line = "STATUS: stream-stress budget breach";
  } else {
    report.status = RunStatus::Ok;
    report.line = gpu.ok ? "STATUS: stream-stress dispatched" : "STATUS: stream-stress ok";
  }
  report.detail = detail.str();
  return report;
}

#endif
