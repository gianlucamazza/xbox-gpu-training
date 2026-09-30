#include "ste_dispatch.h"

#include "cpu_matmul.h"
#include "cpu_ste.h"

#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>

#ifndef _WIN32

RunReport RunSteGradCheckHost(const SteHostOptions&) {
  RunReport report;
  const SteCpuReport cpu = RunSteGradCheck();
  if (!cpu.ok) {
    report.status = RunStatus::Failed;
    report.line = cpu.line;
    report.detail = cpu.detail;
    return report;
  }
  report.status = RunStatus::Blocked;
  report.line = "BLOCKED: no D3D12 device";
  report.detail = cpu.detail +
                  "\nGPU STE/grad kernels were not dispatched. Dispatch log not invented.";
  return report;
}

RunReport RunSteTrainStepHost(const SteHostOptions& options) {
  RunReport report;
  const SteCpuReport cpu = RunSteTrainStep(options.steps);
  if (!cpu.ok) {
    report.status = RunStatus::Failed;
    report.line = cpu.line;
    report.detail = cpu.detail;
    return report;
  }
  report.status = RunStatus::Blocked;
  report.line = "BLOCKED: no D3D12 device";
  report.detail = cpu.detail +
                  "\nGPU STE/grad kernels were not dispatched. Dispatch log not invented.";
  return report;
}

#else

#include "dx12_device.h"

#include <windows.h>

#include <algorithm>
#include <fstream>

namespace {

void PackFP32(const std::vector<float>& in, std::vector<std::uint32_t>& out) {
  out.resize(in.size());
  for (std::size_t i = 0; i < in.size(); ++i) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &in[i], sizeof(bits));
    out[i] = bits;
  }
}

void UnpackFP32(const std::vector<std::uint32_t>& in, std::vector<float>& out) {
  out.resize(in.size());
  for (std::size_t i = 0; i < in.size(); ++i) {
    std::memcpy(&out[i], &in[i], sizeof(float));
  }
}

std::uint32_t AsBits(float v) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &v, sizeof(bits));
  return bits;
}

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

bool CompileHlslWithDxc(const std::filesystem::path& hlsl, const std::filesystem::path& cso,
                        std::string& err) {
  const auto dxc = FindDxc();
  if (dxc.empty()) {
    err = "dxc.exe not found on PATH or under Windows Kits\\10\\bin\\<ver>\\x64";
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(cso.parent_path(), ec);
  std::wstring cmd = L"\"" + dxc.wstring() + L"\" -T cs_6_0 -E CSMain -Fo \"" + cso.wstring() +
                     L"\" \"" + hlsl.wstring() + L"\"";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::wstring mutable_cmd = cmd;
  if (!CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                      nullptr, nullptr, &si, &pi)) {
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

bool LoadOrCompile(const std::filesystem::path& hint, const char* hlsl_name, const char* cso_name,
                   std::vector<std::uint8_t>& dxil, std::string& source_desc, std::string& err) {
  std::vector<std::filesystem::path> candidates;
  auto push = [&](const std::filesystem::path& p) {
    if (!p.empty() &&
        std::find(candidates.begin(), candidates.end(), p) == candidates.end()) {
      candidates.push_back(p);
    }
  };
  push(hint);
  std::vector<std::filesystem::path> roots;
  CollectSearchRoots(roots);
  for (const auto& root : roots) {
    push(root / cso_name);
    push(root / hlsl_name);
    push(root / "src" / "hlsl" / hlsl_name);
    push(root / "build" / cso_name);
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
      const auto tmp = std::filesystem::temp_directory_path() /
                       ("xbox_gpu_" + std::string(cso_name));
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
  err = std::string(hlsl_name) + " / " + cso_name + " not found";
  if (FindDxc().empty()) {
    err += " (dxc missing)";
  }
  return false;
}

Microsoft::WRL::ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* device, UINT64 bytes,
                                                    D3D12_HEAP_TYPE heap, D3D12_RESOURCE_FLAGS flags,
                                                    D3D12_RESOURCE_STATES state, HRESULT& hr) {
  D3D12_HEAP_PROPERTIES props{};
  props.Type = heap;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = bytes < 256 ? 256 : bytes;
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

struct Kernel2in {
  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_sig;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
  std::string source_desc;
};

bool CreatePso(ID3D12Device* device, const std::vector<std::uint8_t>& dxil, UINT const_count,
               UINT srv_count, Kernel2in& k, std::string& err) {
  std::vector<D3D12_ROOT_PARAMETER> params(1 + srv_count + 1);
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[0].Constants.ShaderRegister = 0;
  params[0].Constants.Num32BitValues = const_count;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  for (UINT i = 0; i < srv_count; ++i) {
    params[1 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[1 + i].Descriptor.ShaderRegister = i;
    params[1 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  params.back().ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  params.back().Descriptor.ShaderRegister = 0;
  params.back().ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rs_desc{};
  rs_desc.NumParameters = static_cast<UINT>(params.size());
  rs_desc.pParameters = params.data();
  Microsoft::WRL::ComPtr<ID3DBlob> rs_blob;
  Microsoft::WRL::ComPtr<ID3DBlob> rs_err;
  HRESULT hr = D3D12SerializeRootSignature(&rs_desc, D3D_ROOT_SIGNATURE_VERSION_1, &rs_blob, &rs_err);
  if (FAILED(hr)) {
    err = "D3D12SerializeRootSignature failed (" + HrHex(hr) + ")";
    return false;
  }
  hr = device->CreateRootSignature(0, rs_blob->GetBufferPointer(), rs_blob->GetBufferSize(),
                                   IID_PPV_ARGS(&k.root_sig));
  if (FAILED(hr)) {
    err = "CreateRootSignature failed (" + HrHex(hr) + ")";
    return false;
  }
  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc{};
  pso_desc.pRootSignature = k.root_sig.Get();
  pso_desc.CS.pShaderBytecode = dxil.data();
  pso_desc.CS.BytecodeLength = dxil.size();
  hr = device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&k.pso));
  if (FAILED(hr)) {
    err = "CreateComputePipelineState failed (" + HrHex(hr) + ")";
    return false;
  }
  return true;
}

bool DispatchSimple(Dx12Device& gpu, const Kernel2in& k, const std::uint32_t* consts, UINT nconst,
                    const std::vector<std::vector<std::uint32_t>>& srvs,
                    std::vector<std::uint32_t>& uav, UINT groups_x, UINT groups_y, std::string& err) {
  HRESULT hr = S_OK;
  std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> buf_srv(srvs.size());
  UINT64 upload_bytes = 0;
  for (const auto& s : srvs) {
    upload_bytes += s.size() * sizeof(std::uint32_t);
  }
  if (upload_bytes < 256) {
    upload_bytes = 256;
  }
  auto upload = CreateBuffer(gpu.device.Get(), upload_bytes, D3D12_HEAP_TYPE_UPLOAD,
                             D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, hr);
  if (FAILED(hr)) {
    err = "upload create failed (" + HrHex(hr) + ")";
    return false;
  }
  void* mapped = nullptr;
  hr = upload->Map(0, nullptr, &mapped);
  if (FAILED(hr) || !mapped) {
    err = "upload map failed";
    return false;
  }
  UINT64 off = 0;
  for (const auto& s : srvs) {
    const UINT64 bytes = s.size() * sizeof(std::uint32_t);
    std::memcpy(static_cast<std::uint8_t*>(mapped) + off, s.data(), static_cast<std::size_t>(bytes));
    off += bytes;
  }
  upload->Unmap(0, nullptr);

  off = 0;
  for (std::size_t i = 0; i < srvs.size(); ++i) {
    const UINT64 bytes = srvs[i].size() * sizeof(std::uint32_t);
    buf_srv[i] = CreateBuffer(gpu.device.Get(), bytes < 256 ? 256 : bytes, D3D12_HEAP_TYPE_DEFAULT,
                              D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, hr);
    if (FAILED(hr)) {
      err = "srv buffer create failed";
      return false;
    }
    off += bytes;
  }

  const UINT64 uav_bytes = uav.size() * sizeof(std::uint32_t);
  auto buf_uav = CreateBuffer(gpu.device.Get(), uav_bytes < 256 ? 256 : uav_bytes,
                              D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS, hr);
  if (FAILED(hr)) {
    err = "uav create failed";
    return false;
  }
  auto readback = CreateBuffer(gpu.device.Get(), uav_bytes < 256 ? 256 : uav_bytes,
                               D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
                               D3D12_RESOURCE_STATE_COPY_DEST, hr);
  if (FAILED(hr)) {
    err = "readback create failed";
    return false;
  }

  hr = gpu.allocator->Reset();
  if (FAILED(hr)) {
    err = "allocator reset failed";
    return false;
  }
  hr = gpu.list->Reset(gpu.allocator.Get(), k.pso.Get());
  if (FAILED(hr)) {
    err = "list reset failed";
    return false;
  }

  off = 0;
  std::vector<D3D12_RESOURCE_BARRIER> barriers;
  for (std::size_t i = 0; i < srvs.size(); ++i) {
    const UINT64 bytes = srvs[i].size() * sizeof(std::uint32_t);
    gpu.list->CopyBufferRegion(buf_srv[i].Get(), 0, upload.Get(), off, bytes);
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = buf_srv[i].Get();
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barriers.push_back(b);
    off += bytes;
  }
  if (!barriers.empty()) {
    gpu.list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
  }

  gpu.list->SetComputeRootSignature(k.root_sig.Get());
  gpu.list->SetPipelineState(k.pso.Get());
  gpu.list->SetComputeRoot32BitConstants(0, nconst, consts, 0);
  for (std::size_t i = 0; i < srvs.size(); ++i) {
    gpu.list->SetComputeRootShaderResourceView(static_cast<UINT>(1 + i),
                                               buf_srv[i]->GetGPUVirtualAddress());
  }
  gpu.list->SetComputeRootUnorderedAccessView(static_cast<UINT>(1 + srvs.size()),
                                              buf_uav->GetGPUVirtualAddress());
  gpu.list->Dispatch(groups_x, groups_y, 1);

  D3D12_RESOURCE_BARRIER to_copy{};
  to_copy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  to_copy.Transition.pResource = buf_uav.Get();
  to_copy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  to_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  to_copy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  gpu.list->ResourceBarrier(1, &to_copy);
  gpu.list->CopyBufferRegion(readback.Get(), 0, buf_uav.Get(), 0, uav_bytes);

  hr = gpu.list->Close();
  if (FAILED(hr)) {
    err = "list close failed";
    return false;
  }
  ID3D12CommandList* lists[] = {gpu.list.Get()};
  gpu.queue->ExecuteCommandLists(1, lists);
  if (!WaitForGpu(gpu, err)) {
    return false;
  }
  mapped = nullptr;
  hr = readback->Map(0, nullptr, &mapped);
  if (FAILED(hr) || !mapped) {
    err = "readback map failed";
    return false;
  }
  std::memcpy(uav.data(), mapped, static_cast<std::size_t>(uav_bytes));
  readback->Unmap(0, nullptr);
  return true;
}

bool ComparePacked(const char* name, const std::vector<std::uint32_t>& got,
                   const std::vector<float>& expect, std::ostringstream& detail, std::string& err) {
  std::vector<float> gpu;
  UnpackFP32(got, gpu);
  if (gpu.size() != expect.size()) {
    err = std::string(name) + " size mismatch";
    return false;
  }
  const MatmulError e = CompareFP32(expect.size(), gpu.data(), expect.data(), kSteGradRelFloor);
  detail << "gpu " << name << ": max-abs " << e.max_abs << " max-rel " << e.max_rel << "\n";
  if (!WithinTolerance(e, kSteGpuTolAbs, kSteGpuTolRel)) {
    err = std::string("GPU ") + name + " missed CPU reference";
    return false;
  }
  return true;
}

bool RunGpuKernels(Dx12Device& gpu, const SteHostOptions& options, std::ostringstream& detail,
                   std::string& err) {
  auto load_kernel = [&](const char* hlsl, const char* cso, UINT nconst, UINT nsrv,
                         Kernel2in& k) -> bool {
    std::vector<std::uint8_t> dxil;
    if (!LoadOrCompile(options.shader_hint, hlsl, cso, dxil, k.source_desc, err)) {
      return false;
    }
    return CreatePso(gpu.device.Get(), dxil, nconst, nsrv, k, err);
  };

  Kernel2in k_fq{};
  Kernel2in k_mg{};
  Kernel2in k_r2{};
  Kernel2in k_ste{};
  if (!load_kernel("fakequant_ternary.hlsl", "fakequant_ternary.cso", 4, 1, k_fq) ||
      !load_kernel("matmul_grad.hlsl", "matmul_grad.cso", 4, 2, k_mg) ||
      !load_kernel("relu2_grad.hlsl", "relu2_grad.cso", 4, 2, k_r2) ||
      !load_kernel("ste_backward.hlsl", "ste_backward.cso", 4, 2, k_ste)) {
    return false;
  }
  detail << "shaders: fq=" << k_fq.source_desc << "\n";
  detail << "         mg=" << k_mg.source_desc << "\n";
  detail << "         r2=" << k_r2.source_desc << "\n";
  detail << "         ste=" << k_ste.source_desc << "\n";

  TinySteNet net = MakeDefaultTinySteNet();
  TinySteTensors io;
  if (!TinySteForwardBackward(net, false, io, err)) {
    return false;
  }

  {
    std::vector<std::uint32_t> wbits;
    PackFP32(net.W1, wbits);
    std::vector<std::uint32_t> out(net.W1.size(), 0);
    const std::uint32_t consts[4] = {static_cast<std::uint32_t>(net.W1.size()), AsBits(io.scale1), 0,
                                     0};
    if (!DispatchSimple(gpu, k_fq, consts, 4, {wbits}, out,
                        (static_cast<UINT>(net.W1.size()) + 63) / 64, 1, err)) {
      return false;
    }
    if (!ComparePacked("fakequant W1", out, io.W1q, detail, err)) {
      return false;
    }
  }

  {
    const std::size_t ny = static_cast<std::size_t>(net.B) * net.Out;
    std::vector<float> dy(ny);
    for (std::size_t i = 0; i < ny; ++i) {
      dy[i] = io.y[i] - net.t[i];
    }
    std::vector<float> dw2_pre(io.dW2.size());
    LinearWeightGrad(net.B, net.H, net.Out, dy.data(), io.h.data(), dw2_pre.data());
    std::vector<std::uint32_t> dybits;
    std::vector<std::uint32_t> hbits;
    PackFP32(dy, dybits);
    PackFP32(io.h, hbits);
    std::vector<std::uint32_t> out(dw2_pre.size(), 0);
    const std::uint32_t consts[4] = {net.B, net.Out, net.H, 0};
    if (!DispatchSimple(gpu, k_mg, consts, 4, {dybits, hbits}, out, (net.H + 7) / 8,
                        (net.Out + 7) / 8, err)) {
      return false;
    }
    if (!ComparePacked("matmul_grad dW2", out, dw2_pre, detail, err)) {
      return false;
    }
  }

  {
    const std::size_t nh = static_cast<std::size_t>(net.B) * net.H;
    std::vector<float> dy(static_cast<std::size_t>(net.B) * net.Out);
    for (std::size_t i = 0; i < dy.size(); ++i) {
      dy[i] = io.y[i] - net.t[i];
    }
    std::vector<float> dh(nh);
    LinearInputGrad(net.B, net.H, net.Out, dy.data(), io.W2q.data(), dh.data());
    std::vector<float> dpre(nh);
    Relu2Grad(io.h_pre.data(), dh.data(), dpre.data(), nh);
    std::vector<std::uint32_t> prebits;
    std::vector<std::uint32_t> dhbits;
    PackFP32(io.h_pre, prebits);
    PackFP32(dh, dhbits);
    std::vector<std::uint32_t> out(nh, 0);
    const std::uint32_t consts[4] = {static_cast<std::uint32_t>(nh), 0, 0, 0};
    if (!DispatchSimple(gpu, k_r2, consts, 4, {prebits, dhbits}, out,
                        (static_cast<UINT>(nh) + 63) / 64, 1, err)) {
      return false;
    }
    if (!ComparePacked("relu2_grad", out, dpre, detail, err)) {
      return false;
    }
  }

  {
    std::vector<float> dwq = io.dW1;
    // Recover pre-mask dW1 by dividing by mask where mask==1; zeros stay zero.
    // io.dW1 is already STE-masked. Recompute unmasked dW1 for the shader input.
    TinySteTensors id;
    if (!TinySteForwardBackward(net, true, id, err)) {
      return false;
    }
    // Unmasked quantized-path dW1: rerun without applying STE — use FakeQuant W
    // and skip mask. Reconstruct from LinearWeightGrad on quantized activations.
    const std::size_t nh = static_cast<std::size_t>(net.B) * net.H;
    std::vector<float> dy(static_cast<std::size_t>(net.B) * net.Out);
    for (std::size_t i = 0; i < dy.size(); ++i) {
      dy[i] = io.y[i] - net.t[i];
    }
    std::vector<float> dh(nh);
    LinearInputGrad(net.B, net.H, net.Out, dy.data(), io.W2q.data(), dh.data());
    std::vector<float> dpre(nh);
    Relu2Grad(io.h_pre.data(), dh.data(), dpre.data(), nh);
    std::vector<float> dw1q(io.dW1.size());
    LinearWeightGrad(net.B, net.In, net.H, dpre.data(), net.x.data(), dw1q.data());

    std::vector<std::uint32_t> wbits;
    std::vector<std::uint32_t> gbits;
    PackFP32(net.W1, wbits);
    PackFP32(dw1q, gbits);
    std::vector<std::uint32_t> out(io.dW1.size(), 0);
    const std::uint32_t consts[4] = {static_cast<std::uint32_t>(net.W1.size()), AsBits(io.scale1), 0,
                                     0};
    if (!DispatchSimple(gpu, k_ste, consts, 4, {wbits, gbits}, out,
                        (static_cast<UINT>(net.W1.size()) + 63) / 64, 1, err)) {
      return false;
    }
    if (!ComparePacked("ste_backward dW1", out, io.dW1, detail, err)) {
      return false;
    }
    (void)dwq;
    (void)id;
  }

  return true;
}

RunReport FinishGpu(const SteCpuReport& cpu, Dx12Device& gpu, bool kernels_ok, const std::string& err,
                    const std::string& extra, const char* ok_line, const char* fail_line) {
  RunReport report;
  std::ostringstream detail;
  detail << std::scientific << std::setprecision(4);
  detail << cpu.detail << "\n";
  detail << "adapter: " << gpu.adapter_name << (gpu.warp ? " (WARP software adapter)" : "") << "\n";
  if (gpu.warp) {
    detail << "note: WARP is a Windows software D3D12 device, not Xbox Series S|X hardware.\n";
  }
  detail << extra;
  if (!kernels_ok) {
    report.status = RunStatus::Failed;
    report.line = fail_line;
    report.detail = detail.str() + err;
    DestroyDx12Device(gpu);
    return report;
  }
  report.status = RunStatus::Ok;
  report.line = ok_line;
  report.detail = detail.str() + "not a tok/s result; not a console result";
  DestroyDx12Device(gpu);
  return report;
}

}  // namespace

RunReport RunSteGradCheckHost(const SteHostOptions& options) {
  RunReport report;
  const SteCpuReport cpu = RunSteGradCheck();
  if (!cpu.ok) {
    report.status = RunStatus::Failed;
    report.line = cpu.line;
    report.detail = cpu.detail;
    return report;
  }

  Dx12CreateResult created = CreateDx12Device();
  if (!created.ok) {
    report.status = RunStatus::Blocked;
    report.line = "BLOCKED: no D3D12 device";
    report.detail = cpu.detail + "\n" + created.message +
                    ". GPU STE/grad kernels were not dispatched. Dispatch log not invented.";
    return report;
  }

  std::ostringstream extra;
  extra << std::scientific << std::setprecision(4);
  std::string err;
  const bool ok = RunGpuKernels(created.ctx, options, extra, err);
  if (!ok && (err.find("not found") != std::string::npos || err.find("dxc") != std::string::npos)) {
    report.status = RunStatus::Blocked;
    report.line = "BLOCKED: no compiled shader (dxc missing or HLSL not found)";
    report.detail = err + "\n" + cpu.detail + "\nDevice: " + created.ctx.adapter_name +
                    (created.ctx.warp ? " (WARP)" : "") + ". Not a GPU result.";
    DestroyDx12Device(created.ctx);
    return report;
  }
  return FinishGpu(cpu, created.ctx, ok, err, extra.str(), "STATUS: grad-check dispatched",
                   "FAILED: ste gpu grad-check");
}

RunReport RunSteTrainStepHost(const SteHostOptions& options) {
  RunReport report;
  const SteCpuReport cpu = RunSteTrainStep(options.steps);
  if (!cpu.ok) {
    report.status = RunStatus::Failed;
    report.line = cpu.line;
    report.detail = cpu.detail;
    return report;
  }

  Dx12CreateResult created = CreateDx12Device();
  if (!created.ok) {
    report.status = RunStatus::Blocked;
    report.line = "BLOCKED: no D3D12 device";
    report.detail = cpu.detail + "\n" + created.message +
                    ". GPU STE/grad kernels were not dispatched. Dispatch log not invented.";
    return report;
  }

  std::ostringstream extra;
  extra << std::scientific << std::setprecision(4);
  std::string err;
  const bool ok = RunGpuKernels(created.ctx, options, extra, err);
  if (!ok && (err.find("not found") != std::string::npos || err.find("dxc") != std::string::npos)) {
    report.status = RunStatus::Blocked;
    report.line = "BLOCKED: no compiled shader (dxc missing or HLSL not found)";
    report.detail = err + "\n" + cpu.detail + "\nDevice: " + created.ctx.adapter_name +
                    (created.ctx.warp ? " (WARP)" : "") + ". Not a GPU result.";
    DestroyDx12Device(created.ctx);
    return report;
  }
  return FinishGpu(cpu, created.ctx, ok, err, extra.str(), "STATUS: train-step dispatched",
                   "FAILED: ste gpu train-step");
}

#endif  // _WIN32
