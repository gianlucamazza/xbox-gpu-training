# `src/hlsl/` — HLSL compute shaders

| File | Status | Notes |
| --- | --- | --- |
| `hello_compute.hlsl` | Fase 0 | `[numthreads(64, 1, 1)]` `CSMain` writes `Output[i] = i + 1`. |
| `matmul.hlsl` | Fase 1 | Naive row-major `C = A @ B`. Entry points: `CSMain` (= FP32), `CSMainFP32`, `CSMainFP16`. |
| `rmsnorm.hlsl` | Fase 2 | RMSNorm, `1/sqrt(mean(x^2)+eps) * gamma`. |
| `rope.hlsl` | Fase 2 | RoPE even/odd pairing, `theta=10000`. |
| `flp2_decode.hlsl` | Fase 2 | Scalar decode `(sym-half)*row_scale` (ternary / 2-bit / 4-bit). |
| `flp2_forward.hlsl` | Fase 2 | Tiny 1-layer relu2 forward from packed symbols. |
| `fakequant_ternary.hlsl` | Fase 3 | `s * clip(round(W/s), -1, +1)` (host supplies absmean `s`). |
| `matmul_grad.hlsl` | Fase 3 | `dW[o,i] = Σ_b dy[b,o] * x[b,i]`. |
| `relu2_grad.hlsl` | Fase 3 | `dpre = 2 * max(pre, 0) * dh`. |
| `ste_backward.hlsl` | Fase 3 | `dW = dW_q * 1_{|W/s| ≤ 1}`. |

Hello compute is a real UAV write used to prove the DirectX 12 pipeline. It is **not** a benchmark and **not** a console result.

Matmul is a real compute shader (not a no-op). It is a **correctness** kernel for Fase 1 parity vs the CPU reference. It is **not** a tok/s result and **not** a console result. Research path: [docs/platform/dx12-hlsl-compute.md](../../docs/platform/dx12-hlsl-compute.md). Host: [docs/setup.md](../../docs/setup.md). CPU baseline: [docs/ggml-baseline.md](../../docs/ggml-baseline.md).

Fase 2 shaders are real correctness kernels for RMSNorm, RoPE, and scalar FLP2 decode / tiny forward. Contract: [docs/flp2-forward.md](../../docs/flp2-forward.md). They do **not** unpack a binary FLP2 envelope.

Fase 3 shaders are real correctness kernels for ternary FakeQuant, linear weight grad, relu2 grad, and the STE clip mask. AdamW stays on the host (master fp32). Contract: [docs/ste-adamw.md](../../docs/ste-adamw.md), [docs/adr/0002-ste-qat-mapping.md](../../docs/adr/0002-ste-qat-mapping.md).

Fase 4 adds **no new HLSL**. Streaming is a host + optional `CopyBufferRegion` path: [docs/memory-budget.md](../../docs/memory-budget.md).

## Entry points (`matmul.hlsl`)

| Entry | Precision | Thread group | Storage |
| --- | --- | --- | --- |
| `CSMain` | FP32 | `[numthreads(8, 8, 1)]` | `uint` holds `asuint(float)` |
| `CSMainFP32` | FP32 | same | same as `CSMain` |
| `CSMainFP16` | FP16 | same | IEEE binary16 in the low 16 bits of each `uint` |

`CSMain` exists so `dxc -T cs_6_0 -E CSMain` (CI job `build-windows`) keeps compiling. Do not remove it.

`dtid.x` is the output column (`N`), `dtid.y` is the output row (`M`). Threads outside `M`×`N` return. Accumulation is FP32 for both precisions.

## Compile (Windows SDK `dxc`)

```bat
dxc -T cs_6_0 -E CSMain     -Fo hello_compute.cso src\hlsl\hello_compute.hlsl
dxc -T cs_6_0 -E CSMain     -Fo matmul.cso        src\hlsl\matmul.hlsl
dxc -T cs_6_0 -E CSMainFP32 -Fo matmul_fp32.cso   src\hlsl\matmul.hlsl
dxc -T cs_6_0 -E CSMainFP16 -Fo matmul_fp16.cso   src\hlsl\matmul.hlsl
dxc -T cs_6_0 -E CSMain     -Fo rmsnorm.cso       src\hlsl\rmsnorm.hlsl
dxc -T cs_6_0 -E CSMain     -Fo rope.cso          src\hlsl\rope.hlsl
dxc -T cs_6_0 -E CSMain     -Fo flp2_decode.cso   src\hlsl\flp2_decode.hlsl
dxc -T cs_6_0 -E CSMain     -Fo flp2_forward.cso      src\hlsl\flp2_forward.hlsl
dxc -T cs_6_0 -E CSMain     -Fo fakequant_ternary.cso src\hlsl\fakequant_ternary.hlsl
dxc -T cs_6_0 -E CSMain     -Fo matmul_grad.cso       src\hlsl\matmul_grad.hlsl
dxc -T cs_6_0 -E CSMain     -Fo relu2_grad.cso        src\hlsl\relu2_grad.hlsl
dxc -T cs_6_0 -E CSMain     -Fo ste_backward.cso      src\hlsl\ste_backward.hlsl
```

If `dxc` is missing, CI prints a clear skip notice. That is a missing-toolchain signal, not a green-wash of GPU work.

## Do not

- Claim tok/s or numerical quality from these files beyond the CPU-parity CSV.
- Assume CUDA or NVIDIA tooling. Xbox has no CUDA.
- Treat DirectML as a trainer. DirectML on console is inference/forward-focused; this repo uses DirectX 12 compute shaders.
