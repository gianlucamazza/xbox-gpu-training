# `src/cpp/` — C++ sources

Two lanes share this folder ([docs/architecture.md](../../docs/architecture.md)):

- **`e0/`** — the active E0 trainer library and CLI: [e0/README.md](e0/README.md).
- **Everything else** — the historical diagnostic host `xbox_gpu_host` (Fase 0–5): hello compute, matmul harness, FLP2 fixture, STE / AdamW, streaming, QAT/WSD smoke ([docs/diagnostic/](../../docs/diagnostic/README.md)).

`dx12_device.*` is shared by both lanes.

| File | Role |
| --- | --- |
| `main.cpp` | `xbox_gpu_host`: default = hello compute; `--smoke`; `--cpu-ref`; `--bench matmul`; `--forward-fixture`; `--grad-check`; `--train-step`; `--stream-stress`; `--qat-smoke`. |
| `dx12_device.*` | D3D12 device / queue / fence (Windows). |
| `hello_dispatch.*` | Load or `dxc`-compile hello_compute, PSO, one dispatch, UAV verify. |
| `cpu_matmul.*` | Portable CPU GEMM + tolerances + `--cpu-ref` tests. ggml is not vendored. |
| `matmul_dispatch.*` | Upload A/B, dispatch `matmul.hlsl`, read back C, write CSV. |
| `cpu_flp2.*` | CPU FLP2 decode + RMSNorm + RoPE + tiny forward. |
| `fixture_loader.*` | JSON fixture (`xbox-gpu-training.fixture.flp2.v1`). |
| `flp2_dispatch.*` | `--forward-fixture` host path. |
| `cpu_ste.*` | Ternary + host 2/4-bit FakeQuant, STE, host AdamW, tiny-net grad-check, QAT smoke. |
| `qat_schedule.*` | WSD + isolated cooldown JSON (`xbox-gpu-training.qat.wsd.v1`). |
| `ste_dispatch.*` | `--grad-check` / `--train-step` / `--qat-smoke` host path. |
| `stream_buffer.*` | Chunk fill + 2-slot host double buffer + working-set sample. |
| `stream_stress.*` | `--stream-stress --budget-mb` host path. Optional GPU tile copies. |
| `e0/` | E0 trainer: static lib `xgpu_e0` + CLI `xgpu_e0_train`. |
| `CMakeLists.txt` | `xgpu_dx12`, `xbox_gpu_host`, `xgpu_e0`, `xgpu_e0_train`. |

On non-Windows the same sources compile. `RunHelloCompute`, `RunMatmulBench`, `RunFlp2ForwardFixture`, `RunSteGradCheckHost`, `RunSteTrainStepHost`, and `RunQatSmokeHost` print `BLOCKED: no D3D12 device`. That is not a GPU success. `--cpu-ref` and the CPU sides of `--forward-fixture` / `--grad-check` / `--train-step` / `--stream-stress` / `--qat-smoke` stay green. `--stream-stress` reports `gpu_double_buffer: BLOCKED: no D3D12 device` without inventing a dispatch log. Memory contract: [docs/diagnostic/memory-budget.md](../../docs/diagnostic/memory-budget.md). QAT/WSD: [docs/diagnostic/qat-wsd.md](../../docs/diagnostic/qat-wsd.md).

No CUDA, cuDNN or DirectML trainer paths ([claims policy](../../docs/claims-policy.md)). Setup: [docs/diagnostic/setup.md](../../docs/diagnostic/setup.md). ggml note: [docs/diagnostic/ggml-baseline.md](../../docs/diagnostic/ggml-baseline.md).
