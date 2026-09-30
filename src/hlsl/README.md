# `src/hlsl/` — HLSL compute shaders

| File | Status | Notes |
| --- | --- | --- |
| `hello_compute.hlsl` | Fase 0 | `[numthreads(64, 1, 1)]` `CSMain` writes `Output[i] = i + 1`. |
| `matmul.hlsl` | Fase 1 | Naive row-major `C = A @ B`. Entry points: `CSMain` (= FP32), `CSMainFP32`, `CSMainFP16`. |

Hello compute is a real UAV write used to prove the DirectX 12 pipeline. It is **not** a benchmark and **not** a console result.

Matmul is a real compute shader (not a no-op). It is a **correctness** kernel for Fase 1 parity vs the CPU reference. It is **not** a tok/s result and **not** a console result. Research path: [docs/platform/dx12-hlsl-compute.md](../../docs/platform/dx12-hlsl-compute.md). Host: [docs/setup.md](../../docs/setup.md). CPU baseline: [docs/ggml-baseline.md](../../docs/ggml-baseline.md).

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
```

If `dxc` is missing, CI prints a clear skip notice. That is a missing-toolchain signal, not a green-wash of GPU work.

## Do not

- Claim tok/s or numerical quality from these files beyond the CPU-parity CSV.
- Assume CUDA or NVIDIA tooling. Xbox has no CUDA.
- Treat DirectML as a trainer. DirectML on console is inference/forward-focused; this repo uses DirectX 12 compute shaders.
