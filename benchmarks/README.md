# `benchmarks/`

Honest measurement only. This folder does **not** contain measured Xbox GPU tok/s or quality numbers.

## Current status

| Harness | Output | Meaning |
| --- | --- | --- |
| `python3 benchmarks/run_smoke.py` | `results/smoke.json` | Workflow **Benchmark** stub (`status: stub`). No tok/s. |
| `xbox_gpu_host --bench matmul --out results/matmul.csv` | `results/matmul.csv` | Fase 1 matmul vs CPU reference. |
| `python3 benchmarks/run_matmul.py` | same CSV | Finds the host and runs `--bench matmul`; writes `status=blocked` if the host is missing. |
| `xbox_gpu_host --forward-fixture fixtures/tiny_flp2.json` | stdout | Fase 2 CPU fixture match; GPU dispatch only on D3D12. |
| `python3 benchmarks/gen_tiny_flp2.py` | `fixtures/tiny_flp2.json` | Regenerates the tiny fixture (not a tok/s result). |
| `xbox_gpu_host --stream-stress --budget-mb 1024` | stdout | Fase 4 chunk stream + double buffer. Peak working-set vs App ~1 GB planning. Console AppContainer unvalidated. |

Never invent tok/s, latency-as-quality, or console scores. `gpu_dispatch_ms` is a **measured host wall** (upload + dispatch + readback) only when a D3D12 device actually ran. Empty / omitted GPU time means the GPU path did not run.

## CSV schema (`xbox-gpu-training.benchmark.matmul.v1`)

Header:

```
schema,status,precision,M,N,K,max_abs_error,max_rel_error,tol_abs,tol_rel,parity,cpu_ms,gpu_dispatch_ms,device,adapter,note
```

| Column | Meaning |
| --- | --- |
| `schema` | `xbox-gpu-training.benchmark.matmul.v1` |
| `status` | `ok` (GPU row compared), `blocked` (no device / no shader / no host), `failed`, `cpu-only` |
| `precision` | `fp32` or `fp16` |
| `M,N,K` | Tile shape for `C[M,N] = A[M,K] * B[K,N]` |
| `max_abs_error` / `max_rel_error` | vs CPU reference (empty if not compared) |
| `tol_abs` / `tol_rel` | Fase 1 TBD: FP32 `1e-4` / `1e-3`; FP16 `5e-2` / `5e-2` |
| `parity` | `pass`, `fail`, or `n/a` |
| `cpu_ms` | Measured CPU GEMM wall for that tile (empty if CPU did not run) |
| `gpu_dispatch_ms` | Measured host wall for GPU path only; **not** tok/s |
| `device` | `d3d12`, `d3d12-warp`, `cpu`, or `none` |
| `adapter` | DXGI adapter name, or `portable-gemm` |
| `note` | Honest blocker / fixture name. No invented metrics. |

A committed `results/matmul.csv` may be a **blocked** Linux/cloud snapshot. Live GPU rows are produced on a Windows D3D12 box. Git ignores other `results/*.csv` so a local overwrite does not get committed by accident; `matmul.csv` is the tracked Fase 1 artifact.

## CPU ggml baseline

ggml is **not vendored**. The comparison uses the portable GEMM in `src/cpp/cpu_matmul.*`. Details: [docs/ggml-baseline.md](../docs/ggml-baseline.md).

## Real GPU / Xbox benches

GitHub-hosted `windows-latest` has no Xbox Series S|X GPU. Real numbers require:

- a Dev Mode console, or
- a self-hosted runner labeled `self-hosted`, `windows`, `xbox-gpu` (see `.github/workflows/benchmark.yml`; that job is disabled by default).

WARP / Basic Render Driver is a **Windows software** D3D12 device, not Series S|X hardware.

Fase 6 console table: [docs/console.md](../docs/console.md) — Series S E0 measured ([PR #17](https://github.com/gianlucamazza/xbox-gpu-training/pull/17)); historical host rows / Series X / quality remain **UNMEASURED**. This smoke is **not** that E0 trial. Fase 7 write-up: [docs/results.md](../docs/results.md).

Workflow **Benchmark** job name: **`benchmark`**. Artifact: `benchmark-results` (still the smoke JSON; this workflow was not rewritten in Fase 1).
