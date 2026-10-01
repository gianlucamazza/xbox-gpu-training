#include "dx12_device.h"

#include <chrono>
#include <cstdio>
#include <utility>

#ifndef _WIN32

// Non-Windows translation unit stays empty; hello_dispatch.cpp reports BLOCKED.

#else

std::string HrHex(long hr) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned>(hr));
  return buf;
}

static std::string WideToUtf8(const wchar_t *text) {
  if (!text || !text[0]) {
    return {};
  }
  const int n =
      WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1) {
    return {};
  }
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), n, nullptr, nullptr);
  out.pop_back();
  return out;
}

static void TryEnableDebugLayer() {
  Microsoft::WRL::ComPtr<ID3D12Debug> debug;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
    debug->EnableDebugLayer();
  }
}

Dx12CreateResult CreateDx12Device() {
  Dx12CreateResult result;

  // Release console probes must use the same runtime as the measured job.
#ifndef XGPU_UWP
  TryEnableDebugLayer();
#endif

  Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
  HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    result.blocked = true;
    result.message = "CreateDXGIFactory1 failed (" + HrHex(hr) + ")";
    return result;
  }

  auto try_adapter = [&](IDXGIAdapter *adapter, bool warp,
                         const std::string &name) -> bool {
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    const HRESULT create_hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0,
                                                IID_PPV_ARGS(&device));
    if (FAILED(create_hr)) {
      result.message =
          "D3D12CreateDevice failed on " + name + " (" + HrHex(create_hr) + ")";
      return false;
    }

    D3D12_COMMAND_QUEUE_DESC qdesc{};
    qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    hr = device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&queue));
    if (FAILED(hr)) {
      result.message = "CreateCommandQueue failed (" + HrHex(hr) + ")";
      return false;
    }

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        IID_PPV_ARGS(&allocator));
    if (FAILED(hr)) {
      result.message = "CreateCommandAllocator failed (" + HrHex(hr) + ")";
      return false;
    }

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                   allocator.Get(), nullptr,
                                   IID_PPV_ARGS(&list));
    if (FAILED(hr)) {
      result.message = "CreateCommandList failed (" + HrHex(hr) + ")";
      return false;
    }
    hr = list->Close();
    if (FAILED(hr)) {
      result.message =
          "ID3D12GraphicsCommandList::Close failed (" + HrHex(hr) + ")";
      return false;
    }

    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr)) {
      result.message = "CreateFence failed (" + HrHex(hr) + ")";
      return false;
    }

    HANDLE event = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    if (!event) {
      result.message = "CreateEventW failed";
      return false;
    }

    result.ok = true;
    result.blocked = false;
    result.message = warp ? "WARP software adapter" : "hardware adapter";
    result.ctx.device = std::move(device);
    result.ctx.queue = std::move(queue);
    result.ctx.allocator = std::move(allocator);
    result.ctx.list = std::move(list);
    result.ctx.fence = std::move(fence);
    result.ctx.fence_event = event;
    result.ctx.adapter_name = name;
    result.ctx.warp = warp;
    return true;
  };

  std::string attempts;
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  for (UINT i = 0;; ++i) {
    const HRESULT enum_hr = factory->EnumAdapters1(i, &adapter);
    if (enum_hr == DXGI_ERROR_NOT_FOUND)
      break;
    if (FAILED(enum_hr)) {
      attempts += " EnumAdapters1=" + HrHex(enum_hr);
      break;
    }
    DXGI_ADAPTER_DESC1 desc{};
    const HRESULT desc_hr = adapter->GetDesc1(&desc);
    if (FAILED(desc_hr)) {
      attempts += " GetDesc1=" + HrHex(desc_hr);
      adapter.Reset();
      continue;
    }
    attempts += " [" + WideToUtf8(desc.Description) +
                " vendor=" + std::to_string(desc.VendorId) +
                " device=" + std::to_string(desc.DeviceId) +
                " flags=" + std::to_string(desc.Flags) + "]";
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      adapter.Reset();
      continue;
    }
    const std::string name = WideToUtf8(desc.Description);
    if (try_adapter(adapter.Get(), false,
                    name.empty() ? "hardware adapter" : name)) {
      return result;
    }
    attempts += " " + result.message;
    adapter.Reset();
  }
#ifdef XGPU_UWP
  result.blocked = true;
  result.message = "no usable hardware adapter:" + attempts;
  return result;
#endif

  Microsoft::WRL::ComPtr<IDXGIAdapter> warp;
  hr = factory->EnumWarpAdapter(IID_PPV_ARGS(&warp));
  if (SUCCEEDED(hr)) {
    DXGI_ADAPTER_DESC desc{};
    std::string name = "WARP";
    Microsoft::WRL::ComPtr<IDXGIAdapter1> warp1;
    if (SUCCEEDED(warp.As(&warp1))) {
      DXGI_ADAPTER_DESC1 d1{};
      if (SUCCEEDED(warp1->GetDesc1(&d1))) {
        const std::string decoded = WideToUtf8(d1.Description);
        if (!decoded.empty()) {
          name = decoded;
        }
      }
    } else if (SUCCEEDED(warp->GetDesc(&desc))) {
      const std::string decoded = WideToUtf8(desc.Description);
      if (!decoded.empty()) {
        name = decoded;
      }
    }
    if (try_adapter(warp.Get(), true, name)) {
      return result;
    }
  } else {
    result.message = "EnumWarpAdapter failed (" + HrHex(hr) + ")";
  }

  result.ok = false;
  result.blocked = true;
  if (result.message.empty()) {
    result.message =
        "D3D12CreateDevice failed on every adapter, including WARP";
  }
  return result;
}

void DestroyDx12Device(Dx12Device &ctx) {
  if (ctx.fault) {
    // Intentionally retained until process termination: the GPU may still use
    // these objects or the event. No destructor flush and no resource
    // recycling.
    ctx.list.Detach();
    ctx.allocator.Detach();
    ctx.queue.Detach();
    ctx.fence.Detach();
    ctx.device.Detach();
    ctx.fence_event = nullptr;
    return;
  }
  if (ctx.fence_event) {
    CloseHandle(ctx.fence_event);
    ctx.fence_event = nullptr;
  }
  ctx.list.Reset();
  ctx.allocator.Reset();
  ctx.queue.Reset();
  ctx.fence.Reset();
  ctx.device.Reset();
}

bool WaitForGpu(Dx12Device &ctx, std::string &err) {
  if (ctx.fault) {
    err = ctx.fault.error;
    return false;
  }
  if (!ctx.queue || !ctx.fence || !ctx.fence_event) {
    ctx.fault = {"gpu_fence_error", "WaitForGpu without a live device", 0, 0,
                 0};
    err = ctx.fault.error;
    return false;
  }
  const auto probe = std::exchange(ctx.fault_probe, {});
  uint64_t synthetic_clock = 0;
  GpuWaitApi api;
  api.now_ms = [&] {
    return probe == "gpu_wait_timeout"
               ? synthetic_clock
               : static_cast<uint64_t>(
                     std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count());
  };
  api.completed = [&] {
    return probe.empty() ? ctx.fence->GetCompletedValue() : uint64_t(0);
  };
  api.device_error = [&] {
    if (probe == "gpu_device_removed")
      return std::string("functional probe: device removed");
    const auto hr = ctx.device->GetDeviceRemovedReason();
    return FAILED(hr) ? HrHex(hr) : std::string();
  };
  api.signal = [&](uint64_t value) {
    const auto hr = ctx.queue->Signal(ctx.fence.Get(), value);
    return FAILED(hr) ? "Signal: " + HrHex(hr) : std::string();
  };
  api.arm = [&](uint64_t value) {
    const auto hr = ctx.fence->SetEventOnCompletion(value, ctx.fence_event);
    return FAILED(hr) ? "SetEventOnCompletion: " + HrHex(hr) : std::string();
  };
  DWORD wait_code = 0, wait_error = 0;
  api.wait = [&](uint32_t ms) {
    if (probe == "gpu_wait_timeout") {
      synthetic_clock += ms;
      return GpuWaitApi::Result::Timeout;
    }
    if (probe == "gpu_wait_failed") {
      wait_error = ERROR_INVALID_HANDLE;
      return GpuWaitApi::Result::Failed;
    }
    wait_code = WaitForSingleObjectEx(ctx.fence_event, ms, FALSE);
    if (wait_code == WAIT_FAILED) {
      wait_error = GetLastError();
      return GpuWaitApi::Result::Failed;
    }
    if (wait_code == WAIT_TIMEOUT)
      return GpuWaitApi::Result::Timeout;
    return wait_code == WAIT_OBJECT_0 ? GpuWaitApi::Result::Wake
                                      : GpuWaitApi::Result::Unexpected;
  };
  api.wait_error = [&] {
    return "wait=" + std::to_string(wait_code) +
           " error=" + std::to_string(wait_error);
  };
  if (!BoundedGpuWait(api, ++ctx.fence_value, ctx.fault)) {
    err = ctx.fault.kind + ": " + ctx.fault.error +
          "; operation=" + ctx.last_operation;
    return false;
  }
  return true;
}

#endif // _WIN32
