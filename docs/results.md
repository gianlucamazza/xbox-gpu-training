# Fase 7 — Published results (honest)

**Status: host work documented; Series S E0 functional execution MEASURED; remaining console metrics `UNMEASURED` / `UNVALIDATED`.** This page does **not** invent tok/s, quality curves, Series benches, or peer rankings beyond what [PR #17](https://github.com/gianlucamazza/xbox-gpu-training/pull/17) (`aec1a2a`) already recorded on `main`. It does **not** claim **GDKX**, **ID@Xbox**, CUDA, or DirectML-as-trainer. It does **not** call this research “public” until a human reviews this file.

[PR #15](https://github.com/gianlucamazza/xbox-gpu-training/pull/15) (`66224e05`) left Fase 6 as **`BLOCKED: no console`**. That was honest for the Win32 host lane. #17 later landed a separate E0 UWP backend with Device Portal evidence on retail Series S. This page **cites** that measured E0 and keeps every other Series / quality / PIX / peer cell empty.

Playbook: [docs/execution-plan.md](execution-plan.md#fase-7--publish-results). Console table: [docs/console.md](console.md). Evidence: [docs/evidence/e0-20261001/notes.md](evidence/e0-20261001/notes.md). Figure catalog: [docs/figures/README.md](figures/README.md).

## Sintesi IT

Fasi 0–5: lavoro host (Windows DX12 quando c’è un device; Linux/CI senza D3D12 in `BLOCKED: no D3D12 device`). Fase 6: [PR #17](https://github.com/gianlucamazza/xbox-gpu-training/pull/17) ha misurato l’E0 UWP su Series S (52 operation / 36 model fixtures; trial sintetico **963.571 token/s**). Qualità scientifica, Series X, PIX, confronto CPU e i workload Win32 storici restano **UNMEASURED**. Nessun tok/s inventato oltre quell’E0. Confronto BitNet solo con numeri **loro** (paper) o **UNMEASURED** da questa parte. Companion CPU: [gianlucamazza/xllama](https://github.com/gianlucamazza/xllama) (FloppyLM). xllama **non** modificato; `feat/floppylm-training` **non** toccato.

## English abstract

Fase 0–5 measured **host** contracts (CPU fixtures, chosen tolerances, one Linux working-set). GPU dispatch on a Windows D3D12 device is **not** Series hardware. Fase 6 on `main` now includes measured Series S E0 functional execution from [PR #17](https://github.com/gianlucamazza/xbox-gpu-training/pull/17). This Fase 7 page publishes that state and leaves remaining Series / quality / PIX cells **UNMEASURED**. BitNet numbers below are **paper-sourced** or marked **UNMEASURED** on our side. Companion CPU / FloppyLM path: [gianlucamazza/xllama](https://github.com/gianlucamazza/xllama).

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
| **Windows DX12 box** (when a contributor has one) | Hello / matmul / FLP2 / STE dispatch **if** a device exists | Still not console AppContainer |
| **Series S Dev Mode** | E0 UWP package `0.1.0.24` (source `6a124021`) via Device Portal — [PR #17](https://github.com/gianlucamazza/xbox-gpu-training/pull/17) | Not Series X. Not scientific quality. Not the Win32 host smokes. |
| **Series X Dev Mode** | **Nothing measured** | **UNMEASURED** |
| **GDKX / ID@Xbox kit** | **Not claimed** | Partner/NDA path |

## Companion — FloppyLM CPU path

The FloppyLM **CPU** path lives in **[gianlucamazza/xllama](https://github.com/gianlucamazza/xllama)** (conceptual FLP2 notes: xllama `docs/floppylm.md` and the draft FloppyLM ADRs). This repository is the **GPU research track** only.

- Do **not** claim that xllama trains on the GPU.
- Do **not** modify xllama from here (including `feat/floppylm-training`).
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
| 5 | Schedule dry-run + host `--qat-smoke --steps 16` (WSD + isolated cooldown overlay). Default ternary FakeQuant | [qat-wsd.md](qat-wsd.md), [qat-wsd-smoke.json](../examples/qat-wsd-smoke.json) | No quality / tok/s. No new HLSL. 2/4-bit = host FakeQuant only. |
| 6 | Series S E0 UWP functional execution + representative synthetic throughput | [console.md](console.md), [e0-20261001](evidence/e0-20261001/notes.md), PR #17 `aec1a2a` | E0 **MEASURED**. Quality / Series X / PIX / Win32 host rows **UNMEASURED**. |
| 7 | This page: sourced host + sourced E0; figure placeholders; BitNet paper-or-UNMEASURED | this file, [figures/README.md](figures/README.md) | Draft for human review. Not “public” yet. |

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

**Not** implemented: on-disk FLP2 envelope (`"FLP2"` magic, rANS). Guessing that layout would be a false codec claim. Research issue for envelope byte-parity remains open until xllama publishes a stable on-`main` contract.

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

Default FakeQuant is **ternary absmean**. 2-bit / 4-bit are **host** midrise lattices (`--bit-width 2|4`). **No new HLSL** in Fase 4–5. `--qat-smoke` does **not** re-dispatch Fase 3 shaders.

**UNMEASURED:** final loss / perplexity / tok/s of this host smoke; GPU 2/4-bit FakeQuant parity; cosine WSD (not implemented).

## Fase 6 — Series S|X Dev Mode

Sourced: [docs/console.md](console.md), [docs/evidence/e0-20261001/notes.md](evidence/e0-20261001/notes.md), [throughput.json](evidence/e0-20261001/throughput.json), [PR #17](https://github.com/gianlucamazza/xbox-gpu-training/pull/17) squash `aec1a2a`.

Historical: [PR #15](https://github.com/gianlucamazza/xbox-gpu-training/pull/15) (`66224e05`) reported **`BLOCKED: no console`** for the Win32 host lane. That verdict is **not** rewritten as a denial of the later E0 evidence.

### Measured Series S E0 (PR #17)

Active package `GianlucaMazza.XgpuE0_0.1.0.24_x64__g0p5dcfz4t9z4`, source `6a124021`, E0 UWP CI run 36792707081. Installed via Device Portal on retail Series S. Payloads match CI byte for byte.

| Item | Value | Kind |
| --- | --- | --- |
| Independent GPU operation cases | 52 passed | Sourced acceptance |
| Held-out model fixtures | 36 passed | Sourced acceptance |
| Identical-input AdamW + exact resume | passed | Sourced acceptance |
| Real Dev Home suspension + runner recovery | passed (checkpoint at step 66; 1844-step trunk) | Sourced lifecycle |
| Representative synthetic E0 | d=96 / layers=3 / d_ff=391 / ctx=256 / batch=32 | Config in throughput.json |
| Tokens / wall / tok/s | 147456 / 153.030679 s / **963.571 token/s** | Sourced throughput |
| Peak app memory | 91418624 bytes | Sourced; non-debug Release package |
| Estimate excludes | corpus upload and Python serialization/evaluation | Written in throughput.json |

This is a short functional benchmark. It does **not** establish language-model quality. The companion accepted row16/row8log scientific E0; the full campaign and reserved final test still need to finish.

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
| [F6](figures/README.md#f6) | Series S\|X tok/s / quality | E0 number is sourced in prose; **no plot**. Quality / Series X **UNMEASURED** |
| [F7](figures/README.md#f7) | This repo vs BitNet | Our quality columns **UNMEASURED** — not a ranking chart |

```
+---------------------------+
|  PLACEHOLDER — NOT A PLOT |
|  F1–F7: no fabricated PNG |
+---------------------------+
```

---

## BitNet / peer comparison (sourced or UNMEASURED)

This is **not** a ranking of xbox-gpu-training against BitNet, bitnet.cpp, or FloppyLM. Hardware, task, and stack differ. We quote **their** published numbers and mark **ours** unmeasured unless a cell is sourced above.

### Conceptual overlap (not a port)

| Peer | What we take | What we do not claim |
| --- | --- | --- |
| BitNet / BitNet b1.58 (Microsoft Research) | Ternary `{-1,0,+1}` + absmean FakeQuant as a **known** recipe (ADR 0002) | Their PPL, latency, energy, or A100 tok/s as **our** result |
| FloppyLM / xllama CPU | Scalar FLP2 lattices as conceptual decode | GPU training inside xllama; envelope byte-parity |
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
| xbox-gpu-training Series S E0 (synthetic functional) | d=96 / 3 layers (not an LLM size class) | 91418624 bytes peak app (sourced) | **UNMEASURED** as paper latency | **UNMEASURED** | **963.571** (sourced E0 trial only) |
| xbox-gpu-training Series X | — | **UNMEASURED** | **UNMEASURED** | **UNMEASURED** | **UNMEASURED** |
| FloppyLM / xllama CPU (companion) | — | **UNMEASURED** in this repo | **UNMEASURED** in this repo | **UNMEASURED** in this repo | **UNMEASURED** in this repo |

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
- This page is a **draft-for-review** research note, not a paper camera-ready.

## What this repo does not claim

- GDKX / ID@Xbox access
- CUDA / cuDNN on Xbox
- DirectML as trainer or optimizer
- That xllama already does GPU training
- That scientific E0 / Fase 6 quality is complete
- That Fase 7 makes the work “public” without human review of this file
- Any tok/s other than the sourced Series S E0 trial above

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
- Companion: [gianlucamazza/xllama](https://github.com/gianlucamazza/xllama)
