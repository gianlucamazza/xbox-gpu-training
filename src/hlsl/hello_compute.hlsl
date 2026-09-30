// STUB compute shader — Fase 0 hello dispatch.
// English comments only. Intentionally a no-op. Not a performance claim.

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    // No UAV writes. Host may dispatch this to prove a DirectX 12 pipeline exists.
    (void)dtid;
}
