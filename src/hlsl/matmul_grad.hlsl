// Weight gradient for the tiny-net linear — Fase 3.
// dW[o, i] = Σ_b dy[b, o] * x[b, i]
// W is [Out, In] row-major (same contraction as cpu_ste LinearWeightGrad).
// One thread per (out, in).
//
// Compile (Windows SDK dxc, Shader Model 6.0):
//   dxc -T cs_6_0 -E CSMain -Fo matmul_grad.cso src\hlsl\matmul_grad.hlsl
//
// DirectX 12 compute shader path. No CUDA. DirectML is not the trainer.
// Not a tok/s result. Not a console result. English comments only.

cbuffer MatmulGradParams : register(b0)
{
    uint B;
    uint OutF;
    uint InF;
    uint Unused;
};

StructuredBuffer<uint> DY : register(t0); // [B, Out]
StructuredBuffer<uint> X : register(t1);  // [B, In]
RWStructuredBuffer<uint> DW : register(u0); // [Out, In]

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    const uint i = dtid.x; // in
    const uint o = dtid.y; // out
    if (o >= OutF || i >= InF)
    {
        return;
    }

    float acc = 0.0f;
    [loop]
    for (uint b = 0; b < B; ++b)
    {
        const float dy = asfloat(DY[b * OutF + o]);
        const float x = asfloat(X[b * InF + i]);
        acc += dy * x;
    }
    DW[o * InF + i] = asuint(acc);
}
