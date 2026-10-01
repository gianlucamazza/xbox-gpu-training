# Fase 7 — Published results (honest)

**Status: host work documented; Series S E0 functional execution sourced from [`docs/evidence/e0-20261001/`](evidence/e0-20261001/notes.md); scientific quality/PPL, Series X, matched CPU, and PIX remain `UNMEASURED`.** This page does **not** invent benches, quality curves, or peer rankings. It does **not** claim **GDKX**, **ID@Xbox**, CUDA, or DirectML-as-trainer. Reviewed by the owner on 2026-10-01.

[PR #15](https://github.com/gianlucamazza/xbox-gpu-training/pull/15) (`66224e05`) left the Win32 host lane as **`BLOCKED: no console`**. [PR #17](https://github.com/gianlucamazza/xbox-gpu-training/pull/17) (`aec1a2a7`) later landed a **separate** UWP E0 backend with Device Portal evidence on retail Series S. This page **cites** that evidence as **functional only**. Peer cells stay literature cites. Win32 Fase 0–3 desktop diagnostics are **not** the UWP E0 trainer.

Playbook: [docs/execution-plan.md](execution-plan.md#fase-7--publish-results). Console table: [docs/console.md](console.md). Evidence: [docs/evidence/e0-20261001/notes.md](evidence/e0-20261001/notes.md). Figure catalog: [docs/figures/README.md](figures/README.md).

## Sintesi IT

Fasi 0–5: lavoro host **Win32** (Windows DX12 quando c’è un device; Linux/CI senza D3D12 in `BLOCKED: no D3D12 device`) — **separato** dalla lane UWP E0. Fase 6: [PR #17](https://github.com/gianlucamazza/xbox-gpu-training/pull/17) E0 UWP Series S **sourced** da [`docs/evidence/e0-20261001/`](evidence/e0-20261001/notes.md) (pkg `0.1.0.24` / source `6a124021`; [kernel-parity.json](evidence/e0-20261001/kernel-parity.json) 52 ops / 36 fixture, `hardware_gpu`, `purpose: functional`). Throughput sintetico **solo funzionale**, **non** qualità / PPL. **UNMEASURED:** qualità scientifica / PPL, Series X, CPU matched, PIX. Fase 5 `N=16` = smoke di schedule, non QAT di qualità. Peer BitNet = solo cite di letteratura — **nessun ranking**, nessun numero Series S nelle celle peer. AppContainer ~1 GB / Release non-debug; Dev Mode **≠** GDKX / ID@Xbox; DirectML **≠** trainer. Owner FloppyLM: repo locale `Workspace/experiments/floppy_4mb` (ADR 0012). xllama **non** modificato.

## English abstract

Fase 0–5 measured **Win32 host** contracts (CPU fixtures, chosen tolerances, one Linux working-set). That desktop lane is **not** the UWP E0 trainer. Fase 6 on `main` (`aec1a2a7`, [PR #17](https://github.com/gianlucamazza/xbox-gpu-training/pull/17)) records Series S E0 **functional** evidence sourced from [`docs/evidence/e0-20261001/`](evidence/e0-20261001/notes.md): [kernel-parity.json](evidence/e0-20261001/kernel-parity.json) 52 ops / 36 fixtures, `hardware_gpu: true`, `purpose: functional`. Synthetic throughput is **functional only**, not quality / PPL. **UNMEASURED:** scientific quality / PPL, Series X, matched CPU, PIX. Fase 5 `N=16` is schedule smoke, not QAT quality. BitNet numbers below are **paper-sourced**; our peer cells stay **UNMEASURED**. FloppyLM owner: local repo `Workspace/experiments/floppy_4mb` (ADR 0012).

---

## How to read this page

| Tag | Meaning |
| --- | --- |
| **Sourced** | Number or fact lives in this repo or a cited public document |
| **UNMEASURED** | We have no measurement; cell stays empty / this word |
| **BLOCKED** | Path did not run; reason is written (no D3D12, …) |
| **UNVALIDATED** | Planning budget or SKU copy exists; that SKU/workload was not measured |
| **Placeholder** | Caption for a figure that was **not** drawn — no fabricated plot |

Every metric is one of those, or it is not written.

## Measurement hosts (do not mix)

| Host | What this repo actually ran | What it is not |
| --- | --- | --- |
| **Linux cloud / this agent** | Docs checks; portable CPU GEMM / FLP2 / STE / QAT host paths; Fase 4 `VmHWM` | Not D3D12. Not Xbox. Not `dxc`. |
| **GitHub `windows-latest`** | CI jobs `lint-docs` + `build-windows` (ids unchanged). C++ host + `dxc` when present. Possible WARP / Basic Render Driver | **Not** Series S\|X. WARP is software. |
| **Windows DX12 box** (Win32 `xbox_gpu_host`) | Hello / matmul / FLP2 / STE dispatch **if** a device exists | Still not console AppContainer. **Separate** from UWP E0. |
| **Series S Dev Mode (UWP E0)** | Functional E0 on package `0.1.0.24` / source `6a124021` — [e0-20261001](evidence/e0-20261001/notes.md) | **Not** scientific quality / PPL. **Not** Series X. **Not** GDKX. |
| **Series X Dev Mode** | **Nothing measured** | **UNMEASURED** |
| **GDKX / ID@Xbox kit** | **Not claimed** | Partner/NDA path |

## Repository boundaries

FloppyLM semantics, FLP2 and the `floppylm.*.v1` contracts are owned by the local FloppyLM repo (`Workspace/experiments/floppy_4mb`, ADR 0012); this repository is its only native training backend (E0 DX12/UWP).

- [xllama](https://github.com/gianlucamazza/xllama) contains no FloppyLM logic; its draft `feat/floppylm-training` (PR #301) was closed without merge.
- Do **not** modify xllama from here.
- DirectML on console, as used in the companion app, is **inference/forward-focused** and is **not** the trainer here ([directml-scope.md](platform/directml-scope.md)).

---

## Headline table (Fase 0–7)

| Fase | What was measured | Source | Status |
| --- | --- | --- | --- |
| 0 | Public GDK/Windows SDK/DX12/`dxc`/PIX **install notes**. Hello compute **or** `BLOCKED: no D3D12 device` | [setup.md](setup.md) | Host path documented. No invented dispatch log. |
| 1 | CPU GEMM self-check + chosen parity tolerances. Committed CSV is **`status=blocked`** (no D3D12 on that host) | [matmul.csv](../benchmarks/results/matmul.csv), [ggml-baseline.md](ggml-baseline.md) | GPU matmul **BLOCKED** on hosts without D3D12. **Not** tok/s. |
| 2 | Tiny FLP2 **scalar** decode + RMSNorm + RoPE + relu2 forward vs CPU fixture. Tolerance max-abs `1e-5` / max-rel `1e-4` | [flp2-forward.md](flp2-forward.md), [tiny_flp2.json](../benchmarks/fixtures/tiny_flp2.json) | Binary FLP2 envelope **not** reconstructed. xllama **not** modified. |
| 3 | STE-identity grad-check tolerances; one AdamW/STE step **runs**. Loss before/after if printed is **not** a quality curve | [ste-adamw.md](ste-adamw.md), [ADR 0002](adr/0002-ste-qat-mapping.md) | GPU kernels **BLOCKED** without D3D12. DirectML is not the optimizer. |
| 4 | Linux implementation host peak **36.08 MiB** `VmHWM` vs App plan **1024 MiB**. Logical 2 GiB never allocated as one tensor | [memory-budget.md](memory-budget.md), ROADMAP Fase 4 | **Console AppContainer UNVALIDATED** for this host smoke. Game ~5 GB documented only. |
| 5 | Schedule dry-run + host `--qat-smoke --steps 16` (WSD + isolated cooldown overlay). Default ternary FakeQuant | [qat-wsd.md](qat-wsd.md), [qat-wsd-smoke.json](../examples/qat-wsd-smoke.json) | **Schedule smoke only — not QAT quality.** No PPL. No new HLSL. 2/4-bit = host FakeQuant only. |
| 6 | Series S UWP E0 **functional** execution. Package `0.1.0.24` / source `6a124021` | [e0-20261001](evidence/e0-20261001/notes.md), [kernel-parity.json](evidence/e0-20261001/kernel-parity.json), [console.md](console.md), `aec1a2a7` | **Sourced functional** (52 ops / 36 fixtures, `hardware_gpu`). Quality / PPL / Series X / matched CPU / PIX = **UNMEASURED**. |
| 7 | This page: sourced host + sourced E0; figure placeholders; BitNet paper-or-UNMEASURED | this file, [figures/README.md](figures/README.md) | Owner-reviewed 2026-10-01. |

---

## Fase 0 — Environment (Windows host)

Sourced: [docs/setup.md](setup.md).

- Public **GDK** / **Windows SDK** / DirectX 12 / `dxc` / PIX-on-Windows **notes** exist.
- Hello compute: `STATUS: hello_compute dispatched` **only** when a D3D12 device ran; otherwise `BLOCKED: no D3D12 device`.
- CI job ids **`lint-docs`** / **`build-windows`** unchanged.
- **UNMEASURED:** PIX `.wpix` capture, console dispatch of this host, tok/s for hello_compute.

## Fase 1 — Matmul FP16/FP32 vs CPU

Sourced: [docs/ggml-baseline.md](ggml-baseline.md), [benchmarks/results/matmul.csv](../benchmarks/results/matmul.csv). ggml is **not vendored**; baseline is portable GEMM.

Chosen tolerances (Fase 1 TBD, written down):

| Precision | max-abs | max-rel |
| --- | --- | --- |
| FP32 | `1e-4` | `1e-3` |
| FP16 | `5e-2` | `5e-2` |

Committed CSV (Linux/cloud snapshot): every row `status=blocked`, `device=cpu`, `adapter=portable-gemm`, `parity=pass` on the **CPU self-check**, `gpu_dispatch_ms` empty. Notes say *not a GPU / tok/s / console result*.

Largest recorded CPU tile wall in that file: **0.246 ms** (FP16 `32×32×32`). That is a **tiny portable-GEMM** timing on the host that wrote the CSV. It is **not** tok/s and **not** a GPU or Series number.

**UNMEASURED:** GPU `gpu_dispatch_ms` on a real D3D12 adapter in the committed artifact; ggml `ggml_mul_mat` timing; any tok/s for this host matmul.

## Fase 2 — FLP2 reconstructed forward

Sourced: [docs/flp2-forward.md](flp2-forward.md).

Implemented: scalar decode `W = (symbol - half) * scale` (ternary / 2-bit / 4-bit lattices), RMSNorm (`eps=1e-6`), RoPE (`theta=10000`), tiny 1-layer relu2 fixture `tiny_flp2`.

**Not** implemented: on-disk FLP2 envelope (`"FLP2"` magic, rANS). Guessing that layout would be a false codec claim. Envelope byte-parity is tracked against FloppyLM `pack.py` / `rans.py`, the format owner.

**UNMEASURED:** GPU vs CPU error on this host fixture on a Series kit; any generation tok/s; GELU/SwiGLU/QK-norm/`row8log`/`tensor16` for this smoke.

## Fase 3 — Backward + AdamW + STE

Sourced: [docs/ste-adamw.md](ste-adamw.md), [docs/adr/0002-ste-qat-mapping.md](adr/0002-ste-qat-mapping.md).

Tiny relu2 net (`B=2`, `In=4`, `H=4`, `Out=3`). Ternary absmean FakeQuant. STE `∂Q/∂W ≈ 1` with `|W/s| ≤ 1` clip. Host AdamW: `lr=1e-3`, `β1=0.9`, `β2=0.999`, `ε=1e-8`, `wd=0.01`.

| Check | Sourced gate |
| --- | --- |
| STE-identity vs central finite-diff (`h=1e-4`) | max-abs `1e-3`; max-rel `2e-2` when `\|analytic\| ≥ 1e-2` |
| GPU kernel vs CPU (when D3D12 exists) | max-abs `1e-5` / max-rel `1e-4` |

`--train-step 1` is **one** step. Printed `loss_before` / `loss_after` (if a host prints them) are **not** committed here and are **not** a quality curve.

**UNMEASURED:** LLM quality, tok/s for this smoke, Series backward timing of this host, a committed loss table.

## Fase 4 — Memory streaming

Sourced: [docs/memory-budget.md](memory-budget.md), [docs/platform/uwp-resources.md](platform/uwp-resources.md).

| Item | Value | Kind |
| --- | --- | --- |
| UWP **App** planning budget | **~1 GB** (1024 MiB) | Microsoft Learn planning figure — **not** our console measurement |
| Creators **Game** | **~5 GB** (5120 MiB) | **Other** designation only; not assumed for an App package |
| Logical corpus in stress fixture | 2 GiB streamed in 16 MiB chunks | Fixture, never allocated as one tensor |
| Linux implementation host `VmHWM` | **36.08 MiB** vs 1024 MiB plan | Sourced from the Fase 4 host that ran; **desktop RAM** |
| E0 UWP peak app memory (Series S) | **91418624 bytes** | Sourced from the E0 representative trial; **not** the Fase 4 stream-stress host |
| Console AppContainer for Win32 stream-stress | — | **UNVALIDATED** |
| Debugger | can **mask OOM** | Learn fact; **non-debug** package is the gate |

**UNMEASURED:** Series working-set of the historical stream-stress host; VRAM of a console dispatch of that host.

## Fase 5 — QAT + WSD

Sourced: [docs/qat-wsd.md](qat-wsd.md).

**N = 16** = warmup 4 + stable 8 + decay 4. Isolated cooldown `stable-mid` overlays `[8, 12)` and is **not** `16+4`. Dry-run: `python3 scripts/validate_qat_schedule.py examples/qat-wsd-smoke.json --dry-run`.

This is a **schedule smoke only**. It is **not** QAT quality, **not** PPL, **not** a scientific E0 campaign.

Default FakeQuant is **ternary absmean**. 2-bit / 4-bit are **host** midrise lattices (`--bit-width 2|4`). **No new HLSL** in Fase 4–5. `--qat-smoke` does **not** re-dispatch Fase 3 shaders.

**UNMEASURED:** final loss / perplexity / quality tok/s of this host smoke; GPU 2/4-bit FakeQuant parity; cosine WSD (not implemented).

## Fase 6 — Series S|X Dev Mode

Sourced: [docs/console.md](console.md), [docs/evidence/e0-20261001/notes.md](evidence/e0-20261001/notes.md), [throughput.json](evidence/e0-20261001/throughput.json), [PR #17](https://github.com/gianlucamazza/xbox-gpu-training/pull/17) squash `aec1a2a`.

Historical: [PR #15](https://github.com/gianlucamazza/xbox-gpu-training/pull/15) (`66224e05`) reported **`BLOCKED: no console`** for the Win32 host lane. That verdict is **not** rewritten as a denial of the later E0 evidence.

### Measured Series S E0 (PR #17)

Active package `GianlucaMazza.XgpuE0_0.1.0.24_x64__g0p5dcfz4t9z4`, source `6a124021`, E0 UWP CI run 36792707081. Installed via Device Portal on retail Series S. Payloads match CI byte for byte.

| Item | Value | Kind |
| --- | --- | --- |
| Independent GPU operation cases | **52** passed, `hardware_gpu: true`, `purpose: functional` | **Sourced** [kernel-parity.json](evidence/e0-20261001/kernel-parity.json) |
| Held-out model fixtures | **36** passed | **Sourced** [acceptance.json](evidence/e0-20261001/acceptance.json) / [notes.md](evidence/e0-20261001/notes.md) |
| Identical-input AdamW + exact resume | passed | Sourced acceptance |
| Real Dev Home suspension + runner recovery | passed (checkpoint at step 66; 1844-step trunk) | Sourced lifecycle |
| Representative synthetic E0 | d=96 / layers=3 / d_ff=391 / ctx=256 / batch=32 | Config in throughput.json |
| Tokens / wall / tok/s | 147456 / 153.030679 s / **963.571 token/s** | **Sourced** [throughput.json](evidence/e0-20261001/throughput.json) — **functional only, not quality / PPL** |
| Peak app memory | 91418624 bytes | Sourced; **non-debug Release** UWP App (AppContainer planning **~1 GB**) |
| Estimate excludes | corpus upload and Python serialization/evaluation | Written in throughput.json |

`kernel-parity.json` records `oracle: independent PyTorch operations and autograd` and `purpose: functional`. [notes.md](evidence/e0-20261001/notes.md) states this evidence **certifies functional execution, not language-model quality**. Do **not** copy `963.571` token/s into a peer or PPL cell. The companion accepted row16/row8log scientific E0. See E0.1 below for the active engine.

### E0.1 GPU-resident engine (PR #18)

Package `0.1.0.28`, source `25f8bc39`, CI run 36839565773 — [notes](evidence/e0-20261001-resident/notes.md).
On 2026-10-01 the first scientific campaign (package `0.1.0.24`, GPU busy ~8.6% of wall time) was stopped cleanly for the GPU-resident E0.1 engine. Package `0.1.0.28` passed the same acceptance, is bit-identical to `0.1.0.24` on every fixture and trained weight, and measured **10224 token/s** (×10.6) on the representative benchmark ([E0.1 evidence](evidence/e0-20261001-resident/throughput.json)). Campaign `e0-20261001T090514Z-4236fd` runs on it.
Identity proof: [bit-identity.json](evidence/e0-20261001-resident/bit-identity.json). The speed-up is
**functional only**, not quality / PPL, and is not a peer-comparison number.

### Still UNMEASURED

| Target | Status |
| --- | --- |
| Scientific E0 selection, paired seeds, costs, exclusions | pending companion campaign |
| Series X E0 | **UNMEASURED** |
| Matched CPU versus console throughput | **UNMEASURED** |
| PIX `.wpix` / partner capture | **UNMEASURED** |
| Historical Win32 host rows (hello / matmul / FLP2 / stream-stress / qat-smoke) | **UNMEASURED** on console |

Public SKU copy (not our benches): Series X **12 TFLOPS** / **16 GB GDDR6**; Series S **4 TFLOPS** / **10 GB GDDR6** — [series-s-vs-x.md](platform/series-s-vs-x.md). Usable UWP RAM is **not** those GDDR6 sizes.

Four blockers stay accurate ([blockers-fase6-validation.md](platform/blockers-fase6-validation.md)):

1. No primary Microsoft SoT for on-console LLM **training** via DirectML / ORT.
2. UWP App ~1 GB vs Creators Game ~5 GB; debugger can mask OOM.
3. Public GDK is **Windows-only**; GDKX / ID@Xbox **not claimed**.
4. Dev Mode = UWP develop/test; **≠** GDKX.

A successful small E0 run does **not** retire those constraints.

---

## Figure placeholders

**No plots were generated.** Do not treat the boxes below as data. Catalog: [docs/figures/README.md](figures/README.md).

| ID | Intended figure | Why it is empty |
| --- | --- | --- |
| [F1](figures/README.md#f1) | Matmul max-abs / max-rel vs tile | Committed CSV is CPU self-check only; no GPU series |
| [F2](figures/README.md#f2) | FLP2 forward error vs CPU fixture | No committed error series; gate is a tolerance, not a curve |
| [F3](figures/README.md#f3) | Grad-check analytic vs finite-diff | No committed scatter table in-tree |
| [F4](figures/README.md#f4) | Working-set vs App 1024 MiB | One Linux `VmHWM` point; not a Series trace |
| [F5](figures/README.md#f5) | WSD + cooldown LR vs step | Schedule is specified; no plotted run artifact |
| [F6](figures/README.md#f6) | Series S\|X quality / PIX / Series X | Series S **functional** evidence is cited in prose — **no** quality/PPL/PIX/Series X plot |
| [F7](figures/README.md#f7) | This repo vs BitNet | Peer cells **literature cites only** — not a ranking chart |

```
+---------------------------+
|  PLACEHOLDER — NOT A PLOT |
|  F1–F7: no fabricated PNG |
+---------------------------+
```

---

## BitNet / peer comparison (sourced or UNMEASURED)

This is **not** a ranking of xbox-gpu-training against BitNet, bitnet.cpp, or FloppyLM. Hardware, task, and stack differ. We quote **their** published numbers and mark **ours** unmeasured. **Do not** copy Series S E0 `963.571` token/s (or peak memory) into a peer cell.

### Conceptual overlap (not a port)

| Peer | What we take | What we do not claim |
| --- | --- | --- |
| BitNet / BitNet b1.58 (Microsoft Research) | Ternary `{-1,0,+1}` + absmean FakeQuant as a **known** recipe (ADR 0002) | Their PPL, latency, energy, or A100 tok/s as **our** result |
| FloppyLM (`floppy_4mb`) | Model, codec and FLP2 semantics (oracle) | Its scientific results as GPU results; envelope byte-parity |
| Soul Player / ternary15M (ADR 0002) | Train↔deploy codes; STE clip | Their quality numbers |

### BitNet b1.58 — paper numbers (their experiment)

Source: Ma et al., *The Era of 1-bit LLMs: All Large Language Models are in 1.58 Bits*, [arXiv:2402.17764](https://arxiv.org/abs/2402.17764) (Feb 2024). Tables below are **the paper’s** measurements / estimates. FasterTransformer + Ladder 2-bit kernel; **not** Xbox; **not** this repo.

**Table 1 (paper) — perplexity and cost vs their reproduced FP16 LLaMA LLM** (RedPajama, 100B tokens):

| Models (paper) | Size | Memory (GB) | Latency (ms) | PPL |
| --- | --- | --- | --- | --- |
| LLaMA LLM | 700M | 2.08 (1.00×) | 1.18 (1.00×) | 12.33 |
| BitNet b1.58 | 700M | 0.80 (2.60×) | 0.96 (1.23×) | 12.87 |
| LLaMA LLM | 3B | 7.89 (1.00×) | 5.07 (1.00×) | 10.04 |
| BitNet b1.58 | 3B | 2.22 (3.55×) | 1.87 (2.71×) | 9.91 |

**Table 3 (paper) — 70B throughput on two 80 GB A100**, seq 512, pipeline parallel:

| Models (paper) | Max batch | Throughput (tokens/s) |
| --- | --- | --- |
| LLaMA LLM 70B | 16 (1.0×) | **333** (1.0×) |
| BitNet b1.58 70B | 176 (11.0×) | **2977** (8.9×) |

Paper text also estimates **71.4×** lower arithmetic energy for matrix multiply on a **7 nm** model (Horowitz / PokeBNN energy model) — an **estimate**, not a wall-plug Xbox number.

Original 1-bit BitNet: Wang et al., [arXiv:2310.11453](https://arxiv.org/abs/2310.11453). No additional figures copied here.

### This repository (same columns)

| Models | Size | Memory | Latency | PPL | tok/s |
| --- | --- | --- | --- | --- | --- |
| xbox-gpu-training host smoke | tiny fixtures only | **UNMEASURED** as an LLM footprint | **UNMEASURED** | **UNMEASURED** | **UNMEASURED** |
| xbox-gpu-training Series S E0 | functional trainer only | **UNMEASURED** as an LLM footprint (E0 peak bytes are **not** this cell) | **UNMEASURED** | **UNMEASURED** | **UNMEASURED** as quality / PPL (synthetic functional tok/s is **not** copied here) |
| xbox-gpu-training Series X | — | **UNMEASURED** | **UNMEASURED** | **UNMEASURED** | **UNMEASURED** |
| FloppyLM Python oracle (`floppy_4mb`) | — | **UNMEASURED** in this repo | **UNMEASURED** in this repo | **UNMEASURED** in this repo | **UNMEASURED** in this repo |

Do **not** derive a “we are X× vs BitNet” line from the paper table or from the E0 trial. That would be a fabricated ranking.

---

## Limitations (keep these)

- Series S E0 functional execution is measured; scientific quality is **not**.
- No Series X measurement.
- No PIX `.wpix` is attached.
- Historical Win32 `xbox_gpu_host` smokes were **not** deployed as the E0 AppX.
- App Mode on Xbox is **not** full title GPU (FL **11.0**; public notes ~45% GPU share) — [dx12-hlsl-compute.md](platform/dx12-hlsl-compute.md).
- Binary FLP2 envelope is not implemented in this repo.
- 2/4-bit FakeQuant has no GPU kernel on the historical host path.
- Cosine WSD is not implemented on the historical host path.
- ggml is not vendored.
- This page is a reviewed research note, not a paper camera-ready.

## What this repo does not claim

- GDKX / ID@Xbox access
- CUDA / cuDNN on Xbox
- DirectML as trainer or optimizer
- That xllama already does GPU training
- That scientific E0 / Fase 6 quality is complete
- That Series S functional tok/s is a quality / PPL / peer-rank number
- Any tok/s other than the sourced Series S E0 **functional** trial in the Fase 6 section

## Follow-up (not this page)

A human / companion campaign should:

1. Finish the sequential row16/row8log scientific E0 and the reserved final test.
2. Publish generated paired statistics, costs and exclusions only after those gates.
3. Leave Series X, PIX, matched CPU comparison, and historical host rows empty until measured.
4. Do not back-fill this Fase 7 page with invented Series numbers.

## Related

- [ROADMAP.md](../ROADMAP.md)
- [docs/execution-plan.md](execution-plan.md#fase-7--publish-results)
- [docs/architecture.md](architecture.md)
- [docs/console.md](console.md)
- [docs/evidence/e0-20261001/notes.md](evidence/e0-20261001/notes.md)
- [docs/figures/README.md](figures/README.md)
- FloppyLM owner: `Workspace/experiments/floppy_4mb` (ADR 0012)
