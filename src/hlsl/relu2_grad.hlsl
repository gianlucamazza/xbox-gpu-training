// relu2 backward — Fase 3.
// h = max(pre, 0)^2
// dpre = 2 * max(pre, 0) * dh
// One thread per element. Matches the Fase 2 tiny-fixture MLP activation.
//
// Compile (Windows SDK dxc, Shader Model 6.0):
//   dxc -T cs_6_0 -E CSMain -Fo relu2_grad.cso src\hlsl\relu2_grad.hlsl
//
// DirectX 12 compute shader path. No CUDA. DirectML is not the trainer.
// Not a tok/s result. Not a console result. English comments only.

cbuffer Relu2GradParams : register(b0)
{
    uint Count;
    uint Unused0;
    uint Unused1;
    uint Unused2;
};

StructuredBuffer<uint> Pre : register(t0);
StructuredBuffer<uint> DH : register(t1);
RWStructuredBuffer<uint> DPre : register(u0);

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    const uint i = dtid.x;
    if (i >= Count)
    {
        return;
    }

    const float pre = asfloat(Pre[i]);
    const float dh = asfloat(DH[i]);
    const float dpre = (pre > 0.0f ? 2.0f * pre : 0.0f) * dh;
    DPre[i] = asuint(dpre);
}
