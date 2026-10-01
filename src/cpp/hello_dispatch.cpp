#include "hello_dispatch.h"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <vector>

#ifndef _WIN32

RunReport RunHelloCompute(const std::filesystem::path&) {
  RunReport report;
  report.status = RunStatus::Blocked;
  report.line = "BLOCKED: no D3D12 device";
  report.detail =
      "This process is not running on Windows. DirectX 12 device create is unavailable. "
      "Not a GPU result. Not a console result.";
  return report;
}

#else

#include "dx12_device.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

namespace {

constexpr UINT kThreadCount = 64;
constexpr UINT kBufferBytes = kThreadCount * sizeof(UINT);

std::filesystem::path ExeDir() {
  wchar_t buf[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) {
    return std::filesystem::current_path();
  }
  return std::filesystem::path(buf).parent_path();
}

void CollectSearchRoots(std::vector<std::filesystem::path>& roots) {
  auto add = [&](std::filesystem::path p) {
    std::error_code ec;
    p = std::filesystem::absolute(p, ec);
    if (ec) {
      return;
    }
    if (std::find(roots.begin(), roots.end(), p) == roots.end()) {
      roots.push_back(std::move(p));
    }
  };

  add(ExeDir());
  add(std::filesystem::current_path());
  std::error_code ec;
  std::filesystem::path walk = ExeDir();
  for (int i = 0; i < 6; ++i) {
    add(walk);
    const auto parent = walk.parent_path();
    if (parent == walk) {
      break;
    }
    walk = parent;
  }
  walk = std::filesystem::current_path(ec);
  for (int i = 0; i < 6 && !ec; ++i) {
    add(walk);
    const auto parent = walk.parent_path();
    if (parent == walk) {
      break;
    }
    walk = parent;
  }
}

std::filesystem::path FindDxc() {
  wchar_t buf[MAX_PATH];
  const DWORD n = SearchPathW(nullptr, L"dxc.exe", nullptr, MAX_PATH, buf, nullptr);
  if (n > 0 && n < MAX_PATH) {
    return std::filesystem::path(buf);
  }

  const wchar_t* pf86 = _wgetenv(L"ProgramFiles(x86)");
  std::filesystem::path kits =
      (pf86 ? std::filesystem::path(pf86) : std::filesystem::path(L"C:/Program Files (x86)")) /
      L"Windows Kits" / L"10" / L"bin";
  std::error_code ec;
  if (!std::filesystem::exists(kits, ec)) {
    return {};
  }

  std::vector<std::filesystem::path> found;
  for (const auto& entry : std::filesystem::directory_iterator(kits, ec)) {
    const auto candidate = entry.path() / L"x64" / L"dxc.exe";
    if (std::filesystem::exists(candidate, ec)) {
      found.push_back(candidate);
    }
  }
  if (found.empty()) {
    return {};
  }
  std::sort(found.begin(), found.end());
  return found.back();
}

bool ReadFileBytes(const std::filesystem::path& path, std::vector<std::uint8_t>& out, std::string& err) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    err = "could not open " + path.u8string();
    return false;
  }
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  if (out.empty()) {
    err = path.u8string() + " is empty";
    return false;
  }
  return true;
}

bool CompileHlslWithDxc(const std::filesystem::path& hlsl,
                        const std::filesystem::path& cso,
                        std::string& err) {
  const auto dxc = FindDxc();
  if (dxc.empty()) {
    err = "dxc.exe not found on PATH or under Windows Kits\\10\\bin\\<ver>\\x64";
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(cso.parent_path(), ec);

  std::wstring cmd;
  cmd += L"\"";
  cmd += dxc.wstring();
  cmd += L"\" -T cs_6_0 -E CSMain -Fo \"";
  cmd += cso.wstring();
  cmd += L"\" \"";
  cmd += hlsl.wstring();
  cmd += L"\"";

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::wstring mutable_cmd = cmd;
  if (!CreateProcessW(
          nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
          &pi)) {
    err = "CreateProcessW(dxc) failed";
    return false;
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  if (code != 0) {
    err = "dxc exited " + std::to_string(code) + " using " + dxc.u8string();
    return false;
  }
  if (!std::filesystem::exists(cso, ec)) {
    err = "dxc reported success but " + cso.u8string() + " is missing";
    return false;
  }
  return true;
}

bool LoadOrCompileShader(const std::filesystem::path& hint,
                         std::vector<std::uint8_t>& dxil,
                         std::string& source_desc,
                         std::string& err) {
  std::vector<std::filesystem::path> candidates;
  auto push = [&](const std::filesystem::path& p) {
    if (p.empty()) {
      return;
    }
    if (std::find(candidates.begin(), candidates.end(), p) == candidates.end()) {
      candidates.push_back(p);
    }
  };

  push(hint);

  std::vector<std::filesystem::path> roots;
  CollectSearchRoots(roots);
  for (const auto& root : roots) {
    push(root / "hello_compute.cso");
    push(root / "hello_compute.hlsl");
    push(root / "src" / "hlsl" / "hello_compute.hlsl");
    push(root / "build" / "hello_compute.cso");
  }

  std::error_code ec;
  for (const auto& path : candidates) {
    if (!std::filesystem::exists(path, ec)) {
      continue;
    }
    const auto ext = path.extension().string();
    if (ext == ".cso" || ext == ".dxil") {
      if (!ReadFileBytes(path, dxil, err)) {
        return false;
      }
      source_desc = path.u8string() + " (precompiled DXIL)";
      return true;
    }
    if (ext == ".hlsl") {
      const auto tmp = std::filesystem::temp_directory_path() / "xbox_gpu_hello_compute.cso";
      if (!CompileHlslWithDxc(path, tmp, err)) {
        return false;
      }
      if (!ReadFileBytes(tmp, dxil, err)) {
        return false;
      }
      source_desc = path.u8string() + " (compiled at runtime with dxc -T cs_6_0 -E CSMain)";
      return true;
    }
  }

  err = "hello_compute.cso / hello_compute.hlsl not found next to the exe or in the repo tree";
  if (FindDxc().empty()) {
    err += " (dxc missing)";
  }
  return false;
}

Microsoft::WRL::ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* device,
                                                    UINT64 bytes,
                                                    D3D12_HEAP_TYPE heap,
                                                    D3D12_RESOURCE_FLAGS flags,
                                                    D3D12_RESOURCE_STATES state,
                                                    HRESULT& hr) {
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
  hr = device->CreateCommittedResource(
      &props, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource));
  return resource;
}

}  // namespace

RunReport RunHelloCompute(const std::filesystem::path& shader_hint) {
  RunReport report;

  Dx12CreateResult created = CreateDx12Device();
  if (!created.ok) {
    report.status = RunStatus::Blocked;
    report.line = "BLOCKED: no D3D12 device";
    report.detail = created.message + ". Not a GPU result. Not a console result.";
    return report;
  }

  Dx12Device& gpu = created.ctx;

  std::vector<std::uint8_t> dxil;
  std::string source_desc;
  std::string err;
  if (!LoadOrCompileShader(shader_hint, dxil, source_desc, err)) {
    DestroyDx12Device(gpu);
    report.status = RunStatus::Blocked;
    report.line = "BLOCKED: no compiled shader (dxc missing or HLSL not found)";
    report.detail = err + ". Device was created on " + gpu.adapter_name +
                    (gpu.warp ? " (WARP)" : "") + ". Not a GPU result.";
    return report;
  }

  D3D12_DESCRIPTOR_RANGE range{};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = 1;
  range.BaseShaderRegister = 0;
  range.RegisterSpace = 0;
  range.OffsetInDescriptorsFromTableStart = 0;

  D3D12_ROOT_PARAMETER param{};
  param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  param.DescriptorTable.NumDescriptorRanges = 1;
  param.DescriptorTable.pDescriptorRanges = &range;

  D3D12_ROOT_SIGNATURE_DESC rs_desc{};
  rs_desc.NumParameters = 1;
  rs_desc.pParameters = &param;

  Microsoft::WRL::ComPtr<ID3DBlob> rs_blob;
  Microsoft::WRL::ComPtr<ID3DBlob> rs_err;
  HRESULT hr = D3D12SerializeRootSignature(&rs_desc, D3D_ROOT_SIGNATURE_VERSION_1, &rs_blob, &rs_err);
  if (FAILED(hr)) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: root signature serialize";
    report.detail = HrHex(hr);
    if (rs_err) {
      report.detail += " ";
      report.detail += static_cast<const char*>(rs_err->GetBufferPointer());
    }
    DestroyDx12Device(gpu);
    return report;
  }

  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_sig;
  hr = gpu.device->CreateRootSignature(
      0, rs_blob->GetBufferPointer(), rs_blob->GetBufferSize(), IID_PPV_ARGS(&root_sig));
  if (FAILED(hr)) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: CreateRootSignature";
    report.detail = HrHex(hr);
    DestroyDx12Device(gpu);
    return report;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc{};
  pso_desc.pRootSignature = root_sig.Get();
  pso_desc.CS.pShaderBytecode = dxil.data();
  pso_desc.CS.BytecodeLength = dxil.size();
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
  hr = gpu.device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso));
  if (FAILED(hr)) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: CreateComputePipelineState";
    report.detail = HrHex(hr) + " shader=" + source_desc;
    DestroyDx12Device(gpu);
    return report;
  }

  auto uav = CreateBuffer(
      gpu.device.Get(),
      kBufferBytes,
      D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST,
      hr);
  if (FAILED(hr)) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: UAV buffer create";
    report.detail = HrHex(hr);
    DestroyDx12Device(gpu);
    return report;
  }

  auto upload = CreateBuffer(
      gpu.device.Get(), kBufferBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_GENERIC_READ, hr);
  if (FAILED(hr)) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: upload buffer create";
    report.detail = HrHex(hr);
    DestroyDx12Device(gpu);
    return report;
  }

  auto readback = CreateBuffer(
      gpu.device.Get(), kBufferBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST, hr);
  if (FAILED(hr)) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: readback buffer create";
    report.detail = HrHex(hr);
    DestroyDx12Device(gpu);
    return report;
  }

  void* mapped = nullptr;
  hr = upload->Map(0, nullptr, &mapped);
  if (FAILED(hr) || !mapped) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: upload map";
    report.detail = HrHex(hr);
    DestroyDx12Device(gpu);
    return report;
  }
  std::memset(mapped, 0, kBufferBytes);
  upload->Unmap(0, nullptr);

  D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.NumDescriptors = 1;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
  hr = gpu.device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap));
  if (FAILED(hr)) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: descriptor heap create";
    report.detail = HrHex(hr);
    DestroyDx12Device(gpu);
    return report;
  }

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
  uav_desc.Format = DXGI_FORMAT_UNKNOWN;
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.NumElements = kThreadCount;
  uav_desc.Buffer.StructureByteStride = sizeof(UINT);
  gpu.device->CreateUnorderedAccessView(
      uav.Get(), nullptr, &uav_desc, heap->GetCPUDescriptorHandleForHeapStart());

  hr = gpu.allocator->Reset();
  if (FAILED(hr)) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: command allocator reset";
    report.detail = HrHex(hr);
    DestroyDx12Device(gpu);
    return report;
  }
  hr = gpu.list->Reset(gpu.allocator.Get(), pso.Get());
  if (FAILED(hr)) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: command list reset";
    report.detail = HrHex(hr);
    DestroyDx12Device(gpu);
    return report;
  }

  gpu.list->CopyBufferRegion(uav.Get(), 0, upload.Get(), 0, kBufferBytes);

  D3D12_RESOURCE_BARRIER to_uav{};
  to_uav.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  to_uav.Transition.pResource = uav.Get();
  to_uav.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  to_uav.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  to_uav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  gpu.list->ResourceBarrier(1, &to_uav);

  ID3D12DescriptorHeap* heaps[] = {heap.Get()};
  gpu.list->SetDescriptorHeaps(1, heaps);
  gpu.list->SetComputeRootSignature(root_sig.Get());
  gpu.list->SetPipelineState(pso.Get());
  gpu.list->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
  gpu.list->Dispatch(1, 1, 1);

  D3D12_RESOURCE_BARRIER to_copy{};
  to_copy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  to_copy.Transition.pResource = uav.Get();
  to_copy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  to_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  to_copy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  gpu.list->ResourceBarrier(1, &to_copy);
  gpu.list->CopyBufferRegion(readback.Get(), 0, uav.Get(), 0, kBufferBytes);

  hr = gpu.list->Close();
  if (FAILED(hr)) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: command list close";
    report.detail = HrHex(hr);
    DestroyDx12Device(gpu);
    return report;
  }

  ID3D12CommandList* lists[] = {gpu.list.Get()};
  gpu.queue->ExecuteCommandLists(1, lists);
  if (!WaitForGpu(gpu, err)) {
    uav.Detach(); upload.Detach(); readback.Detach();
    heap.Detach(); root_sig.Detach(); pso.Detach();
    report.status = RunStatus::Failed;
    report.line = "FAILED: GPU wait";
    report.detail = err;
    DestroyDx12Device(gpu);
    return report;
  }

  mapped = nullptr;
  hr = readback->Map(0, nullptr, &mapped);
  if (FAILED(hr) || !mapped) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: readback map";
    report.detail = HrHex(hr);
    DestroyDx12Device(gpu);
    return report;
  }

  const auto* values = static_cast<const UINT*>(mapped);
  UINT mismatches = 0;
  UINT first_bad_index = 0;
  UINT first_bad_value = 0;
  for (UINT i = 0; i < kThreadCount; ++i) {
    const UINT expected = i + 1u;
    if (values[i] != expected) {
      if (mismatches == 0) {
        first_bad_index = i;
        first_bad_value = values[i];
      }
      ++mismatches;
    }
  }
  readback->Unmap(0, nullptr);

  std::ostringstream detail;
  detail << "adapter: " << gpu.adapter_name << (gpu.warp ? " (WARP software adapter)" : "") << "\n"
         << "shader: " << source_desc << "\n"
         << "dispatch: 1 thread group, [numthreads(64, 1, 1)], Output[i] = i + 1\n";
  if (gpu.warp) {
    detail << "note: WARP is a Windows software D3D12 device, not Xbox Series S|X hardware.\n";
  }

  if (mismatches != 0) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: hello_compute UAV verify";
    detail << "mismatches: " << mismatches << "/" << kThreadCount << " (first Output["
           << first_bad_index << "]=" << first_bad_value << ", expected " << (first_bad_index + 1)
           << ")";
    report.detail = detail.str();
    DestroyDx12Device(gpu);
    return report;
  }

  report.status = RunStatus::Ok;
  report.line = "STATUS: hello_compute dispatched";
  detail << "verify: " << kThreadCount << "/" << kThreadCount
         << " threads wrote Output[i] == i + 1\n"
         << "not a tok/s result; not a console result";
  report.detail = detail.str();
  DestroyDx12Device(gpu);
  return report;
}

#endif  // _WIN32
