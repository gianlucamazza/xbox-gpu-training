// RMSNorm compute shader — Fase 2.
// y[row, i] = x[row, i] * rsqrt(mean(x^2) + eps) * gamma[i]
//
// Compile (Windows SDK dxc, Shader Model 6.0):
//   dxc -T cs_6_0 -E CSMain -Fo rmsnorm.cso src\hlsl\rmsnorm.hlsl
//
// Uses 1.0 / sqrt(...) (not the hardware rsqrt intrinsic) so CPU/GPU
// parity stays honest on the tiny fixture. One thread per (row, col).
// Storage: one IEEE binary32 per uint (asfloat / asuint).
//
// DirectX 12 compute shader path. No CUDA. DirectML is not the trainer.
// Not a tok/s result. Not a console result. English comments only.

cbuffer RmsNormParams : register(b0)
{
    uint Count;    // rows
    uint Dim;      // columns
    uint EpsBits;  // asuint(eps)
    uint Unused;
};

StructuredBuffer<uint> X : register(t0);     // [Count, Dim]
StructuredBuffer<uint> Gamma : register(t1); // [Dim]
RWStructuredBuffer<uint> Y : register(u0);   // [Count, Dim]

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    const uint row = dtid.y;
    const uint col = dtid.x;
    if (row >= Count || col >= Dim)
    {
        return;
    }

    const float eps = asfloat(EpsBits);
    float acc = 0.0f;
    [loop]
    for (uint i = 0; i < Dim; ++i)
    {
        const float v = asfloat(X[row * Dim + i]);
        acc += v * v;
    }
    const float inv = 1.0f / sqrt(acc / (float)Dim + eps);
    const float g = asfloat(Gamma[col]);
    const float x = asfloat(X[row * Dim + col]);
    Y[row * Dim + col] = asuint(x * inv * g);
}
