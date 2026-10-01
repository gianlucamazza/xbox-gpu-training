# Execution plan (Cursor)

## Sintesi IT

Un agente Cursor esegue **una fase alla volta**. Branch `phase-N/<slug>` da `main`, una PR per fase, label `phase-N` + topic, milestone **Fase N**. Nessun tok/s inventato. Nessuna CUDA. DirectML solo come nota inference/forward. GDKX/ID@Xbox non rivendicati. CI verde prima del merge. Playbook in inglese sotto.

## How Cursor must work

- One phase at a time. Never start phase N+1 until phase N acceptance criteria pass.
- Branch: `phase-N/<short-slug>` from latest `main` (after the prior phase merged). One PR per phase → `main`.
- Labels: matching `phase-N` plus topic (`kernel` / `memory` / `benchmark` / `research` / `adr`).
- Milestone: matching **Fase N** (Italian titles on GitHub).
- Commits: English conventional commits. PR body: objective, commands run, acceptance results (paste), what was **not** done.
- Report results in the PR and update [ROADMAP.md](../ROADMAP.md) checkboxes / status.
- Do **not** invent tok/s or quality metrics. Do **not** claim CUDA, ID@Xbox, or that DirectML is the trainer. DirectML = inference/forward note only if used.
- Prefer the Windows DirectX 12 path; document Xbox Dev Mode steps for Fase 6. Platform SoT: [docs/platform/blockers-fase6-validation.md](platform/blockers-fase6-validation.md) (Fase 0–5 is not gated by Fase 6).

## Phase checklist

| Phase | Branch pattern | Gate |
| --- | --- | --- |
| 0 | `phase-0/<slug>` | Device + `dxc` + hello compute shader + **CI** `lint-docs` / `build-windows` |
| 1 | `phase-1/<slug>` | Matmul parity vs CPU ggml; CSV in `benchmarks/` |
| 2 | `phase-2/<slug>` | FLP2 forward matches CPU fixture |
| 3 | `phase-3/<slug>` | Grad check + one AdamW/STE step |
| 4 | `phase-4/<slug>` | Streaming stays under documented App ~1 GB budget |
| 5 | `phase-5/<slug>` | QAT/WSD config + N-step smoke |
| 6 | `phase-6/<slug>` | Console table **or** `BLOCKED: no console` |
| 7 | `phase-7/<slug>` | Honest `docs/results.md` |

## Global constraints (NEVER)

- No CUDA/cuDNN. No claiming GDKX/ID@Xbox access. No DirectML-as-trainer. No merging without CI green. No inventing benchmarks.

---

### Fase 0 — Setup ambiente

**Objective:** Public GDK/Windows SDK notes, DirectX 12 device create, HLSL compile path (`dxc`), PIX profiling install/docs, hello compute shader dispatch, CI `windows-latest` smoke.

**Branch:** `phase-0/env-setup`

**Create/modify:**

- `docs/setup.md`
- `examples/hello-compute/` (host that dispatches `src/hlsl/hello_compute.hlsl`)
- `src/cpp/` DirectX 12 device init (keep the smoke `main` if needed)
- `.github/workflows/ci.yml` (already present — extend, do not rename jobs `lint-docs` / `build-windows`)

**Commands:**

```bat
REM from <VS_DEV_CMD> or x64 Native Tools
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
where dxc
dxc -T cs_6_0 -E CSMain -Fo build\hello_compute.cso src\hlsl\hello_compute.hlsl
python scripts\check_required_docs.py
```

**Acceptance criteria:**

- [ ] `docs/setup.md` documents GDK (public), Windows SDK, DirectX 12, `dxc`, PIX — no GDKX/ID@Xbox access claim
- [ ] Hello compute shader dispatches on a Windows DX12 device **or** documents BLOCKED reason
- [ ] **CI** jobs `lint-docs` and `build-windows` green
- [ ] HLSL compile step runs when `dxc` exists; skip notice when missing

**Do NOT:** CUDA paths; fake PIX captures; claim console numbers; rename CI job ids.

**Checkpoint:** PR lists OS, SDK, whether `dxc`/PIX/device create worked. Stop if no Windows GPU box — do not invent a dispatch log.

---

### Fase 1 — Kernel matmul FP16/FP32

**Objective:** HLSL matmul kernels, host harness, benchmark vs CPU ggml (vendor or stub interface + CPU baseline).

**Branch:** `phase-1/matmul-hlsl`

**Create/modify:**

- `src/hlsl/matmul.hlsl` (replace stub)
- `src/cpp/` harness + CPU reference interface
- `benchmarks/` runner writing CSV (not invented tok/s)
- `docs/` note on how ggml is vendored/compared

**Commands:**

```bat
cmake --build build --config Release
dxc -T cs_6_0 -E CSMain -Fo build\matmul.cso src\hlsl\matmul.hlsl
.\build\Release\xbox_gpu_host.exe --bench matmul --out benchmarks\results\matmul.csv
```

**Acceptance criteria:**

- [ ] Numerical parity vs CPU reference within tolerance **TBD** (write the TBD number in the PR when chosen)
- [ ] Bench CSV under `benchmarks/` with schema + `status` if not a full run
- [ ] Labels `phase-1`, `kernel`, `benchmark`; milestone Fase 1

**Do NOT:** Invent tok/s; skip the CPU baseline; treat DirectML as the matmul trainer.

**Checkpoint:** Paste max-abs / rel error and whether ggml is vendored. Stop if no DX12 device — keep CPU-only reference tests green.

---

### Fase 2 — Forward FLP2 on GPU

**Objective:** Reconstruct forward from the FLP2 codec (FloppyLM `floppy_4mb` as reference, ADR 0012). GPU decode + forward ops.

**Branch:** `phase-2/flp2-forward`

**Create/modify:**

- `src/hlsl/` RMSNorm, RoPE, FLP2 decode / forward
- `src/cpp/` fixture loader
- Tiny CPU-reference fixture under `benchmarks/` or `examples/`

**Commands:**

```bat
.\build\Release\xbox_gpu_host.exe --forward-fixture fixtures\tiny_flp2.json
```

**Acceptance criteria:**

- [ ] Forward matches CPU reference on the tiny fixture (tolerance TBD)
- [ ] xllama was **not** modified
- [ ] Labels `phase-2`, `kernel`, `research`; milestone Fase 2

**Do NOT:** Modify [gianlucamazza/xllama](https://github.com/gianlucamazza/xllama); claim xllama GPU train; skip the fixture.

**Checkpoint:** Paste fixture name + error. Stop if codec reconstruction is ambiguous — open a `research` issue instead of guessing.

---

### Fase 3 — Backward + AdamW + STE

**Objective:** Backward kernels, AdamW, straight-through estimator for quantized weights.

**Branch:** `phase-3/adamw-ste`

**Create/modify:**

- `src/hlsl/` backward / STE
- `src/cpp/` AdamW step on master fp32 weights
- Tiny-net grad-check harness

**Commands:**

```bat
.\build\Release\xbox_gpu_host.exe --grad-check
.\build\Release\xbox_gpu_host.exe --train-step 1
```

**Acceptance criteria:**

- [ ] Gradient check on a tiny net (finite-diff vs analytic; tolerance TBD)
- [ ] One train step runs without crash
- [ ] Labels `phase-3`, `kernel`, `research`; milestone Fase 3

**Do NOT:** Publish quality metrics; call DirectML the optimizer.

**Checkpoint:** Paste grad-check table. STE/QAT mapping: [docs/adr/0002-ste-qat-mapping.md](adr/0002-ste-qat-mapping.md). Stop if that mapping is unspecified — ADR first.

---

### Fase 4 — Memory streaming

**Objective:** Double buffering, ~1 GB UWP AppContainer RAM awareness, VRAM budgets. Document Game designation ~5 GB.

**Branch:** `phase-4/streaming`

**Create/modify:**

- `src/cpp/` chunk stream + double buffer
- `docs/` memory budget note
- Stress fixture

**Commands:**

```bat
.\build\Release\xbox_gpu_host.exe --stream-stress --budget-mb 1024
```

**Acceptance criteria:**

- [ ] Stress test stays under the documented App ~1 GB planning budget (or records a measured breach)
- [ ] No OOM on the defined fixture
- [ ] Game ~5 GB documented as the other designation
- [ ] Labels `phase-4`, `memory`; milestone Fase 4

**Do NOT:** Assume Game designation in App packages; ignore AppContainer.

**Checkpoint:** Paste peak working-set. Stop if only desktop RAM is available — mark console budget as unvalidated.

---

### Fase 5 — QAT ternary/2/4-bit + WSD

**Objective:** QAT schedule, WSD + isolated cooldowns.

**Branch:** `phase-5/qat-wsd`

**Create/modify:**

- Schedule config (JSON/YAML under `examples/` or `src/`)
- Train loop wiring QAT + WSD + cooldown
- Docs for bit-widths

**Commands:**

```bat
.\build\Release\xbox_gpu_host.exe --qat-smoke --steps 16
```

**Acceptance criteria:**

- [ ] Schedule config validated (schema / dry-run)
- [ ] Smoke train loop completes N steps (**N = 16**, written in [docs/qat-wsd.md](qat-wsd.md))
- [ ] Labels `phase-5`, `research`; milestone Fase 5

**Do NOT:** Invent final loss/quality; skip cooldown isolation in the config.

**Checkpoint:** Paste step count + config path. No quality claims.

---

### Fase 6 — Series S|X Dev Mode validation

Current execution: the separate E0 x64 UWP app has passed Series S functional
acceptance, representative throughput, exact recovery and real suspension.
See [console status](console.md) and [current evidence](evidence/e0-20261001/notes.md).
The commands below document the historical desktop lane; they do not validate
that lane on console. The next active task is companion scientific E0 completion.


**Objective:** Deploy package, run benches on console, PIX capture notes. Speed/quality vs CPU-only only if measured.

**Branch:** `phase-6/dev-mode-validation`

**Create/modify:**

- Deploy notes in `docs/setup.md` or `docs/console.md`
- Results table (empty cells allowed if BLOCKED)
- PIX capture checklist

**Commands:**

```bat
REM Device Portal / Dev Mode deploy — exact cmd TBD in docs/setup.md
REM PIX: capture one compute shader dispatch on console if tooling allows
```

**Acceptance criteria:**

- [ ] Results table filled from **real hardware** **OR** explicit `BLOCKED: no console` with reason
- [ ] CPU-only comparison only if both sides measured
- [ ] Labels `phase-6`, `benchmark`; milestone Fase 6

**Do NOT:** Invent console tok/s; claim ID@Xbox/GDKX; merge a fake table.

**Checkpoint:** Stop and wait for a human if no Dev Mode kit. That is a successful honest phase if BLOCKED is explicit. Blocker summary: [docs/platform/blockers-fase6-validation.md](platform/blockers-fase6-validation.md).

---

### Fase 7 — Publish results

**Objective:** `docs/results.md`, figure placeholders, honest limitations, companion link to FloppyLM CPU path. Public BitNet/peer comparison only with real numbers.

**Branch:** `phase-7/publish`

**Create/modify:**

- `docs/results.md`
- Figure placeholders (no fabricated plots)
- README status update (IT + EN)

**Commands:**

```bash
python3 scripts/check_required_docs.py
python3 scripts/check_glossary.py
```

**Acceptance criteria:**

- [ ] Docs complete; every metric sourced or marked unmeasured
- [ ] Link to the FloppyLM owner repo (`floppy_4mb`, ADR 0012)
- [ ] Labels `phase-7`, `research`; milestone Fase 7

**Do NOT:** Fabricate numbers or peer rankings.

**Checkpoint:** Owner review of `docs/results.md` (done 2026-10-01 for functional evidence).
