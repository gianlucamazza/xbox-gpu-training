# Results

Published results of the project, reviewed by the owner on 2026-10-01. Every value
is tagged per the [claims policy](claims-policy.md). Current package and headline
measurements: [status.md](status.md).

**Summary.** The E0 trainer executes the FloppyLM E0 protocol on a retail Series S
GPU and agrees with independent oracles on every operation, model fixture, optimizer
step and resume check. Its synthetic throughput is measured. This is **functional**
evidence: no language-model quality, Series X, matched-CPU or PIX result exists yet.
The scientific campaign is running; its results will be added here once gated.

## Measurement hosts

Results from different hosts are never mixed.

| Host                        | What ran                                                                       | What it is not                      |
| --------------------------- | ------------------------------------------------------------------------------ | ----------------------------------- |
| Series S, Dev Mode, UWP App | E0 acceptance, lifecycle, recovery, benchmark ([evidence](evidence/README.md)) | Not Series X; not quality; not GDKX |
| Linux host                  | Docs checks; diagnostic-lane CPU paths; Fase 4 working set                     | No D3D12; not Xbox                  |
| GitHub `windows-latest`     | CI builds (`build-windows`, `E0 UWP`); `dxc` compile; possibly WARP            | Not a console; WARP is software     |
| Windows DX12 desktop        | Diagnostic host dispatch when a device exists                                  | Not console AppContainer            |
| Series X                    | Nothing                                                                        | UNMEASURED                          |

## E0 trainer (Series S)

Sourced from the evidence directories; values in [status.md](status.md).

| Claim                                                                        | Evidence                                  | Tag                           |
| ---------------------------------------------------------------------------- | ----------------------------------------- | ----------------------------- |
| GPU operations match independent PyTorch ops/autograd within ADR 0004 bounds | `kernel-parity.json`                      | Sourced                       |
| Held-out model fixtures and identical-input AdamW match the FloppyLM oracle  | `acceptance.json`                         | Sourced                       |
| Interrupted, resumed and uninterrupted runs end bit-identical                | `lifecycle.json`, `runner-recovery.json`  | Sourced                       |
| Real Dev Home suspension checkpoints and recovers                            | `lifecycle.json`                          | Sourced                       |
| E0.1 engine is bit-identical to the previous engine                          | `bit-identity.json`                       | Sourced                       |
| Representative synthetic throughput and peak app memory                      | `throughput.json` (`purpose: functional`) | Sourced, functional only      |
| Language-model quality / perplexity                                          | —                                         | UNMEASURED (campaign pending) |
| Series X, matched CPU, PIX                                                   | —                                         | UNMEASURED                    |

Throughput was measured on a synthetic corpus and excludes corpus upload and Python
evaluation. It is not a quality or peer-comparison number.

Earlier engine generations and the stop of the first campaign: [history.md](history.md).

## Diagnostic lane (Fase 0–5)

Desktop bring-up of the Win32 host. These results certify nothing for E0 and were
never run on a console ([diagnostic/](diagnostic/README.md)).

| Fase | Result                                                                                                                                                             | Tag                                          |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------- |
| 0    | Toolchain notes; hello compute dispatches when a D3D12 device exists                                                                                               | `BLOCKED: no D3D12 device` on Linux/CI       |
| 1    | Matmul tolerances chosen (FP32 `1e-4`/`1e-3`, FP16 `5e-2`/`5e-2`); committed [matmul.csv](../benchmarks/results/matmul.csv) is a CPU self-check (`status=blocked`) | GPU matmul BLOCKED in the committed artifact |
| 2    | Scalar FLP2 decode + RMSNorm + RoPE + tiny forward within `1e-5`/`1e-4` of the CPU fixture                                                                         | Sourced; binary FLP2 envelope out of scope   |
| 3    | STE-identity grad-check within `1e-3` abs / `2e-2` rel; one AdamW step runs                                                                                        | Sourced; no loss curve                       |
| 4    | Linux `VmHWM` 36.08 MiB while streaming a 2 GiB logical corpus against a 1024 MiB plan                                                                             | Sourced on desktop; console UNVALIDATED      |
| 5    | WSD schedule with isolated cooldown overlay; N=16 host smoke                                                                                                       | Schedule smoke only; no quality              |

## Figures

No plots exist; captions only ([figures/README.md](figures/README.md)).

| ID                                                      | Intended figure                 | Why empty                            |
| ------------------------------------------------------- | ------------------------------- | ------------------------------------ |
| [F1](figures/README.md#f1--matmul-parity-vs-tile)       | Matmul error vs tile            | Committed CSV is CPU self-check only |
| [F2](figures/README.md#f2--flp2-forward-vs-cpu-fixture) | FLP2 forward error              | Gate is a tolerance, not a series    |
| [F3](figures/README.md#f3--grad-check)                  | Grad-check scatter              | No committed table                   |
| [F4](figures/README.md#f4--streaming-working-set)       | Working set vs budget           | One desktop point                    |
| [F5](figures/README.md#f5--wsd--isolated-cooldown-lr)   | WSD + cooldown LR               | No plotted run artifact              |
| [F6](figures/README.md#f6--series-sx-benches)           | Series quality / PIX / Series X | UNMEASURED                           |
| [F7](figures/README.md#f7--bitnet--peer-overlay)        | Peer overlay                    | Peer cells are literature only       |

## BitNet / peer comparison

This is not a ranking. Hardware, task and stack differ; the tables below quote the
peers' own published numbers and mark ours UNMEASURED. Never copy E0 functional
throughput or memory into these tables.

### Conceptual overlap

| Peer                     | What we take                                                      | What we do not claim                                         |
| ------------------------ | ----------------------------------------------------------------- | ------------------------------------------------------------ |
| BitNet / BitNet b1.58    | Ternary `{-1,0,+1}` quantization as a known recipe                | Their perplexity, latency, energy or A100 throughput as ours |
| FloppyLM (`gianlucamazza/floppylm`) | Model, quantization, optimizer and FLP2 semantics (oracle)        | Its scientific results as GPU results                        |
| Soul Player / ternary15M | Train↔deploy code agreement; STE clip (diagnostic lane, ADR 0002) | Their quality numbers                                        |

### BitNet b1.58 — paper numbers

Source: Ma et al., _The Era of 1-bit LLMs: All Large Language Models are in 1.58 Bits_,
[arXiv:2402.17764](https://arxiv.org/abs/2402.17764) (Feb 2024). FasterTransformer +
Ladder 2-bit kernel on GPUs; not Xbox, not this repository.

Table 1 (paper) — perplexity and cost versus their reproduced FP16 LLaMA (RedPajama, 100B tokens):

| Model (paper) | Size | Memory (GB)  | Latency (ms) | PPL   |
| ------------- | ---- | ------------ | ------------ | ----- |
| LLaMA LLM     | 700M | 2.08 (1.00×) | 1.18 (1.00×) | 12.33 |
| BitNet b1.58  | 700M | 0.80 (2.60×) | 0.96 (1.23×) | 12.87 |
| LLaMA LLM     | 3B   | 7.89 (1.00×) | 5.07 (1.00×) | 10.04 |
| BitNet b1.58  | 3B   | 2.22 (3.55×) | 1.87 (2.71×) | 9.91  |

Table 3 (paper) — 70B throughput on two 80 GB A100, sequence 512, pipeline parallel:

| Model (paper)    | Max batch   | Throughput (tokens/s) |
| ---------------- | ----------- | --------------------- |
| LLaMA LLM 70B    | 16 (1.0×)   | 333 (1.0×)            |
| BitNet b1.58 70B | 176 (11.0×) | 2977 (8.9×)           |

The paper also estimates 71.4× lower matmul arithmetic energy on a 7 nm model — an
estimate, not a measurement. Original 1-bit BitNet: Wang et al.,
[arXiv:2310.11453](https://arxiv.org/abs/2310.11453).

### This repository, same columns

| Model           | Size                            | Memory                         | Latency    | PPL        | token/s                           |
| --------------- | ------------------------------- | ------------------------------ | ---------- | ---------- | --------------------------------- |
| Series S E0     | small functional configurations | UNMEASURED as an LLM footprint | UNMEASURED | UNMEASURED | UNMEASURED as a comparable figure |
| Series X        | —                               | UNMEASURED                     | UNMEASURED | UNMEASURED | UNMEASURED                        |
| Diagnostic host | tiny fixtures                   | UNMEASURED                     | UNMEASURED | UNMEASURED | UNMEASURED                        |

## Limitations

- Language-model quality is not measured; the campaign is in progress.
- No Series X measurement and no PIX capture.
- Diagnostic-lane workloads were never deployed to a console.
- UWP App mode runs at feature level 11.0 with a partial GPU share, not full title
  GPU access ([platform/dx12-hlsl-compute.md](platform/dx12-hlsl-compute.md)).
- The binary FLP2 envelope is implemented by FloppyLM, not here.
- Diagnostic lane only: no GPU 2/4-bit FakeQuant, no cosine WSD, ggml not vendored.
- This page is a reviewed research note, not a camera-ready paper.
