#include "matmul_dispatch.h"

#include "cpu_matmul.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace {

constexpr const char* kSchema = "xbox-gpu-training.benchmark.matmul.v1";

struct BenchRow {
  std::string status;
  std::string precision;
  std::uint32_t M = 0;
  std::uint32_t N = 0;
  std::uint32_t K = 0;
  std::string max_abs;
  std::string max_rel;
  std::string tol_abs;
  std::string tol_rel;
  std::string parity;
  std::string cpu_ms;
  std::string gpu_dispatch_ms;
  std::string device;
  std::string adapter;
  std::string note;
};

std::string CsvEscape(const std::string& field) {
  if (field.find_first_of(",\"\n") == std::string::npos) {
    return field;
  }
  std::string out = "\"";
  for (char c : field) {
    if (c == '"') {
      out += "\"\"";
    } else {
      out += c;
    }
  }
  out += '"';
  return out;
}

std::string FmtFloat(float v) {
  std::ostringstream os;
  os << std::scientific << std::setprecision(6) << v;
  return os.str();
}

std::string FmtMs(double ms) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(3) << ms;
  return os.str();
}

bool WriteCsv(const std::filesystem::path& path, const std::vector<BenchRow>& rows, std::string& err) {
  if (path.empty()) {
    err = "CSV output path is empty";
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::out | std::ios::trunc);
  if (!out) {
    err = "could not write " + path.u8string();
    return false;
  }
  out << "schema,status,precision,M,N,K,max_abs_error,max_rel_error,tol_abs,tol_rel,parity,"
         "cpu_ms,gpu_dispatch_ms,device,adapter,note\n";
  for (const auto& row : rows) {
    out << CsvEscape(kSchema) << ',' << CsvEscape(row.status) << ',' << CsvEscape(row.precision)
        << ',' << row.M << ',' << row.N << ',' << row.K << ',' << CsvEscape(row.max_abs) << ','
        << CsvEscape(row.max_rel) << ',' << CsvEscape(row.tol_abs) << ',' << CsvEscape(row.tol_rel)
        << ',' << CsvEscape(row.parity) << ',' << CsvEscape(row.cpu_ms) << ','
        << CsvEscape(row.gpu_dispatch_ms) << ',' << CsvEscape(row.device) << ','
        << CsvEscape(row.adapter) << ',' << CsvEscape(row.note) << '\n';
  }
  if (!out) {
    err = "failed while writing " + path.u8string();
    return false;
  }
  return true;
}

struct Fixture {
  const char* name;
  MatmulShape shape;
};

const Fixture kFixtures[] = {
    {"hand_2x2x3", {2, 2, 3}},
    {"odd_7x5x9", {7, 5, 9}},
    {"tile_8x8x8", {8, 8, 8}},
    {"tile_16x16x16", {16, 16, 16}},
    {"tile_32x32x32", {32, 32, 32}},
};

void FillDeterministic(std::vector<float>& v, float start, float step) {
  for (std::size_t i = 0; i < v.size(); ++i) {
    v[i] = start + step * static_cast<float>(i % 17);
  }
}

void FillHand(const Fixture& fx, std::vector<float>& a, std::vector<float>& b) {
  a.assign(MatmulACount(fx.shape), 0.0f);
  b.assign(MatmulBCount(fx.shape), 0.0f);
  if (fx.shape.M == 2 && fx.shape.N == 2 && fx.shape.K == 3) {
    a = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f};
    b = {7.f, 8.f, 9.f, 10.f, 11.f, 12.f};
    return;
  }
  FillDeterministic(a, -0.5f, 0.07f);
  FillDeterministic(b, 0.25f, -0.03f);
}

BenchRow CpuRow(const Fixture& fx, const char* precision, const MatmulError& err, float tol_abs,
                float tol_rel, double cpu_ms, const std::string& note) {
  BenchRow row;
  row.status = "cpu-only";
  row.precision = precision;
  row.M = fx.shape.M;
  row.N = fx.shape.N;
  row.K = fx.shape.K;
  row.max_abs = FmtFloat(err.max_abs);
  row.max_rel = FmtFloat(err.max_rel);
  row.tol_abs = FmtFloat(tol_abs);
  row.tol_rel = FmtFloat(tol_rel);
  row.parity = WithinTolerance(err, tol_abs, tol_rel) ? "pass" : "fail";
  row.cpu_ms = FmtMs(cpu_ms);
  row.gpu_dispatch_ms = "";
  row.device = "cpu";
  row.adapter = "portable-gemm";
  row.note = note;
  return row;
}

}  // namespace

#ifndef _WIN32

RunReport RunMatmulBench(const MatmulBenchOptions& options) {
  RunReport report;
  const CpuRefReport cpu = RunCpuReferenceTests();

  std::vector<BenchRow> rows;
  for (const auto& fx : kFixtures) {
    std::vector<float> a;
    std::vector<float> b;
    FillHand(fx, a, b);
    std::vector<float> c(MatmulCCount(fx.shape));
    std::vector<float> c2(MatmulCCount(fx.shape));
    const auto t0 = std::chrono::steady_clock::now();
    CpuMatmulFP32(fx.shape, a.data(), b.data(), c.data());
    const auto t1 = std::chrono::steady_clock::now();
    CpuMatmulFP32(fx.shape, a.data(), b.data(), c2.data());
    const double cpu_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    const MatmulError err = CompareFP32(c.size(), c.data(), c2.data());
    BenchRow row = CpuRow(fx, "fp32", err, kMatmulTolAbsFP32, kMatmulTolRelFP32, cpu_ms,
                          "BLOCKED: no D3D12 device. CPU self-check only. Not a GPU result. "
                          "Not a tok/s result. Not a console result.");
    row.status = "blocked";
    rows.push_back(row);

    std::vector<std::uint16_t> a16(a.size());
    std::vector<std::uint16_t> b16(b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
      a16[i] = FloatToHalf(a[i]);
    }
    for (std::size_t i = 0; i < b.size(); ++i) {
      b16[i] = FloatToHalf(b[i]);
    }
    std::vector<std::uint16_t> c16(MatmulCCount(fx.shape));
    std::vector<std::uint16_t> c16b(MatmulCCount(fx.shape));
    const auto h0 = std::chrono::steady_clock::now();
    CpuMatmulFP16(fx.shape, a16.data(), b16.data(), c16.data());
    const auto h1 = std::chrono::steady_clock::now();
    CpuMatmulFP16(fx.shape, a16.data(), b16.data(), c16b.data());
    const double cpu16_ms =
        std::chrono::duration<double, std::milli>(h1 - h0).count();
    const MatmulError err16 = CompareFP16(c16.size(), c16.data(), c16b.data());
    BenchRow row16 = CpuRow(fx, "fp16", err16, kMatmulTolAbsFP16, kMatmulTolRelFP16, cpu16_ms,
                            "BLOCKED: no D3D12 device. CPU self-check only. Not a GPU result. "
                            "Not a tok/s result. Not a console result.");
    row16.status = "blocked";
    rows.push_back(row16);
  }

  std::string csv_err;
  const std::filesystem::path out =
      options.out_csv.empty() ? std::filesystem::path("benchmarks/results/matmul.csv")
                              : options.out_csv;
  if (!WriteCsv(out, rows, csv_err)) {
    report.status = RunStatus::Failed;
    report.line = "FAILED: matmul CSV write";
    report.detail = csv_err;
    return report;
  }

  if (!cpu.ok) {
    report.status = RunStatus::Failed;
    report.line = cpu.line;
    report.detail = cpu.detail + "\ncsv: " + out.u8string();
    return report;
  }

  report.status = RunStatus::Blocked;
  report.line = "BLOCKED: no D3D12 device";
  report.detail = cpu.detail + "\ncsv: " + out.u8string() +
                  "\nGPU matmul was not dispatched. Dispatch log not invented.";
  return report;
}

#else

#include "dx12_device.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

namespace {

constexpr UINT kThreadXY = 8;

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

bool CompileHlslWithDxc(const std::filesystem::path& hlsl, const char* entry,
                        const std::filesystem::path& cso, std::string& err) {
  const auto dxc = FindDxc();
  if (dxc.empty()) {
    err = "dxc.exe not found on PATH or under Windows Kits\\10\\bin\\<ver>\\x64";
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(cso.parent_path(), ec);

  std::wstring wentry(entry, entry + std::strlen(entry));
  std::wstring cmd;
  cmd += L"\"";
  cmd += dxc.wstring();
  cmd += L"\" -T cs_6_0 -E ";
  cmd += wentry;
  cmd += L" -Fo \"";
  cmd += cso.wstring();
  cmd += L"\" \"";
  cmd += hlsl.wstring();
  cmd += L"\"";

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
    err = "dxc exited " + std::to_string(code) + " entry " + entry + " using " + dxc.u8string();
    return false;
  }
  if (!std::filesystem::exists(cso, ec)) {
    err = "dxc reported success but " + cso.u8string() + " is missing";
    return false;
  }
  return true;
}

bool LoadOrCompileMatmul(const std::filesystem::path& hint, const char* entry,
                         const char* cso_name, std::vector<std::uint8_t>& dxil,
                         std::string& source_desc, std::string& err) {
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
    push(root / cso_name);
    push(root / "matmul.cso");
    push(root / "matmul.hlsl");
    push(root / "src" / "hlsl" / "matmul.hlsl");
    push(root / "build" / cso_name);
    push(root / "build" / "matmul.cso");
  }

  std::error_code ec;
  for (const auto& path : candidates) {
    if (!std::filesystem::exists(path, ec)) {
      continue;
    }
    const auto ext = path.extension().string();
    if (ext == ".cso" || ext == ".dxil") {
      // A generic matmul.cso is CSMain (FP32). Skip it for the FP16 entry.
      if (std::strcmp(entry, "CSMainFP16") == 0 && path.stem() == "matmul") {
        continue;
      }
      if (!ReadFileBytes(path, dxil, err)) {
        return false;
      }
      source_desc = path.u8string() + " (precompiled DXIL, entry " + entry + ")";
      return true;
    }
    if (ext == ".hlsl") {
      const auto tmp = std::filesystem::temp_directory_path() /
                       ("xbox_gpu_" + std::string(entry) + ".cso");
      if (!CompileHlslWithDxc(path, entry, tmp, err)) {
        return false;
      }
      if (!ReadFileBytes(tmp, dxil, err)) {
        return false;
      }
      source_desc = path.u8string() + " (compiled at runtime with dxc -T cs_6_0 -E " + entry + ")";
      return true;
    }
  }

  err = std::string("matmul shader not found for entry ") + entry;
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

bool CreateComputePso(ID3D12Device* device, const std::vector<std::uint8_t>& dxil,
                      Microsoft::WRL::ComPtr<ID3D12RootSignature>& root_sig,
                      Microsoft::WRL::ComPtr<ID3D12PipelineState>& pso, std::string& err) {
  D3D12_ROOT_PARAMETER params[4]{};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[0].Constants.ShaderRegister = 0;
  params[0].Constants.RegisterSpace = 0;
  params[0].Constants.Num32BitValues = 4;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[1].Descriptor.ShaderRegister = 0;
  params[1].Descriptor.RegisterSpace = 0;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[2].Descriptor.ShaderRegister = 1;
  params[2].Descriptor.RegisterSpace = 0;
  params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  params[3].Descriptor.ShaderRegister = 0;
  params[3].Descriptor.RegisterSpace = 0;
  params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rs_desc{};
  rs_desc.NumParameters = 4;
  rs_desc.pParameters = params;

  Microsoft::WRL::ComPtr<ID3DBlob> rs_blob;
  Microsoft::WRL::ComPtr<ID3DBlob> rs_err;
  HRESULT hr = D3D12SerializeRootSignature(&rs_desc, D3D_ROOT_SIGNATURE_VERSION_1, &rs_blob, &rs_err);
  if (FAILED(hr)) {
    err = "D3D12SerializeRootSignature failed (" + HrHex(hr) + ")";
    if (rs_err) {
      err += " ";
      err += static_cast<const char*>(rs_err->GetBufferPointer());
    }
    return false;
  }

  hr = device->CreateRootSignature(0, rs_blob->GetBufferPointer(), rs_blob->GetBufferSize(),
                                   IID_PPV_ARGS(&root_sig));
  if (FAILED(hr)) {
    err = "CreateRootSignature failed (" + HrHex(hr) + ")";
    return false;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc{};
  pso_desc.pRootSignature = root_sig.Get();
  pso_desc.CS.pShaderBytecode = dxil.data();
  pso_desc.CS.BytecodeLength = dxil.size();
  hr = device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso));
  if (FAILED(hr)) {
    err = "CreateComputePipelineState failed (" + HrHex(hr) + ")";
    return false;
  }
  return true;
}

struct GpuKernel {
  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_sig;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
  std::string source_desc;
};

bool DispatchMatmul(Dx12Device& gpu, const GpuKernel& kernel, MatmulShape shape,
                    const std::vector<std::uint32_t>& a_bits, const std::vector<std::uint32_t>& b_bits,
                    std::vector<std::uint32_t>& c_bits, double& gpu_ms, std::string& err) {
  if (gpu.fault) { err = gpu.fault.error; return false; }
  const UINT64 a_bytes = a_bits.size() * sizeof(std::uint32_t);
  const UINT64 b_bytes = b_bits.size() * sizeof(std::uint32_t);
  const UINT64 c_bytes = static_cast<UINT64>(MatmulCCount(shape)) * sizeof(std::uint32_t);

  HRESULT hr = S_OK;
  auto buf_a = CreateBuffer(gpu.device.Get(), a_bytes, D3D12_HEAP_TYPE_DEFAULT,
                            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, hr);
  if (FAILED(hr)) {
    err = "A buffer create failed (" + HrHex(hr) + ")";
    return false;
  }
  auto buf_b = CreateBuffer(gpu.device.Get(), b_bytes, D3D12_HEAP_TYPE_DEFAULT,
                            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, hr);
  if (FAILED(hr)) {
    err = "B buffer create failed (" + HrHex(hr) + ")";
    return false;
  }
  auto buf_c = CreateBuffer(gpu.device.Get(), c_bytes, D3D12_HEAP_TYPE_DEFAULT,
                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, hr);
  if (FAILED(hr)) {
    err = "C buffer create failed (" + HrHex(hr) + ")";
    return false;
  }

  const UINT64 upload_bytes = a_bytes + b_bytes;
  auto upload = CreateBuffer(gpu.device.Get(), upload_bytes, D3D12_HEAP_TYPE_UPLOAD,
                             D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, hr);
  if (FAILED(hr)) {
    err = "upload buffer create failed (" + HrHex(hr) + ")";
    return false;
  }
  auto readback = CreateBuffer(gpu.device.Get(), c_bytes, D3D12_HEAP_TYPE_READBACK,
                               D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, hr);
  if (FAILED(hr)) {
    err = "readback buffer create failed (" + HrHex(hr) + ")";
    return false;
  }

  void* mapped = nullptr;
  hr = upload->Map(0, nullptr, &mapped);
  if (FAILED(hr) || !mapped) {
    err = "upload map failed (" + HrHex(hr) + ")";
    return false;
  }
  std::memcpy(mapped, a_bits.data(), static_cast<std::size_t>(a_bytes));
  std::memcpy(static_cast<std::uint8_t*>(mapped) + a_bytes, b_bits.data(),
              static_cast<std::size_t>(b_bytes));
  upload->Unmap(0, nullptr);

  hr = gpu.allocator->Reset();
  if (FAILED(hr)) {
    err = "command allocator reset failed (" + HrHex(hr) + ")";
    return false;
  }
  hr = gpu.list->Reset(gpu.allocator.Get(), kernel.pso.Get());
  if (FAILED(hr)) {
    err = "command list reset failed (" + HrHex(hr) + ")";
    return false;
  }

  gpu.list->CopyBufferRegion(buf_a.Get(), 0, upload.Get(), 0, a_bytes);
  gpu.list->CopyBufferRegion(buf_b.Get(), 0, upload.Get(), a_bytes, b_bytes);

  D3D12_RESOURCE_BARRIER barriers[2]{};
  barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barriers[0].Transition.pResource = buf_a.Get();
  barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barriers[1].Transition.pResource = buf_b.Get();
  barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  gpu.list->ResourceBarrier(2, barriers);

  gpu.list->SetComputeRootSignature(kernel.root_sig.Get());
  gpu.list->SetPipelineState(kernel.pso.Get());
  const std::uint32_t consts[4] = {shape.M, shape.N, shape.K, 0};
  gpu.list->SetComputeRoot32BitConstants(0, 4, consts, 0);
  gpu.list->SetComputeRootShaderResourceView(1, buf_a->GetGPUVirtualAddress());
  gpu.list->SetComputeRootShaderResourceView(2, buf_b->GetGPUVirtualAddress());
  gpu.list->SetComputeRootUnorderedAccessView(3, buf_c->GetGPUVirtualAddress());

  const UINT groups_x = (shape.N + kThreadXY - 1) / kThreadXY;
  const UINT groups_y = (shape.M + kThreadXY - 1) / kThreadXY;
  gpu.list->Dispatch(groups_x, groups_y, 1);

  D3D12_RESOURCE_BARRIER to_copy{};
  to_copy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  to_copy.Transition.pResource = buf_c.Get();
  to_copy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  to_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  to_copy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  gpu.list->ResourceBarrier(1, &to_copy);
  gpu.list->CopyBufferRegion(readback.Get(), 0, buf_c.Get(), 0, c_bytes);

  hr = gpu.list->Close();
  if (FAILED(hr)) {
    err = "command list close failed (" + HrHex(hr) + ")";
    return false;
  }

  const auto t0 = std::chrono::steady_clock::now();
  ID3D12CommandList* lists[] = {gpu.list.Get()};
  gpu.queue->ExecuteCommandLists(1, lists);
  if (!WaitForGpu(gpu, err)) {
    buf_a.Detach(); buf_b.Detach(); buf_c.Detach();
    upload.Detach(); readback.Detach();
    kernel.root_sig->AddRef(); kernel.pso->AddRef();
    return false;
  }
  const auto t1 = std::chrono::steady_clock::now();
  gpu_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

  mapped = nullptr;
  hr = readback->Map(0, nullptr, &mapped);
  if (FAILED(hr) || !mapped) {
    err = "readback map failed (" + HrHex(hr) + ")";
    return false;
  }
  c_bits.resize(MatmulCCount(shape));
  std::memcpy(c_bits.data(), mapped, static_cast<std::size_t>(c_bytes));
  readback->Unmap(0, nullptr);
  return true;
}

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
    float v = 0.0f;
    std::memcpy(&v, &in[i], sizeof(v));
    out[i] = v;
  }
}

void PackFP16(const std::vector<std::uint16_t>& in, std::vector<std::uint32_t>& out) {
  out.resize(in.size());
  for (std::size_t i = 0; i < in.size(); ++i) {
    out[i] = in[i];
  }
}

void UnpackFP16(const std::vector<std::uint32_t>& in, std::vector<std::uint16_t>& out) {
  out.resize(in.size());
  for (std::size_t i = 0; i < in.size(); ++i) {
    out[i] = static_cast<std::uint16_t>(in[i] & 0xffffu);
  }
}

}  // namespace

RunReport RunMatmulBench(const MatmulBenchOptions& options) {
  RunReport report;
  const CpuRefReport cpu = RunCpuReferenceTests();
  std::ostringstream detail;
  detail << cpu.detail << "\n";

  const std::filesystem::path out =
      options.out_csv.empty() ? std::filesystem::path("benchmarks/results/matmul.csv")
                              : options.out_csv;

  std::vector<BenchRow> rows;

  auto finish_csv = [&](RunStatus status, const std::string& line) -> RunReport {
    std::string csv_err;
    if (!WriteCsv(out, rows, csv_err)) {
      report.status = RunStatus::Failed;
      report.line = "FAILED: matmul CSV write";
      report.detail = csv_err;
      return report;
    }
    report.status = status;
    report.line = line;
    report.detail = detail.str() + "csv: " + out.u8string();
    return report;
  };

  if (!cpu.ok) {
    report.status = RunStatus::Failed;
    report.line = cpu.line;
    report.detail = cpu.detail;
    return report;
  }

  Dx12CreateResult created = CreateDx12Device();
  if (!created.ok) {
    for (const auto& fx : kFixtures) {
      std::vector<float> a;
      std::vector<float> b;
      FillHand(fx, a, b);
      std::vector<float> c(MatmulCCount(fx.shape), 0.0f);
      const auto t0 = std::chrono::steady_clock::now();
      CpuMatmulFP32(fx.shape, a.data(), b.data(), c.data());
      const auto t1 = std::chrono::steady_clock::now();
      const MatmulError err = CompareFP32(c.size(), c.data(), c.data());
      BenchRow row = CpuRow(fx, "fp32", err, kMatmulTolAbsFP32, kMatmulTolRelFP32,
                            std::chrono::duration<double, std::milli>(t1 - t0).count(),
                            "BLOCKED: no D3D12 device. CPU reference only. Not a GPU result. "
                            "Dispatch log not invented.");
      row.status = "blocked";
      rows.push_back(row);
    }
    detail << created.message << ". Not a GPU result. Not a console result.\n"
           << "GPU matmul was not dispatched. Dispatch log not invented.\n";
    return finish_csv(RunStatus::Blocked, "BLOCKED: no D3D12 device");
  }

  Dx12Device& gpu = created.ctx;

  std::vector<std::uint8_t> dxil32;
  std::vector<std::uint8_t> dxil16;
  std::string src32;
  std::string src16;
  std::string err;
  if (!LoadOrCompileMatmul(options.shader_hint, "CSMainFP32", "matmul_fp32.cso", dxil32, src32,
                           err)) {
    // CSMain is the FP32 alias; accept matmul.cso compiled with -E CSMain.
    err.clear();
    if (!LoadOrCompileMatmul(options.shader_hint, "CSMain", "matmul.cso", dxil32, src32, err)) {
      DestroyDx12Device(gpu);
      detail << err << ". Device was created on " << gpu.adapter_name
             << (gpu.warp ? " (WARP)" : "") << ". Not a GPU result.\n";
      for (const auto& fx : kFixtures) {
        BenchRow row;
        row.status = "blocked";
        row.precision = "fp32";
        row.M = fx.shape.M;
        row.N = fx.shape.N;
        row.K = fx.shape.K;
        row.parity = "n/a";
        row.device = "none";
        row.note = "BLOCKED: no compiled shader (dxc missing or HLSL not found)";
        rows.push_back(row);
      }
      return finish_csv(RunStatus::Blocked,
                        "BLOCKED: no compiled shader (dxc missing or HLSL not found)");
    }
  }

  bool have_fp16 = LoadOrCompileMatmul(options.shader_hint, "CSMainFP16", "matmul_fp16.cso", dxil16,
                                       src16, err);
  if (!have_fp16) {
    detail << "FP16 shader: BLOCKED (" << err << "). FP32 will still run.\n";
    err.clear();
  }

  GpuKernel k32;
  if (!CreateComputePso(gpu.device.Get(), dxil32, k32.root_sig, k32.pso, err)) {
    DestroyDx12Device(gpu);
    report.status = RunStatus::Failed;
    report.line = "FAILED: matmul FP32 PSO";
    report.detail = err + " shader=" + src32;
    return report;
  }
  k32.source_desc = src32;

  GpuKernel k16;
  if (have_fp16 && !CreateComputePso(gpu.device.Get(), dxil16, k16.root_sig, k16.pso, err)) {
    DestroyDx12Device(gpu);
    report.status = RunStatus::Failed;
    report.line = "FAILED: matmul FP16 PSO";
    report.detail = err + " shader=" + src16;
    return report;
  }
  k16.source_desc = src16;

  detail << "adapter: " << gpu.adapter_name << (gpu.warp ? " (WARP software adapter)" : "") << "\n"
         << "shader fp32: " << src32 << "\n";
  if (have_fp16) {
    detail << "shader fp16: " << src16 << "\n";
  }
  if (gpu.warp) {
    detail << "note: WARP is a Windows software D3D12 device, not Xbox Series S|X hardware.\n";
  }
  detail << "dispatch: [numthreads(8, 8, 1)], C = A @ B, not a tok/s result\n";

  bool any_fail = false;
  float worst_abs32 = 0.0f;
  float worst_rel32 = 0.0f;
  float worst_abs16 = 0.0f;
  float worst_rel16 = 0.0f;

  for (const auto& fx : kFixtures) {
    std::vector<float> a;
    std::vector<float> b;
    FillHand(fx, a, b);
    std::vector<float> cref(MatmulCCount(fx.shape));
    const auto t0 = std::chrono::steady_clock::now();
    CpuMatmulFP32(fx.shape, a.data(), b.data(), cref.data());
    const auto t1 = std::chrono::steady_clock::now();
    const double cpu_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    std::vector<std::uint32_t> a_bits;
    std::vector<std::uint32_t> b_bits;
    std::vector<std::uint32_t> c_bits;
    PackFP32(a, a_bits);
    PackFP32(b, b_bits);
    double gpu_ms = 0.0;
    if (!DispatchMatmul(gpu, k32, fx.shape, a_bits, b_bits, c_bits, gpu_ms, err)) {
      any_fail = true;
      BenchRow row;
      row.status = "failed";
      row.precision = "fp32";
      row.M = fx.shape.M;
      row.N = fx.shape.N;
      row.K = fx.shape.K;
      row.parity = "fail";
      row.cpu_ms = FmtMs(cpu_ms);
      row.device = gpu.warp ? "d3d12-warp" : "d3d12";
      row.adapter = gpu.adapter_name;
      row.note = err;
      rows.push_back(row);
      detail << "FAILED fp32 " << fx.name << ": " << err << "\n";
      continue;
    }

    std::vector<float> cgpu;
    UnpackFP32(c_bits, cgpu);
    const MatmulError err32 = CompareFP32(cref.size(), cgpu.data(), cref.data());
    worst_abs32 = std::max(worst_abs32, err32.max_abs);
    worst_rel32 = std::max(worst_rel32, err32.max_rel);
    const bool pass32 = WithinTolerance(err32, kMatmulTolAbsFP32, kMatmulTolRelFP32);
    if (!pass32) {
      any_fail = true;
    }
    BenchRow row;
    row.status = pass32 ? "ok" : "failed";
    row.precision = "fp32";
    row.M = fx.shape.M;
    row.N = fx.shape.N;
    row.K = fx.shape.K;
    row.max_abs = FmtFloat(err32.max_abs);
    row.max_rel = FmtFloat(err32.max_rel);
    row.tol_abs = FmtFloat(kMatmulTolAbsFP32);
    row.tol_rel = FmtFloat(kMatmulTolRelFP32);
    row.parity = pass32 ? "pass" : "fail";
    row.cpu_ms = FmtMs(cpu_ms);
    row.gpu_dispatch_ms = FmtMs(gpu_ms);
    row.device = gpu.warp ? "d3d12-warp" : "d3d12";
    row.adapter = gpu.adapter_name;
    row.note = std::string(fx.name) +
               "; host wall includes upload+dispatch+readback; not tok/s; not console";
    rows.push_back(row);
    detail << "fp32 " << fx.name << " " << fx.shape.M << "x" << fx.shape.N << "x" << fx.shape.K
           << " max-abs " << FmtFloat(err32.max_abs) << " max-rel " << FmtFloat(err32.max_rel)
           << (pass32 ? " PASS" : " FAIL") << "\n";

    if (!have_fp16) {
      continue;
    }

    std::vector<std::uint16_t> a16(a.size());
    std::vector<std::uint16_t> b16(b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
      a16[i] = FloatToHalf(a[i]);
    }
    for (std::size_t i = 0; i < b.size(); ++i) {
      b16[i] = FloatToHalf(b[i]);
    }
    std::vector<std::uint16_t> cref16(MatmulCCount(fx.shape));
    const auto h0 = std::chrono::steady_clock::now();
    CpuMatmulFP16(fx.shape, a16.data(), b16.data(), cref16.data());
    const auto h1 = std::chrono::steady_clock::now();
    const double cpu16_ms = std::chrono::duration<double, std::milli>(h1 - h0).count();

    std::vector<std::uint32_t> a16_bits;
    std::vector<std::uint32_t> b16_bits;
    std::vector<std::uint32_t> c16_bits;
    PackFP16(a16, a16_bits);
    PackFP16(b16, b16_bits);
    double gpu16_ms = 0.0;
    if (!DispatchMatmul(gpu, k16, fx.shape, a16_bits, b16_bits, c16_bits, gpu16_ms, err)) {
      any_fail = true;
      BenchRow r16;
      r16.status = "failed";
      r16.precision = "fp16";
      r16.M = fx.shape.M;
      r16.N = fx.shape.N;
      r16.K = fx.shape.K;
      r16.parity = "fail";
      r16.cpu_ms = FmtMs(cpu16_ms);
      r16.device = gpu.warp ? "d3d12-warp" : "d3d12";
      r16.adapter = gpu.adapter_name;
      r16.note = err;
      rows.push_back(r16);
      detail << "FAILED fp16 " << fx.name << ": " << err << "\n";
      continue;
    }
    std::vector<std::uint16_t> cgpu16;
    UnpackFP16(c16_bits, cgpu16);
    const MatmulError err16 = CompareFP16(cref16.size(), cgpu16.data(), cref16.data());
    worst_abs16 = std::max(worst_abs16, err16.max_abs);
    worst_rel16 = std::max(worst_rel16, err16.max_rel);
    const bool pass16 = WithinTolerance(err16, kMatmulTolAbsFP16, kMatmulTolRelFP16);
    if (!pass16) {
      any_fail = true;
    }
    BenchRow r16;
    r16.status = pass16 ? "ok" : "failed";
    r16.precision = "fp16";
    r16.M = fx.shape.M;
    r16.N = fx.shape.N;
    r16.K = fx.shape.K;
    r16.max_abs = FmtFloat(err16.max_abs);
    r16.max_rel = FmtFloat(err16.max_rel);
    r16.tol_abs = FmtFloat(kMatmulTolAbsFP16);
    r16.tol_rel = FmtFloat(kMatmulTolRelFP16);
    r16.parity = pass16 ? "pass" : "fail";
    r16.cpu_ms = FmtMs(cpu16_ms);
    r16.gpu_dispatch_ms = FmtMs(gpu16_ms);
    r16.device = gpu.warp ? "d3d12-warp" : "d3d12";
    r16.adapter = gpu.adapter_name;
    r16.note = std::string(fx.name) +
               "; host wall includes upload+dispatch+readback; not tok/s; not console";
    rows.push_back(r16);
    detail << "fp16 " << fx.name << " " << fx.shape.M << "x" << fx.shape.N << "x" << fx.shape.K
           << " max-abs " << FmtFloat(err16.max_abs) << " max-rel " << FmtFloat(err16.max_rel)
           << (pass16 ? " PASS" : " FAIL") << "\n";
  }

  DestroyDx12Device(gpu);

  detail << "worst GPU vs CPU FP32 max-abs " << FmtFloat(worst_abs32) << " max-rel "
         << FmtFloat(worst_rel32) << "\n";
  if (have_fp16) {
    detail << "worst GPU vs CPU FP16 max-abs " << FmtFloat(worst_abs16) << " max-rel "
           << FmtFloat(worst_rel16) << "\n";
  }
  detail << "ggml: not vendored (portable CPU GEMM; see docs/ggml-baseline.md)\n"
         << "not a tok/s result; not a console result\n";

  if (any_fail) {
    return finish_csv(RunStatus::Failed, "FAILED: matmul parity or dispatch");
  }
  return finish_csv(RunStatus::Ok, "STATUS: matmul bench ok");
}

#endif  // _WIN32
