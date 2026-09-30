# `src/hlsl/` — HLSL compute shaders

Placeholder compute shader sources for the Xbox Series S|X GPU research track.

| File | Status | Notes |
| --- | --- | --- |
| `hello_compute.hlsl` | stub | No-op dispatch. Fase 0 hello-compute. |
| `matmul.hlsl` | stub | FP16/FP32 matmul placeholder. Fase 1 replaces this. |

These files are **not** production kernels. They exist so CI can compile HLSL with `dxc` when the Windows SDK is present, and so later phases have a stable folder.

## Compile (Windows SDK `dxc`)

```bat
dxc -T cs_6_0 -E CSMain -Fo hello_compute.cso src\hlsl\hello_compute.hlsl
dxc -T cs_6_0 -E CSMain -Fo matmul.cso src\hlsl\matmul.hlsl
```

If `dxc` is missing, CI prints a clear skip notice. That is not a green-wash of GPU work — it is a missing toolchain signal.

## Do not

- Claim tok/s or numerical quality from these stubs.
- Assume CUDA or NVIDIA tooling.
- Treat DirectML as a trainer. DirectML on console is inference/forward-focused; this repo uses DirectX 12 compute shaders.
