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

- [x] Public GDK / Windows SDK documented in [docs/setup.md](docs/setup.md) (links [docs/platform/gdk-vs-gdkx.md](docs/platform/gdk-vs-gdkx.md), [dev-mode.md](docs/platform/dev-mode.md)). DirectX 12 device create. `dxc` for HLSL. PIX install notes (no invented capture).
- [x] Hello compute shader dispatch on Windows DX12 **or** explicit `BLOCKED: no D3D12 device` / missing-shader reason (no invented dispatch log).
- [x] CI `windows-latest` smoke: jobs `lint-docs` / `build-windows` (ids unchanged). HLSL compile when `dxc` exists; skip notice when missing.
- Status: **Fase 0 implementation**. Execution: [docs/execution-plan.md](docs/execution-plan.md#fase-0--setup-ambiente).

## Phase 1 — base HLSL kernels (matmul FP16/FP32, bench vs CPU ggml)

- [x] Real matmul compute shaders (`CSMain` / `CSMainFP32` / `CSMainFP16` in [src/hlsl/matmul.hlsl](src/hlsl/matmul.hlsl)). Host harness uploads A/B, dispatches, reads back C.
- [x] CPU baseline: portable GEMM interface ([src/cpp/cpu_matmul.cpp](src/cpp/cpu_matmul.cpp)); ggml **not vendored** — [docs/ggml-baseline.md](docs/ggml-baseline.md).
- [x] Bench CSV under [benchmarks/results/matmul.csv](benchmarks/results/matmul.csv) (`xbox-gpu-training.benchmark.matmul.v1` + `status`). No invented tok/s.
- [x] Chosen TBD tolerances: FP32 max-abs `1e-4` / max-rel `1e-3`; FP16 max-abs `5e-2` / max-rel `5e-2`.
- Status: **Fase 1 implementation**. Execution: [docs/execution-plan.md](docs/execution-plan.md#fase-1--kernel-matmul-fp16fp32). GPU dispatch is **BLOCKED** on hosts without D3D12 (CPU-only tests stay green). Label: `kernel`, `benchmark`, `phase-1`.

## Phase 2 — FLP2-codec reconstructed forward on GPU

- [x] HLSL RMSNorm, RoPE, FLP2 scalar decode, tiny forward ([src/hlsl/rmsnorm.hlsl](src/hlsl/rmsnorm.hlsl), [rope.hlsl](src/hlsl/rope.hlsl), [flp2_decode.hlsl](src/hlsl/flp2_decode.hlsl), [flp2_forward.hlsl](src/hlsl/flp2_forward.hlsl)).
- [x] Host `--forward-fixture` + fixture loader ([src/cpp/fixture_loader.cpp](src/cpp/fixture_loader.cpp)). Tiny CPU fixture [benchmarks/fixtures/tiny_flp2.json](benchmarks/fixtures/tiny_flp2.json).
- [x] Chosen TBD tolerance: max-abs `1e-5` / max-rel `1e-4` (FloppyLM ADR 0004 forward gate). Contract: [docs/flp2-forward.md](docs/flp2-forward.md).
- [x] xllama **not** modified. Binary FLP2 envelope (rANS) **not** guessed — `research` issue for envelope byte-parity.
- Status: **Fase 2 implementation**. Execution: [docs/execution-plan.md](docs/execution-plan.md#fase-2--forward-flp2-on-gpu). GPU dispatch is **BLOCKED** on hosts without D3D12 (CPU fixture tests stay green). Label: `kernel`, `research`, `phase-2`.

## Phase 3 — backward + AdamW on GPU with straight-through estimator

- [x] STE / FakeQuant mapping written: [docs/adr/0002-ste-qat-mapping.md](docs/adr/0002-ste-qat-mapping.md). Master fp32; ternary absmean FakeQuant; STE `∂Q/∂W ≈ 1` with `|W/s| ≤ 1` clip. Contract: [docs/ste-adamw.md](docs/ste-adamw.md).
- [x] HLSL grad path: [`fakequant_ternary.hlsl`](src/hlsl/fakequant_ternary.hlsl), [`matmul_grad.hlsl`](src/hlsl/matmul_grad.hlsl), [`relu2_grad.hlsl`](src/hlsl/relu2_grad.hlsl), [`ste_backward.hlsl`](src/hlsl/ste_backward.hlsl).
- [x] Host AdamW on master fp32 (`lr=1e-3`, `β1=0.9`, `β2=0.999`, `ε=1e-8`, `wd=0.01`). DirectML is not the optimizer.
- [x] `--grad-check` tiny relu2 net: STE-identity analytic vs central finite-diff. Chosen TBD: max-abs `1e-3`; max-rel `2e-2` when `|analytic| ≥ 1e-2` (smaller grads are abs-gated).
- [x] `--train-step 1` runs without crash (measured loss before/after only; not a quality curve).
- Status: **Fase 3 implementation**. Execution: [docs/execution-plan.md](docs/execution-plan.md#fase-3--backward--adamw--ste). GPU dispatch is **BLOCKED** on hosts without D3D12 (CPU grad-check / one step stay green). Label: `kernel`, `research`, `adr`, `phase-3`.

## Phase 4 — memory streaming, double buffering, ~1 GB UWP App RAM discipline

- [x] Chunk stream + 2-slot host double buffer ([src/cpp/stream_buffer.cpp](src/cpp/stream_buffer.cpp)). Logical corpus is never allocated as one tensor.
- [x] Stress fixture [benchmarks/fixtures/stream_stress.json](benchmarks/fixtures/stream_stress.json) + `xbox_gpu_host --stream-stress --budget-mb 1024`.
- [x] App **~1 GB** (1024 MiB) planning budget documented; Creators **Game ~5 GB** (5120 MiB) documented as the **other** designation only — not assumed for App packages. Contract: [docs/memory-budget.md](docs/memory-budget.md). SoT: [docs/platform/uwp-resources.md](docs/platform/uwp-resources.md) (debugger can mask OOM; non-debug is the gate).
- [x] Peak working-set printed from the host that actually ran. Linux desktop (this implementation host): **36.08 MiB** (`VmHWM`) vs App planning 1024 MiB — under budget, no OOM. **Console AppContainer UNVALIDATED** — no invented Series S|X numbers.
- [x] No new HLSL kernel. GPU path (when D3D12 exists) is ping-pong `CopyBufferRegion` of two tiles; otherwise `gpu_double_buffer: BLOCKED: no D3D12 device`.
- Status: **Fase 4 implementation**. Execution: [docs/execution-plan.md](docs/execution-plan.md#fase-4--memory-streaming). Label: `memory`, `phase-4`.

## Phase 5 — full QAT ternary/2/4-bit on GPU with WSD + isolated cooldowns

- [x] Schedule SoT: [docs/qat-wsd.md](docs/qat-wsd.md) + [examples/qat-wsd-smoke.json](examples/qat-wsd-smoke.json) (`xbox-gpu-training.qat.wsd.v1`). Schema note: [examples/qat-wsd.schema.json](examples/qat-wsd.schema.json). Dry-run: `python3 scripts/validate_qat_schedule.py examples/qat-wsd-smoke.json --dry-run`.
- [x] WSD on host AdamW (ADR 0002 `lr=1e-3`, β/ε/wd frozen): warmup linear `0 → base_lr`, stable constant, decay **linear** `base_lr → min_lr` (`min_lr = 1e-4` = `0.1 × base`).
- [x] Isolated cooldowns are overlay windows `[{ start_step, steps, end_lr }]` — **not** merged into WSD decay. Overlaps rejected. Example: `stable-mid` on steps `[8, 12)`.
- [x] Chosen smoke **N = 16** (`warmup 4 + stable 8 + decay 4`). Cooldown overlays; N is not `16+4`. Host: `xbox_gpu_host --qat-smoke --steps 16`.
- [x] Default FakeQuant **ternary absmean** + STE (ADR 0002). 2-bit / 4-bit are **host** midrise FakeQuant (`--bit-width 2|4`); Fase 3 stubs are no longer no-ops. **No new HLSL** — GPU 2/4-bit FakeQuant not dispatched.
- Status: **Fase 5 implementation**. Execution: [docs/execution-plan.md](docs/execution-plan.md#fase-5--qat-ternary24-bit--wsd). GPU path without D3D12: `BLOCKED: no D3D12 device`. No quality / tok/s / Series numbers. Label: `research`, `phase-5`.

## Phase 6 — Series S|X Dev Mode validation; speed/quality vs CPU-only

- [x] Deploy notes + PIX checklist scaffolding: [docs/console.md](docs/console.md) (pointer in [docs/setup.md](docs/setup.md)). Win32 host is **not** a UWP package; no AppX was deployed.
- [x] Results table present with explicit **`BLOCKED: no console`** (empty metric cells). Reason at merge (`66224e05`, [PR #15](https://github.com/gianlucamazza/xbox-gpu-training/pull/15)): **no Dev Mode kit** in that lane; **GDKX / ID@Xbox not claimed**. No invented tok/s or Series benches.
- [ ] Real Series S|X Dev Mode benches — **UNVALIDATED**. Table not filled from hardware. Console validation is **not** complete. A Dev Mode kit is **now available** (re-enabled 2026-09-30) for a **follow-up measured PR** only — do not back-fill Fase 6 on `main` with invented numbers.
- [ ] CPU-only vs console comparison — not filled (both sides not measured on the same kit).
- Status: **`BLOCKED: no console`** on `main`. Honest checkpoint. Known blockers stay accurate: [docs/platform/blockers-fase6-validation.md](docs/platform/blockers-fase6-validation.md) (UWP memory + debugger mask; no DirectML-as-trainer SoT; public GDK Windows-only; Dev Mode ≠ GDKX). Public SKU specs only: [docs/platform/series-s-vs-x.md](docs/platform/series-s-vs-x.md). Execution: [docs/execution-plan.md](docs/execution-plan.md#fase-6--series-sx-dev-mode-validation). Label: `phase-6`, `benchmark`.

## Phase 7 — publish results (paper/blog), public BitNet/peer comparison

- [x] Honest [docs/results.md](docs/results.md): Fase 0–6 host measurements sourced; console / tok/s / quality **UNMEASURED** or **`BLOCKED: no console`**.
- [x] Figure placeholders only ([docs/figures/README.md](docs/figures/README.md)) — **no fabricated plots**.
- [x] Companion link to FloppyLM **CPU** path: [gianlucamazza/xllama](https://github.com/gianlucamazza/xllama). xllama / `feat/floppylm-training` **not** modified.
- [x] Public BitNet/peer table: paper-sourced numbers (Ma et al. [arXiv:2402.17764](https://arxiv.org/abs/2402.17764)) or explicitly **UNMEASURED** on our side. **No ranking.**
- [x] README status updated (IT + EN).
- [ ] Human review of `docs/results.md` before calling the research **public** (Fase 7 checkpoint).
- [ ] Series / peer charts — still placeholders until a measured follow-up.
- Status: **Fase 7 draft for human review**. Execution: [docs/execution-plan.md](docs/execution-plan.md#fase-7--publish-results). Does **not** complete console validation. Label: `phase-7`, `research`.
