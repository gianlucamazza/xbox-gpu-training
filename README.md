# xbox-gpu-training

[![CI](https://github.com/gianlucamazza/xbox-gpu-training/actions/workflows/ci.yml/badge.svg)](https://github.com/gianlucamazza/xbox-gpu-training/actions/workflows/ci.yml)
[![E0 UWP](https://github.com/gianlucamazza/xbox-gpu-training/actions/workflows/e0-uwp.yml/badge.svg)](https://github.com/gianlucamazza/xbox-gpu-training/actions/workflows/e0-uwp.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**Native GPU trainer for FloppyLM, running on a retail Xbox Series S.**

[FloppyLM](https://github.com/gianlucamazza/floppylm) looks for the best language model that
fits entirely on a 3.5" floppy disk (1 474 560 bytes for weights, tokenizer and runtime together). Its first
experiment, **E0**, measures the *scalar frontier*: small byte-level transformers whose cores are
quantized to ternary or 2-bit weights, trained on TinyStories at miniature budgets. Every later
FloppyLM design must beat that frontier.

This repository is E0's training backend. It runs on a dedicated Series S in Dev Mode, with the
console's constraints: there is no CUDA, DirectML on console is inference-focused, and a UWP app
gets about 1 GB of RAM. Every console number is measured on that hardware and committed as evidence,
or it is not claimed ([claims policy](docs/claims-policy.md)).

## What it trains

- **Models:** byte-level causal transformers (vocabulary 256) at miniature size. The benchmark
  configuration is in [docs/status.md](docs/status.md). They are not large language models.
- **Quantization:** quantization-aware training with FloppyLM's own quantizer (ternary or
  2-bit cores, fp16 scales, identity STE), checked op by op against FloppyLM's Python oracle.
- **Split of work:** the GPU runs the whole forward and backward pass as HLSL compute shaders.
  The app's CPU keeps the fp32 master weights and runs AdamW and the WSD schedule with its
  three cooldown branches.
- **E0.1** is the current execution engine. It keeps tensors resident on the GPU and is
  bit-identical to the previous engine ([docs/e0/engine.md](docs/e0/engine.md)).

FloppyLM owns the model semantics, the FLP2 format, the oracle, the fixtures, corpus
preparation and the campaign runner. Running E0 needs both repositories: FloppyLM drives the
console through the job protocol described in [docs/e0/job-protocol.md](docs/e0/job-protocol.md).

## Status

The E0 trainer passes its full hardware acceptance on Series S (52 operation cases,
36 model fixtures, exact resume, real suspension). Scientific state is owned by
[FloppyLM](https://github.com/gianlucamazza/floppylm/blob/main/docs/STATUS.md);
no language-model quality result is published yet. Package, throughput and open items:
**[docs/status.md](docs/status.md)**.

## Two lanes

| Lane                                       | Code                                                                                | State                                                      |
| ------------------------------------------ | ----------------------------------------------------------------------------------- | ---------------------------------------------------------- |
| **E0 trainer** (active)                    | `src/cpp/e0/`, `src/hlsl/e0_tensor.hlsl`, `uwp/`                                    | Accepted on Series S; scientific state in FloppyLM          |
| **Diagnostic host** (historical, Fase 0–5) | `src/cpp/` (`xbox_gpu_host`), other `src/hlsl/` kernels, `examples/`, `benchmarks/` | Desktop bring-up of DX12 compute; certifies nothing for E0 |

## Start here

| Goal                                             | Read                                                                                                    |
| ------------------------------------------------ | ------------------------------------------------------------------------------------------------------- |
| Understand the design                            | [docs/architecture.md](docs/architecture.md), [ADRs](docs/adr/README.md)                                |
| Build, install and run E0 on a console           | [docs/e0/runbook.md](docs/e0/runbook.md)                                                                |
| Understand the E0 code                           | [docs/e0/engine.md](docs/e0/engine.md), [job protocol](docs/e0/job-protocol.md)                         |
| Check what is measured                           | [docs/status.md](docs/status.md), [evidence index](docs/evidence/README.md), [results](docs/results.md) |
| Platform constraints (Dev Mode, UWP memory, GDK) | [docs/platform/](docs/platform/README.md)                                                               |
| Everything else                                  | [docs/README.md](docs/README.md)                                                                        |

## Quick check

```bash
python3 scripts/check_required_docs.py && python3 scripts/check_relative_links.py && python3 scripts/check_doc_claims.py
cmake -S . -B build && cmake --build build
./build/xgpu_e0_train --help
```

Windows toolchain and diagnostic-host commands: [docs/diagnostic/setup.md](docs/diagnostic/setup.md).

## Layout

| Path                                    | Content                                                                |
| --------------------------------------- | ---------------------------------------------------------------------- |
| `src/cpp/e0/`                           | E0 tensor runtime, model, DX12 kernel, job runner, `xgpu_e0_train` CLI |
| `src/hlsl/e0_tensor.hlsl`               | E0 multi-op compute shader                                             |
| `uwp/`                                  | x64 UWP worker app (`XgpuE0`)                                          |
| `src/cpp/`, `src/hlsl/`                 | Diagnostic host and its kernels                                        |
| `scripts/`                              | Doc checks, E0 UWP build, QAT schedule validator                       |
| `benchmarks/`, `examples/`, `fixtures/` | Diagnostic-lane harnesses and fixtures                                 |
| `docs/`                                 | Documentation ([map](docs/README.md))                                  |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Code, docs and commits are in English.

## License

[MIT](LICENSE).
