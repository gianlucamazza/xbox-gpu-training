// Real matmul compute shaders — Fase 1.
// C[M, N] = A[M, K] * B[K, N], row-major, one output element per thread.
//
// Compile (Windows SDK dxc, Shader Model 6.0):
//   dxc -T cs_6_0 -E CSMain     -Fo matmul.cso      src/hlsl/matmul.hlsl
//   dxc -T cs_6_0 -E CSMainFP32 -Fo matmul_fp32.cso src/hlsl/matmul.hlsl
//   dxc -T cs_6_0 -E CSMainFP16 -Fo matmul_fp16.cso src/hlsl/matmul.hlsl
//
// CSMain is an alias of CSMainFP32 so the existing CI compile
// (`dxc -T cs_6_0 -E CSMain`) stays valid. Do not rename that entry point.
//
// Element storage: one value per uint.
//   FP32 — IEEE binary32 bits (asfloat / asuint).
//   FP16 — IEEE binary16 in the low 16 bits (f16tof32 / f32tof16).
//           Stays on cs_6_0; does not require SM 6.2 native float16_t.
// Accumulation is FP32 for both entry points (matches the CPU reference).
//
// DirectX 12 compute shader path. No CUDA. DirectML is not the trainer.
// Not a tok/s result. Not a console result. English comments only.

cbuffer MatmulDims : register(b0)
{
    uint M;
    uint N;
    uint K;
    uint Unused;
};

StructuredBuffer<uint> A : register(t0); // [M, K]
StructuredBuffer<uint> B : register(t1); // [K, N]
RWStructuredBuffer<uint> C : register(u0); // [M, N]

float LoadA32(uint index)
{
    return asfloat(A[index]);
}

float LoadB32(uint index)
{
    return asfloat(B[index]);
}

float LoadA16(uint index)
{
    return f16tof32(A[index] & 0xffffu);
}

float LoadB16(uint index)
{
    return f16tof32(B[index] & 0xffffu);
}

void StoreC32(uint index, float value)
{
    C[index] = asuint(value);
}

void StoreC16(uint index, float value)
{
    C[index] = f32tof16(value);
}

void MatmulAccumulate(uint row, uint col, bool fp16)
{
    if (row >= M || col >= N)
    {
        return;
    }

    float acc = 0.0f;
    [loop]
    for (uint k = 0; k < K; ++k)
    {
        const uint ia = row * K + k;
        const uint ib = k * N + col;
        const float av = fp16 ? LoadA16(ia) : LoadA32(ia);
        const float bv = fp16 ? LoadB16(ib) : LoadB32(ib);
        acc += av * bv;
    }

    const uint ic = row * N + col;
    if (fp16)
    {
        StoreC16(ic, acc);
    }
    else
    {
        StoreC32(ic, acc);
    }
}

[numthreads(8, 8, 1)]
void CSMainFP32(uint3 dtid : SV_DispatchThreadID)
{
    // dtid.x = column (N), dtid.y = row (M)
    MatmulAccumulate(dtid.y, dtid.x, false);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    MatmulAccumulate(dtid.y, dtid.x, false);
}

[numthreads(8, 8, 1)]
void CSMainFP16(uint3 dtid : SV_DispatchThreadID)
{
    MatmulAccumulate(dtid.y, dtid.x, true);
}
