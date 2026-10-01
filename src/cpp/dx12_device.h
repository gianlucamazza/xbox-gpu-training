#pragma once

#include "gpu_wait.h"
#include <string>

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

struct Dx12Device {
  Microsoft::WRL::ComPtr<ID3D12Device> device;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence;
  HANDLE fence_event = nullptr;
  UINT64 fence_value = 0;
  std::string adapter_name;
  bool warp = false;
  GpuRuntimeFault fault;
  std::string last_operation = "command queue submission";
  std::string fault_probe;
};

struct Dx12CreateResult {
  bool ok = false;
  bool blocked = false;
  std::string message;
  Dx12Device ctx;
};

Dx12CreateResult CreateDx12Device();
void DestroyDx12Device(Dx12Device &ctx);
bool WaitForGpu(Dx12Device &ctx, std::string &err);
std::string HrHex(long hr);

#endif // _WIN32
