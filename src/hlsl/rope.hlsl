// RoPE compute shader — Fase 2.
// Interleaved even/odd pairing (FloppyLM TinyGPT conceptual contract):
//   inv[p] = theta ^ -(2p / head_dim)
//   (x0, x1) = (x[..., 2p], x[..., 2p+1])
//   y0 = x0 * cos - x1 * sin
//   y1 = x0 * sin + x1 * cos
//
// Compile (Windows SDK dxc, Shader Model 6.0):
//   dxc -T cs_6_0 -E CSMain -Fo rope.cso src\hlsl\rope.hlsl
//
// Layout: X/Y are [Seq, Heads, HeadDim], row-major.
// One thread per (seq, head). Storage: IEEE binary32 per uint.
//
// DirectX 12 compute shader path. No CUDA. DirectML is not the trainer.
// Not a tok/s result. Not a console result. English comments only.

cbuffer RopeParams : register(b0)
{
    uint Seq;
    uint Heads;
    uint HeadDim;
    uint ThetaBits; // asuint(rope_theta), typically 10000
};

StructuredBuffer<uint> X : register(t0);
RWStructuredBuffer<uint> Y : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    const uint t = dtid.x;
    const uint h = dtid.y;
    if (t >= Seq || h >= Heads)
    {
        return;
    }

    const float theta = asfloat(ThetaBits);
    const uint base = (t * Heads + h) * HeadDim;
    [loop]
    for (uint p = 0; p < HeadDim / 2; ++p)
    {
        const float freq = pow(theta, -((float)(2 * p) / (float)HeadDim));
        const float ang = (float)t * freq;
        const float c = cos(ang);
        const float s = sin(ang);
        const float x0 = asfloat(X[base + 2 * p]);
        const float x1 = asfloat(X[base + 2 * p + 1]);
        Y[base + 2 * p] = asuint(x0 * c - x1 * s);
        Y[base + 2 * p + 1] = asuint(x0 * s + x1 * c);
    }
}
