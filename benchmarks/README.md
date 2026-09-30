# `benchmarks/`

Honest measurement only. This folder does **not** contain measured Xbox GPU tok/s or quality numbers.

## Current status

The harness is a **stub**. `scripts` / CI write `benchmarks/results/smoke.json` with:

```json
{ "status": "stub", "note": "..." }
```

Never invent tok/s, latency, or quality scores.

## CPU ggml baseline (Fase 1)

Fase 1 compares HLSL matmul against a CPU ggml reference. ggml is **not vendored yet**. The planned interface:

1. Host builds a tiny FP32 (then FP16) fixture.
2. CPU reference (ggml or a portable GEMM) writes expected tiles.
3. GPU compute shader writes actual tiles.
4. Harness records max-abs / relative error and wall time to CSV under `benchmarks/results/`.

Until that lands, `python3 benchmarks/run_smoke.py` is the only supported command.

## Real GPU / Xbox benches

GitHub-hosted `windows-latest` has no Xbox Series S|X GPU. Real numbers require:

- a Dev Mode console, or
- a self-hosted runner labeled `self-hosted`, `windows`, `xbox-gpu` (see `.github/workflows/benchmark.yml`; that job is disabled by default).

Workflow **Benchmark** job name: **`benchmark`**. Artifact: `benchmark-results`.
