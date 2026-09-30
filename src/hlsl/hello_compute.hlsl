// Hello compute shader — Fase 0.
// Each thread writes (SV_DispatchThreadID.x + 1) into a structured UAV.
// Compile: dxc -T cs_6_0 -E CSMain -Fo hello_compute.cso src/hlsl/hello_compute.hlsl
// English comments only. Not a benchmark. No tok/s. Not a console result.

RWStructuredBuffer<uint> Output : register(u0);

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    Output[dtid.x] = dtid.x + 1u;
}
