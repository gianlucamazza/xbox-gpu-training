// Ternary FakeQuant (absmean scale) — Fase 3.
// W_q[i] = s * clip(round(W[i] / s), -1, +1)
// Host supplies s = mean(|W|) (or 1 if that mean is 0). One thread per element.
//
// Compile (Windows SDK dxc, Shader Model 6.0):
//   dxc -T cs_6_0 -E CSMain -Fo fakequant_ternary.cso src\hlsl\fakequant_ternary.hlsl
//
// DirectX 12 compute shader path. No CUDA. DirectML is not the trainer.
// Not a tok/s result. Not a console result. English comments only.

cbuffer FakeQuantParams : register(b0)
{
    uint Count;
    uint ScaleBits; // asuint(s)
    uint Unused0;
    uint Unused1;
};

StructuredBuffer<uint> W : register(t0);      // master fp32 bits
RWStructuredBuffer<uint> Wq : register(u0);   // FakeQuant output

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
    const float nrm = w / s;
    float q = round(nrm);
    q = clamp(q, -1.0f, 1.0f);
    Wq[i] = asuint(q * s);
}
