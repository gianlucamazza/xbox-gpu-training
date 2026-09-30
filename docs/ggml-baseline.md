# CPU ggml baseline (Fase 1)

How this repository compares HLSL matmul to a **CPU** reference. ggml is **not vendored** in this tree.

## Decision

Fase 1 uses a **portable CPU GEMM** (`src/cpp/cpu_matmul.*`) as the numerical baseline:

- Layout: row-major `C[M, N] = A[M, K] * B[K, N]`
- FP32: naive triple loop, FP32 accumulation
- FP16: IEEE binary16 elements, **FP32 accumulation**, convert back (matches `CSMainFP16`)
- Interface: `CpuMatmulFP32` / `CpuMatmulFP16` plus `CompareFP32` / `CompareFP16`

[ggml](https://github.com/ggml-org/ggml) is **not** a git submodule and is **not** compiled here. ADR 0001 already allowed “vendored or interfaced later”; this note records the interface so a later drop-in can wrap `ggml_mul_mat` without changing the CSV contract.

## Why not vendor ggml in Fase 1

- ggml is a large tree with its own build. Vendoring it would couple Linux `lint-docs` / host smoke and Windows `build-windows` to that build.
- The Fase 1 gate is **numerical parity** of a tiny tile, not ggml performance.
- A naive GEMM on 8–32 sized tiles is enough to catch a no-op or transposed HLSL kernel.
- DirectML is **not** used as this baseline or as the trainer.

## How to compare (and how to swap ggml in later)

1. Host builds a fixture (`A`, `B`) with a documented layout.
2. CPU reference writes `C_ref` (`CpuMatmulFP32` / `CpuMatmulFP16` today).
3. On a D3D12 device, `src/hlsl/matmul.hlsl` writes `C_gpu`.
4. Harness records **max-abs** and **max-rel** into `benchmarks/results/matmul.csv`.

A future ggml vendor would replace the body of `CpuMatmulFP32` (and an FP16 path if used) and keep:

- the same `MatmulShape` / pointer contract
- the same CSV schema (`xbox-gpu-training.benchmark.matmul.v1`)
- the same tolerances unless an ADR changes them

`ggml_mul_mat` uses ggml’s own dimension/transpose conventions. Any wrapper must **normalize** to row-major `C = A @ B` before comparing to the HLSL UAV.

## Chosen tolerances (TBD written down)

| Precision | max-abs | max-rel | Pass rule |
| --- | --- | --- | --- |
| FP32 | `1e-4` | `1e-3` | both must hold |
| FP16 | `5e-2` | `5e-2` | both must hold |

Relative error is `|actual - ref| / max(|ref|, floor)` with floor `1e-8` (FP32) or `1e-4` (FP16).

These are **Fase 1 smoke tolerances** for small fixtures with values in about `[-1, 1]`, not a claim about ggml, DirectML, or Xbox Series S|X hardware.

## What this is not

- Not a tok/s number.
- Not a console bench (Fase 6).
- Not a ggml performance study.
- Not a DirectML trainer path.

Host commands: `xbox_gpu_host --cpu-ref` and `xbox_gpu_host --bench matmul --out benchmarks/results/matmul.csv`.
