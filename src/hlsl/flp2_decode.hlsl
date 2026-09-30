// FLP2 scalar decode — Fase 2.
// Reconstructs a weight matrix from packed symbols + per-row scales:
//   W[row, col] = (symbol - half) * scale[row]
//   half = (levels - 1) / 2
//
// Documented GPU-track contract (FloppyLM/xllama conceptual reference):
//   ternary levels=3 → symbols {0,1,2} → {-1, 0, +1} * scale
//   2-bit    levels=4 → midrise {±0.5, ±1.5} * scale
//   4-bit    levels=16 → midrise {±0.5 .. ±7.5} * scale
// Scale policy in this kernel: row16 (one finite non-negative scale per row).
//
// This kernel does NOT unpack a binary FLP2 envelope (magic/header/rANS).
// See docs/flp2-forward.md. Not a claim of xllama GPU train.
//
// Compile (Windows SDK dxc, Shader Model 6.0):
//   dxc -T cs_6_0 -E CSMain -Fo flp2_decode.cso src\hlsl\flp2_decode.hlsl
//
// DirectX 12 compute shader path. No CUDA. DirectML is not the trainer.
// Not a tok/s result. Not a console result. English comments only.

cbuffer DecodeParams : register(b0)
{
    uint Rows;
    uint Cols;
    uint Levels;
    uint Unused;
};

StructuredBuffer<uint> Symbols : register(t0); // [Rows, Cols], integer codes
StructuredBuffer<uint> Scales : register(t1);  // [Rows], asuint(float)
RWStructuredBuffer<uint> Weight : register(u0); // [Rows, Cols], asuint(float)

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    const uint col = dtid.x;
    const uint row = dtid.y;
    if (row >= Rows || col >= Cols)
    {
        return;
    }

    const float halfv = ((float)Levels - 1.0f) * 0.5f;
    const float scale = asfloat(Scales[row]);
    const float sym = (float)Symbols[row * Cols + col];
    Weight[row * Cols + col] = asuint((sym - halfv) * scale);
}
