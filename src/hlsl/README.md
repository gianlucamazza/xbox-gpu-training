# `src/hlsl/` — HLSL compute shaders

| File | Status | Notes |
| --- | --- | --- |
| `hello_compute.hlsl` | Fase 0 | `[numthreads(64, 1, 1)]` `CSMain` writes `Output[i] = i + 1`. |
| `matmul.hlsl` | stub | FP16/FP32 matmul placeholder. Fase 1 replaces this. |

Hello compute is a real UAV write used to prove the DirectX 12 pipeline. It is **not** a benchmark and **not** a console result. Research path: [docs/platform/dx12-hlsl-compute.md](../../docs/platform/dx12-hlsl-compute.md). Host: [docs/setup.md](../../docs/setup.md).

## Compile (Windows SDK `dxc`)

```bat
dxc -T cs_6_0 -E CSMain -Fo hello_compute.cso src\hlsl\hello_compute.hlsl
dxc -T cs_6_0 -E CSMain -Fo matmul.cso src\hlsl\matmul.hlsl
```

If `dxc` is missing, CI prints a clear skip notice. That is a missing-toolchain signal, not a green-wash of GPU work.

## Do not

- Claim tok/s or numerical quality from these files.
- Assume CUDA or NVIDIA tooling. Xbox has no CUDA.
- Treat DirectML as a trainer. DirectML on console is inference/forward-focused; this repo uses DirectX 12 compute shaders.
