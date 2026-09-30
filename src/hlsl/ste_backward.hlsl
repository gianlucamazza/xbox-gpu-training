// STE mask — Fase 3.
// dW_master[i] = dW_q[i] * 1_{|W[i] / s| ≤ 1}
// Scale s is a host constant (no gradient through absmean). ADR 0002.
//
// Compile (Windows SDK dxc, Shader Model 6.0):
//   dxc -T cs_6_0 -E CSMain -Fo ste_backward.cso src\hlsl\ste_backward.hlsl
//
// DirectX 12 compute shader path. No CUDA. DirectML is not the trainer.
// Not a tok/s result. Not a console result. English comments only.

cbuffer SteParams : register(b0)
{
    uint Count;
    uint ScaleBits; // asuint(s)
    uint Unused0;
    uint Unused1;
};

StructuredBuffer<uint> W : register(t0);    // master fp32
StructuredBuffer<uint> DWq : register(t1);  // grad w.r.t. FakeQuant output
RWStructuredBuffer<uint> DW : register(u0); // STE grad on master

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    const uint i = dtid.x;
    if (i >= Count)
    {
        return;
    }

    const float s = asfloat(ScaleBits);
    const float w = asfloat(W[i]);
    const float dwq = asfloat(DWq[i]);
    const float nrm = w / s;
    const float mask = abs(nrm) <= 1.0f ? 1.0f : 0.0f;
    DW[i] = asuint(dwq * mask);
}
