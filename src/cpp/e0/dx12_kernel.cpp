#include "tensor.h"
#ifdef _WIN32
#include "../dx12_device.h"
#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace e0 {
namespace {
using Microsoft::WRL::ComPtr;
void check(HRESULT hr,const char* action) {
  if(FAILED(hr)) throw std::runtime_error(std::string(action)+": "+HrHex(hr));
}
class GpuKernel final : public Kernel {
  Dx12Device ctx_;
  ComPtr<ID3D12RootSignature> root_;
  ComPtr<ID3D12PipelineState> pipeline_;
public:
  explicit GpuKernel(const std::string& shader) {
    auto result=CreateDx12Device();
    if(!result.ok) throw std::runtime_error("BLOCKED: "+result.message);
    ctx_=std::move(result.ctx);
    try {
      if(ctx_.warp) throw std::runtime_error("BLOCKED: WARP is not a hardware GPU");
      D3D12_ROOT_PARAMETER parameters[7]{};
      parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
      parameters[0].Constants={0,0,11};
      for(UINT i=0;i<5;++i) { parameters[i+1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV; parameters[i+1].Descriptor={i,0}; }
      parameters[6].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
      parameters[6].Descriptor={0,0};
      D3D12_ROOT_SIGNATURE_DESC desc{}; desc.NumParameters=7; desc.pParameters=parameters;
      ComPtr<ID3DBlob> blob,error;
      check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"root serialization");
      check(ctx_.device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root_)),"root creation");
      std::ifstream file(shader,std::ios::binary);
      if(!file) throw std::runtime_error("missing precompiled E0 shader: "+shader);
      std::vector<char> bytes((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
      D3D12_COMPUTE_PIPELINE_STATE_DESC state{}; state.pRootSignature=root_.Get(); state.CS={bytes.data(),bytes.size()};
      check(ctx_.device->CreateComputePipelineState(&state,IID_PPV_ARGS(&pipeline_)),"E0 pipeline");
    } catch(...) { DestroyDx12Device(ctx_); throw; }
  }
  ~GpuKernel() override { DestroyDx12Device(ctx_); }
  bool hardware() const override { return true; }
  std::string adapter() const override { return ctx_.adapter_name; }
  ComPtr<ID3D12Resource> buffer(size_t bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,bool uav=false) {
    D3D12_HEAP_PROPERTIES heap{}; heap.Type=type;
    D3D12_RESOURCE_DESC desc{}; desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width=std::max<size_t>(bytes,4); desc.Height=1; desc.DepthOrArraySize=1;
    desc.MipLevels=1; desc.SampleDesc.Count=1; desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(uav) desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> resource;
    check(ctx_.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&resource)),"E0 buffer");
    return resource;
  }
  Values run(const Command& p,const Values& x,const Values& w,const Values& z,const Values& y,const Values& dy) override {
    static_assert(sizeof(Command)==44,"HLSL constant layout");
    if(!p.count) throw std::runtime_error("empty E0 dispatch");
    std::array<const Values*,5> inputs{&x,&w,&z,&y,&dy};
    std::array<ComPtr<ID3D12Resource>,5> uploads;
    for(size_t i=0;i<5;++i) {
      uploads[i]=buffer(inputs[i]->size()*sizeof(float),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
      void* mapped=nullptr; D3D12_RANGE no_read{0,0};
      check(uploads[i]->Map(0,&no_read,&mapped),"input map");
      if(!inputs[i]->empty()) std::memcpy(mapped,inputs[i]->data(),inputs[i]->size()*sizeof(float));
      uploads[i]->Unmap(0,nullptr);
    }
    auto output=buffer(p.count*sizeof(float),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);
    auto readback=buffer(p.count*sizeof(float),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    check(ctx_.allocator->Reset(),"allocator reset");
    check(ctx_.list->Reset(ctx_.allocator.Get(),pipeline_.Get()),"list reset");
    ctx_.list->SetComputeRootSignature(root_.Get());
    ctx_.list->SetComputeRoot32BitConstants(0,11,&p,0);
    for(UINT i=0;i<5;++i) ctx_.list->SetComputeRootShaderResourceView(i+1,uploads[i]->GetGPUVirtualAddress());
    ctx_.list->SetComputeRootUnorderedAccessView(6,output->GetGPUVirtualAddress());
    const UINT groups=((p.op==Op::Softmax?p.rows:p.count)+63)/64;
    if(groups>65535) throw std::runtime_error("E0 tensor exceeds one dispatch dimension");
    ctx_.list->Dispatch(groups,1,1);
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource=output.Get(); barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    ctx_.list->ResourceBarrier(1,&barrier);
    ctx_.list->CopyResource(readback.Get(),output.Get());
    check(ctx_.list->Close(),"list close");
    ID3D12CommandList* lists[]={ctx_.list.Get()}; ctx_.queue->ExecuteCommandLists(1,lists);
    std::string error; if(!WaitForGpu(ctx_,error)) throw std::runtime_error(error);
    void* mapped=nullptr; D3D12_RANGE range{0,p.count*sizeof(float)};
    check(readback->Map(0,&range,&mapped),"output map");
    Values result(p.count); std::memcpy(result.data(),mapped,result.size()*sizeof(float));
    D3D12_RANGE no_write{0,0}; readback->Unmap(0,&no_write); ++dispatches;
    return result;
  }
};
}
std::unique_ptr<Kernel> make_gpu(const std::string& shader) { return std::make_unique<GpuKernel>(shader); }
}
#endif
