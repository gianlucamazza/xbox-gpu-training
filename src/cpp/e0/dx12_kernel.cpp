#include "tensor.h"
#ifdef _WIN32
#include "../dx12_device.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>

namespace e0 {
namespace {
using Microsoft::WRL::ComPtr;
// Two timestamps per dispatch; a full heap forces an intermediate flush.
constexpr UINT kQueries = 2 * 8192;

struct GpuBuffer {
  ComPtr<ID3D12Resource> resource;
  size_t bytes = 0;
  bool upload = false;
  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
};
// Buffers are recycled instead of created per operation. Upload buffers that
// an unexecuted command list still reads are parked until the next flush;
// default buffers are reused immediately because every reuse is ordered by a
// state transition or UAV barrier in the same queue.
struct Pool {
  std::multimap<size_t, std::unique_ptr<GpuBuffer>> free_default, free_upload;
  std::vector<std::unique_ptr<GpuBuffer>> parked;
  bool recording = false, poisoned = false;
  void quarantine() {
    poisoned = true;
    for (auto &entry : free_default)
      entry.second->resource.Detach();
    for (auto &entry : free_upload)
      entry.second->resource.Detach();
    for (auto &entry : parked)
      entry->resource.Detach();
  }
  void release(std::unique_ptr<GpuBuffer> b) {
    if (poisoned) {
      b->resource.Detach();
      return;
    }
    if (b->upload && recording)
      parked.push_back(std::move(b));
    else
      (b->upload ? free_upload : free_default).emplace(b->bytes, std::move(b));
  }
  void unpark() {
    for (auto &b : parked)
      free_upload.emplace(b->bytes, std::move(b));
    parked.clear();
  }
};
struct Handle final : DeviceBuffer {
  std::unique_ptr<GpuBuffer> buffer;
  std::shared_ptr<Pool> pool;
  ~Handle() override {
    if (pool)
      pool->release(std::move(buffer));
  }
};

class GpuKernel final : public Kernel {
  Dx12Device ctx_;
  ComPtr<ID3D12RootSignature> root_;
  ComPtr<ID3D12PipelineState> pipeline_;
  ComPtr<ID3D12QueryHeap> timestamps_;
  ComPtr<ID3D12Resource> timing_, readback_, dummy_;
  size_t readback_bytes_ = 0;
  uint64_t frequency_ = 0;
  UINT queries_ = 0;
  std::shared_ptr<Pool> pool_ = std::make_shared<Pool>();

public:
  using Kernel::read;
  using Kernel::run;
  explicit GpuKernel(const std::string &shader) {
    auto result = CreateDx12Device();
    if (!result.ok)
      throw std::runtime_error("BLOCKED: " + result.message);
    ctx_ = std::move(result.ctx);
    try {
      if (ctx_.warp)
        throw std::runtime_error("BLOCKED: WARP is not a hardware GPU");
      check(ctx_.queue->GetTimestampFrequency(&frequency_),
            "timestamp frequency");
      D3D12_QUERY_HEAP_DESC query{};
      query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
      query.Count = kQueries;
      check(ctx_.device->CreateQueryHeap(&query, IID_PPV_ARGS(&timestamps_)),
            "timestamp heap");
      timing_ = create(kQueries * sizeof(uint64_t), D3D12_HEAP_TYPE_READBACK,
                       D3D12_RESOURCE_STATE_COPY_DEST);
      dummy_ =
          create(4, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
      D3D12_ROOT_PARAMETER parameters[7]{};
      parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
      parameters[0].Constants = {0, 0, 11};
      for (UINT i = 0; i < 5; ++i) {
        parameters[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[i + 1].Descriptor = {i, 0};
      }
      parameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
      parameters[6].Descriptor = {0, 0};
      D3D12_ROOT_SIGNATURE_DESC desc{};
      desc.NumParameters = 7;
      desc.pParameters = parameters;
      ComPtr<ID3DBlob> blob, error;
      check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                        &blob, &error),
            "root serialization");
      check(ctx_.device->CreateRootSignature(0, blob->GetBufferPointer(),
                                             blob->GetBufferSize(),
                                             IID_PPV_ARGS(&root_)),
            "root creation");
      std::ifstream file(shader, std::ios::binary);
      if (!file)
        throw std::runtime_error("missing precompiled E0 shader: " + shader);
      std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
      D3D12_COMPUTE_PIPELINE_STATE_DESC state{};
      state.pRootSignature = root_.Get();
      state.CS = {bytes.data(), bytes.size()};
      check(ctx_.device->CreateComputePipelineState(&state,
                                                    IID_PPV_ARGS(&pipeline_)),
            "E0 pipeline");
    } catch (...) {
      DestroyDx12Device(ctx_);
      throw;
    }
  }
  ~GpuKernel() override {
    if (!poisoned()) {
      try {
        flush();
      } catch (...) {
      }
    }
    if (poisoned()) {
      pool_->quarantine();
      root_.Detach();
      pipeline_.Detach();
      timestamps_.Detach();
      timing_.Detach();
      readback_.Detach();
      dummy_.Detach();
    }
    pool_.reset();
    DestroyDx12Device(ctx_);
  }
  void inject_runtime_fault(const std::string &kind) override {
    require_healthy();
    ctx_.fault_probe = kind;
  }
  bool hardware() const override { return true; }
  std::string adapter() const override { return ctx_.adapter_name; }

  Tensor run(const Command &p, const Tensor &x, const Tensor &w,
             const Tensor &z, const Tensor &y, const Tensor &dy) override {
    require_healthy();
    ctx_.last_operation = "tensor op " + std::to_string(uint32_t(p.op));
    static_assert(sizeof(Command) == 44, "HLSL constant layout");
    if (!p.count)
      throw std::runtime_error("empty E0 dispatch");
    const uint64_t elements = p.op == Op::Softmax ? p.rows : p.count;
    const uint64_t groups = (elements + 63) / 64;
    if (!groups || groups > 65535)
      throw std::runtime_error("E0 tensor exceeds one dispatch dimension");
    if (queries_ + 2 > kQueries)
      flush();
    std::array<const Tensor *, 5> inputs{&x, &w, &z, &y, &dy};
    std::array<GpuBuffer *, 5> sources{};
    for (size_t i = 0; i < 5; ++i)
      sources[i] = *inputs[i] ? resident(**inputs[i]) : nullptr;
    auto output = acquire(p.count * sizeof(float), false);
    begin();
    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    for (auto *b : sources)
      if (b && !b->upload)
        transition(*b, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   barriers);
    transition(*output->buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               barriers);
    if (!barriers.empty())
      ctx_.list->ResourceBarrier(UINT(barriers.size()), barriers.data());
    ctx_.list->SetComputeRootSignature(root_.Get());
    ctx_.list->SetComputeRoot32BitConstants(0, 11, &p, 0);
    for (UINT i = 0; i < 5; ++i)
      ctx_.list->SetComputeRootShaderResourceView(
          i + 1,
          (sources[i] ? sources[i]->resource : dummy_)->GetGPUVirtualAddress());
    ctx_.list->SetComputeRootUnorderedAccessView(
        6, output->buffer->resource->GetGPUVirtualAddress());
    ctx_.list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                        queries_++);
    ctx_.list->Dispatch(UINT(groups), 1, 1);
    D3D12_RESOURCE_BARRIER uav{};
    uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    ctx_.list->ResourceBarrier(1, &uav);
    ctx_.list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                        queries_++);
    ++dispatches;
    auto result = std::make_shared<Storage>();
    result->count = p.count;
    result->device = std::move(output);
    return result;
  }

  std::vector<Values> read(const std::vector<Tensor> &tensors) override {
    require_healthy();
    std::vector<Values> out(tensors.size());
    std::vector<size_t> offsets(tensors.size(), SIZE_MAX);
    size_t total = 0;
    for (size_t i = 0; i < tensors.size(); ++i) {
      const auto &t = tensors[i];
      if (!t)
        continue;
      if (t->on_host)
        out[i] = t->host;
      else {
        offsets[i] = total;
        total += t->count * sizeof(float);
      }
    }
    if (total) {
      if (total > readback_bytes_) {
        readback_ = create(total, D3D12_HEAP_TYPE_READBACK,
                           D3D12_RESOURCE_STATE_COPY_DEST);
        readback_bytes_ = total;
      }
      begin();
      for (size_t i = 0; i < tensors.size(); ++i) {
        if (offsets[i] == SIZE_MAX)
          continue;
        auto &b = buffer(*tensors[i]);
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        transition(b, D3D12_RESOURCE_STATE_COPY_SOURCE, barriers);
        if (!barriers.empty())
          ctx_.list->ResourceBarrier(UINT(barriers.size()), barriers.data());
        ctx_.list->CopyBufferRegion(readback_.Get(), offsets[i],
                                    b.resource.Get(), 0,
                                    tensors[i]->count * sizeof(float));
      }
    }
    flush();
    if (total) {
      void *mapped = nullptr;
      D3D12_RANGE range{0, total};
      check(readback_->Map(0, &range, &mapped), "output map");
      for (size_t i = 0; i < tensors.size(); ++i)
        if (offsets[i] != SIZE_MAX) {
          out[i].resize(tensors[i]->count);
          std::memcpy(out[i].data(),
                      static_cast<const char *>(mapped) + offsets[i],
                      out[i].size() * sizeof(float));
        }
      D3D12_RANGE no_write{0, 0};
      readback_->Unmap(0, &no_write);
      transfer_bytes += total;
    }
    return out;
  }

private:
  void check(HRESULT hr, const char *action) {
    if (SUCCEEDED(hr))
      return;
    const std::string message = std::string(action) + ": " + HrHex(hr);
    const auto removed =
        ctx_.device ? ctx_.device->GetDeviceRemovedReason() : S_OK;
    if (FAILED(removed)) {
      ctx_.fault = {"gpu_device_removed",
                    message + "; removed=" + HrHex(removed), ctx_.fence_value,
                    completed_fence, 0};
      runtime_fault = ctx_.fault;
      pool_->quarantine();
    }
    throw std::runtime_error(message);
  }
  ComPtr<ID3D12Resource> create(size_t bytes, D3D12_HEAP_TYPE type,
                                D3D12_RESOURCE_STATES state, bool uav = false) {
    require_healthy();
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = std::max<size_t>(bytes, 4);
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (uav)
      desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> resource;
    check(ctx_.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                                               &desc, state, nullptr,
                                               IID_PPV_ARGS(&resource)),
          "E0 buffer");
    return resource;
  }
  std::shared_ptr<Handle> acquire(size_t bytes, bool upload) {
    require_healthy();
    bytes = (std::max<size_t>(bytes, 4) + 255) & ~size_t(255);
    auto &free = upload ? pool_->free_upload : pool_->free_default;
    auto handle = std::make_shared<Handle>();
    handle->pool = pool_;
    auto it = free.find(bytes);
    if (it != free.end()) {
      handle->buffer = std::move(it->second);
      free.erase(it);
    } else {
      handle->buffer = std::make_unique<GpuBuffer>();
      handle->buffer->bytes = bytes;
      handle->buffer->upload = upload;
      handle->buffer->state = upload ? D3D12_RESOURCE_STATE_GENERIC_READ
                                     : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      handle->buffer->resource = create(
          bytes, upload ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT,
          handle->buffer->state, !upload);
    }
    return handle;
  }
  static GpuBuffer &buffer(const Storage &t) {
    return *static_cast<Handle &>(*t.device).buffer;
  }
  // Uploads host data once; later uses of the same tensor reuse it.
  GpuBuffer *resident(Storage &t) {
    if (!t.device) {
      if (!t.on_host)
        throw std::runtime_error("E0 tensor has no data");
      auto handle = acquire(t.count * sizeof(float), true);
      void *mapped = nullptr;
      D3D12_RANGE no_read{0, 0};
      check(handle->buffer->resource->Map(0, &no_read, &mapped), "input map");
      if (t.count)
        std::memcpy(mapped, t.host.data(), t.count * sizeof(float));
      handle->buffer->resource->Unmap(0, nullptr);
      transfer_bytes += t.count * sizeof(float);
      t.device = std::move(handle);
    }
    return &buffer(t);
  }
  static void transition(GpuBuffer &b, D3D12_RESOURCE_STATES after,
                         std::vector<D3D12_RESOURCE_BARRIER> &barriers) {
    if (b.upload || b.state == after)
      return;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = b.resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = b.state;
    barrier.Transition.StateAfter = after;
    barriers.push_back(barrier);
    b.state = after;
  }
  void begin() {
    require_healthy();
    if (pool_->recording)
      return;
    check(ctx_.allocator->Reset(), "allocator reset");
    check(ctx_.list->Reset(ctx_.allocator.Get(), pipeline_.Get()),
          "list reset");
    pool_->recording = true;
    queries_ = 0;
  }
  // The only synchronisation point: executes recorded work and accumulates
  // per-dispatch GPU timestamps.
  void flush() {
    require_healthy();
    if (!pool_->recording)
      return;
    if (queries_)
      ctx_.list->ResolveQueryData(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                  0, queries_, timing_.Get(), 0);
    pool_->recording = false;
    check(ctx_.list->Close(), "list close");
    ID3D12CommandList *lists[] = {ctx_.list.Get()};
    ctx_.queue->ExecuteCommandLists(1, lists);
    std::string error;
    if (!WaitForGpu(ctx_, error)) {
      runtime_fault = ctx_.fault;
      runtime_fault.error = error;
      pool_->quarantine();
      throw std::runtime_error(error);
    }
    completed_fence = ctx_.fence_value;
    if (gpu_progress)
      gpu_progress(completed_fence, ctx_.last_operation);
    pool_->unpark();
    if (queries_) {
      uint64_t *stamps = nullptr;
      D3D12_RANGE range{0, queries_ * sizeof(uint64_t)};
      check(timing_->Map(0, &range, reinterpret_cast<void **>(&stamps)),
            "timestamp readback");
      for (UINT i = 0; i + 1 < queries_; i += 2)
        gpu_seconds += double(stamps[i + 1] - stamps[i]) / double(frequency_);
      D3D12_RANGE no_write{0, 0};
      timing_->Unmap(0, &no_write);
      queries_ = 0;
    }
    observe();
  }
};
} // namespace
std::unique_ptr<Kernel> make_gpu(const std::string &shader) {
  return std::make_unique<GpuKernel>(shader);
}
} // namespace e0
#endif
