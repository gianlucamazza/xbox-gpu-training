# ROADMAP

Cursor playbook (commands, acceptance, checkpoints): [docs/execution-plan.md](docs/execution-plan.md).

Milestones (Italian titles, already on GitHub): [Fase 0–7](https://github.com/gianlucamazza/xbox-gpu-training/milestones).

Phase labels (English kebab-case): `phase-0` … `phase-7`. Topic labels: `research`, `kernel`, `memory`, `benchmark`, `adr`.

No tok/s or quality numbers until Dev Mode hardware measures them (or the phase reports `BLOCKED` / **UNMEASURED**). Series S E0 functional throughput is measured ([docs/console.md](docs/console.md)); scientific quality is not. Fase 0–5 Windows / public-GDK work is **not** gated by Fase 6. Explicit console blockers: [docs/platform/blockers-fase6-validation.md](docs/platform/blockers-fase6-validation.md).

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
- [x] Historical desktop peak working-set printed from the host that actually ran. Linux desktop (this implementation host): **36.08 MiB** (`VmHWM`) vs App planning 1024 MiB — under budget, no OOM. **Console AppContainer UNVALIDATED** — no invented Series S|X numbers.
- [x] No new HLSL kernel. GPU path (when D3D12 exists) is ping-pong `CopyBufferRegion` of two tiles; otherwise `gpu_double_buffer: BLOCKED: no D3D12 device`.
- Status: **Fase 4 implementation**. Execution: [docs/execution-plan.md](docs/execution-plan.md#fase-4--memory-streaming). Label: `memory`, `phase-4`.

## Phase 5 — full QAT ternary/2/4-bit on GPU with WSD + isolated cooldowns

- [x] Schedule SoT: [docs/qat-wsd.md](docs/qat-wsd.md) + [examples/qat-wsd-smoke.json](examples/qat-wsd-smoke.json) (`xbox-gpu-training.qat.wsd.v1`). Schema note: [examples/qat-wsd.schema.json](examples/qat-wsd.schema.json). Dry-run: `python3 scripts/validate_qat_schedule.py examples/qat-wsd-smoke.json --dry-run`.
- [x] WSD on host AdamW (ADR 0002 `lr=1e-3`, β/ε/wd frozen): warmup linear `0 → base_lr`, stable constant, decay **linear** `base_lr → min_lr` (`min_lr = 1e-4` = `0.1 × base`).
- [x] Isolated cooldowns are overlay windows `[{ start_step, steps, end_lr }]` — **not** merged into WSD decay. Overlaps rejected. Example: `stable-mid` on steps `[8, 12)`.
- [x] Chosen smoke **N = 16** (`warmup 4 + stable 8 + decay 4`). Cooldown overlays; N is not `16+4`. Host: `xbox_gpu_host --qat-smoke --steps 16`.
- [x] Default FakeQuant **ternary absmean** + STE (ADR 0002). 2-bit / 4-bit are **host** midrise FakeQuant (`--bit-width 2|4`); Fase 3 stubs are no longer no-ops. **No new HLSL** — GPU 2/4-bit FakeQuant not dispatched.
- Status: **Fase 5 implementation**. **N=16 is schedule smoke only — not QAT quality.** Execution: [docs/execution-plan.md](docs/execution-plan.md#fase-5--qat-ternary24-bit--wsd). GPU path without D3D12: `BLOCKED: no D3D12 device`. No quality / tok/s / Series numbers. Label: `research`, `phase-5`.

## Phase 6 — Series S|X Dev Mode validation

- [x] Separate E0 x64 UWP Release package built, signed with verified CI payloads,
  installed and exercised on retail Series S in Dev Mode ([PR #17](https://github.com/gianlucamazza/xbox-gpu-training/pull/17), `aec1a2a`).
- [x] Independent operation/model parity, identical-input optimizer, exact resume,
  real suspension and worker reuse validated on the current package.
- [x] Representative throughput, transfers and non-debug app memory measured.
  [Evidence](docs/evidence/e0-20261001/notes.md), [console status](docs/console.md).
- [x] E0.1 GPU-resident engine (`0.1.0.28`): same acceptance, bit-identical to `0.1.0.24`,
  10224 token/s vs 963.6 on the representative benchmark ([evidence](docs/evidence/e0-20261001-resident/notes.md)).
- [ ] Full scientific E0 campaign and reserved final test. The first campaign on `0.1.0.24`
  was stopped cleanly at trunk step 455; campaign `e0-20261001T090514Z-4236fd` runs on E0.1.
- [ ] Series X, matched CPU comparison and PIX captures: no measurements available.
- [ ] Historical Win32 host workloads (`hello_compute` / matmul / FLP2 / stream-stress / qat-smoke) on console: **UNMEASURED**.
- Status: Series S E0 **measured**; remaining Series / quality / PIX cells **UNMEASURED**. Known blockers stay accurate: [docs/platform/blockers-fase6-validation.md](docs/platform/blockers-fase6-validation.md). Execution: [docs/execution-plan.md](docs/execution-plan.md#fase-6--series-sx-dev-mode-validation). Label: `phase-6`, `benchmark`.

The Phase 0–5 entries above document the original desktop diagnostic lane. They
do not establish console validation of those separate host workloads. The active
E0 UWP trainer is governed by ADR 0003/0004; its measured results are listed here.

## Phase 7 — Publication

- [x] Honest [docs/results.md](docs/results.md): Fase 0–5 Win32 host sourced; Series S E0 **functional** evidence sourced from [`docs/evidence/e0-20261001/`](docs/evidence/e0-20261001/notes.md) (pkg `0.1.0.24` / source `6a124021`; [kernel-parity.json](docs/evidence/e0-20261001/kernel-parity.json) 52 ops / 36 fixtures, `hardware_gpu`, `purpose: functional`). Synthetic throughput cited as **functional only**, not quality / PPL.
- [x] Figure placeholders only ([docs/figures/README.md](docs/figures/README.md)) — **no fabricated plots**.
- [x] Companion link to FloppyLM **CPU** path: [gianlucamazza/xllama](https://github.com/gianlucamazza/xllama). xllama / `feat/floppylm-training` **not** modified.
- [x] Public BitNet/peer table: paper-sourced numbers (Ma et al. [arXiv:2402.17764](https://arxiv.org/abs/2402.17764)) or explicitly **UNMEASURED** on our side. **No ranking.** Do **not** borrow Series S E0 numbers into peer cells.
- [x] README status updated (IT + EN).
- [x] Publish functional E0 evidence, exact package lineage, baseline failures and limits.
- [x] Owner review of `docs/results.md` (2026-10-01).
- [ ] Publish scientific E0 selection, paired statistics, exclusions and costs after the companion campaign passes all gates and the single final test finishes.
- [ ] Peer/BitNet comparison or paper: requires a preregistered matched comparison; no such result is claimed by the functional acceptance.
- [ ] Series / peer charts — still placeholders (E0 number is sourced in prose, not drawn).
- Status: **Fase 7 functional publication reviewed**; scientific E0 pending. Execution: [docs/execution-plan.md](docs/execution-plan.md#fase-7--publish-results). Does **not** complete scientific E0. Label: `phase-7`, `research`.

## Completion sequence (2026-10-01)

1. Merge the reviewed E0 trainer and keep validated source/package binding.
2. Run the companion's sequential row16/row8log campaign with frozen hashes.
3. Diagnose any failed native job; explicitly recover bound interrupted trials.
4. Publish generated scientific results when available; keep unmeasured targets pending.
