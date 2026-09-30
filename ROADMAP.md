# ROADMAP

Cursor playbook (commands, acceptance, checkpoints): [docs/execution-plan.md](docs/execution-plan.md).

Milestones (Italian titles, already on GitHub): [Fase 0–7](https://github.com/gianlucamazza/xbox-gpu-training/milestones).

Phase labels (English kebab-case): `phase-0` … `phase-7`. Topic labels: `research`, `kernel`, `memory`, `benchmark`, `adr`.

No tok/s or quality numbers until Fase 6 measures them on Dev Mode hardware (or the phase reports `BLOCKED: no console`). Fase 0–5 Windows / public-GDK work is **not** gated by Fase 6. Explicit console blockers: [docs/platform/blockers-fase6-validation.md](docs/platform/blockers-fase6-validation.md).

| Phase | Milestone | Branch pattern | Gate |
| --- | --- | --- | --- |
| 0 | [Fase 0 — Setup ambiente](https://github.com/gianlucamazza/xbox-gpu-training/milestone/1) | `phase-0/<slug>` | DirectX 12 device + HLSL compile path + CI smoke |
| 1 | [Fase 1 — Kernel HLSL di base](https://github.com/gianlucamazza/xbox-gpu-training/milestone/2) | `phase-1/<slug>` | Matmul parity vs CPU ggml (tolerance TBD) |
| 2 | [Fase 2 — Forward FLP2 su GPU](https://github.com/gianlucamazza/xbox-gpu-training/milestone/3) | `phase-2/<slug>` | Forward match on tiny FLP2 fixture |
| 3 | [Fase 3 — Backward + AdamW](https://github.com/gianlucamazza/xbox-gpu-training/milestone/4) | `phase-3/<slug>` | Grad check + one AdamW/STE step |
| 4 | [Fase 4 — Streaming memoria](https://github.com/gianlucamazza/xbox-gpu-training/milestone/5) | `phase-4/<slug>` | Double buffer under ~1 GB App budget |
| 5 | [Fase 5 — QAT completo WSD](https://github.com/gianlucamazza/xbox-gpu-training/milestone/6) | `phase-5/<slug>` | QAT schedule + N-step smoke |
| 6 | [Fase 6 — Validazione Series S\|X](https://github.com/gianlucamazza/xbox-gpu-training/milestone/7) | `phase-6/<slug>` | Real console table **or** BLOCKED |
| 7 | [Fase 7 — Pubblicazione](https://github.com/gianlucamazza/xbox-gpu-training/milestone/8) | `phase-7/<slug>` | Honest `docs/results.md` |

## Phase 0 — env setup (GDK/Windows SDK, DirectX 12, HLSL compute, PIX profiling)

- Public GDK / Windows SDK. DirectX 12 device create. `dxc` for HLSL. PIX install notes.
- Hello compute shader dispatch. CI `windows-latest` smoke already in `.github/workflows/ci.yml`.
- Status: **scaffold only** (this PR). Execution: [docs/execution-plan.md](docs/execution-plan.md#fase-0--setup-ambiente).

## Phase 1 — base HLSL kernels (matmul FP16/FP32, bench vs CPU ggml)

- Real matmul compute shaders. Host harness. CPU ggml baseline (vendor or stub interface).
- Bench CSV under `benchmarks/`. Label: `kernel`, `benchmark`, `phase-1`.

## Phase 2 — FLP2-codec reconstructed forward on GPU

- Reconstruct forward from the FLP2 codec. Reference FloppyLM/xllama docs conceptually. **Do not modify xllama.**
- Acceptance: forward match vs CPU reference on a tiny fixture.

## Phase 3 — backward + AdamW on GPU with straight-through estimator

- Backward kernels, AdamW, STE for quantized weights.
- Acceptance: gradient check on a tiny net; one train step runs.

## Phase 4 — memory streaming, double buffering, ~1 GB UWP App RAM discipline

- Also document Game designation ~5 GB. AppContainer + VRAM budgets.
- Public SoT: [docs/platform/uwp-resources.md](docs/platform/uwp-resources.md) (debugger can mask OOM; non-debug is the gate).
- Label: `memory`, `phase-4`.

## Phase 5 — full QAT ternary/2/4-bit on GPU with WSD + isolated cooldowns

- Schedule config validated; smoke train loop completes N steps (N TBD).

## Phase 6 — Series S|X Dev Mode validation; speed/quality vs CPU-only

- Deploy package, run benches, PIX notes. Fill a results table from hardware **or** write `BLOCKED: no console` with reason.
- Stop for a human if no Dev Mode kit.
- Known blockers (UWP memory, no DirectML-as-trainer SoT, public GDK Windows-only, Dev Mode ≠ GDKX): [docs/platform/blockers-fase6-validation.md](docs/platform/blockers-fase6-validation.md). Public SKU specs only: [docs/platform/series-s-vs-x.md](docs/platform/series-s-vs-x.md).

## Phase 7 — publish results (paper/blog), public BitNet/peer comparison

- `docs/results.md`, figure placeholders, honest limitations, companion link to FloppyLM CPU path.
- No fabricated numbers.
