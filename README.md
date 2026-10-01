# xbox-gpu-training

[![CI](https://github.com/gianlucamazza/xbox-gpu-training/actions/workflows/ci.yml/badge.svg)](https://github.com/gianlucamazza/xbox-gpu-training/actions/workflows/ci.yml)
[![E0 UWP](https://github.com/gianlucamazza/xbox-gpu-training/actions/workflows/e0-uwp.yml/badge.svg)](https://github.com/gianlucamazza/xbox-gpu-training/actions/workflows/e0-uwp.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**Quantized transformer training on the Xbox Series S GPU with DirectX 12 compute shaders.**

Xbox has no CUDA, DirectML on console is inference-focused, and a UWP app in Dev Mode
gets about 1 GB of RAM. This repository trains anyway: a full causal transformer
forward/backward written as HLSL compute shaders, AdamW on fp32 master weights, and a
UWP worker that runs jobs on a retail Series S in Dev Mode. Every number is measured
on hardware and committed as evidence, or it is not claimed ([claims policy](docs/claims-policy.md)).

It is the native training backend of [FloppyLM](https://github.com/gianlucamazza/floppylm), which owns the model
semantics, the FLP2 format, the Python oracle and the experiment campaign.

## Status

The E0 trainer passes its full hardware acceptance on Series S (52 operation cases,
36 model fixtures, exact resume, real suspension) and runs the first scientific
campaign on the GPU-resident E0.1 engine. No language-model quality result is
published yet. Package, throughput and open items: **[docs/status.md](docs/status.md)**.

## Two lanes

| Lane                                       | Code                                                                                | State                                                      |
| ------------------------------------------ | ----------------------------------------------------------------------------------- | ---------------------------------------------------------- |
| **E0 trainer** (active)                    | `src/cpp/e0/`, `src/hlsl/e0_tensor.hlsl`, `uwp/`                                    | Accepted on Series S; scientific campaign running          |
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
