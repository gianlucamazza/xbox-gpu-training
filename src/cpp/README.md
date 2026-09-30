# `src/cpp/` — C++ host (Fase 5)

Portable host for CI job **`build-windows`**, the Windows DirectX 12 hello-compute path, the Fase 1 matmul harness, the Fase 2 FLP2 fixture, Fase 3 STE / AdamW, Fase 4 memory streaming, and Fase 5 QAT/WSD smoke.

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
| `CMakeLists.txt` | Static lib `xgpu_dx12` + `xbox_gpu_host`. |

On non-Windows the same sources compile. `RunHelloCompute`, `RunMatmulBench`, `RunFlp2ForwardFixture`, `RunSteGradCheckHost`, `RunSteTrainStepHost`, and `RunQatSmokeHost` print `BLOCKED: no D3D12 device`. That is not a GPU success. `--cpu-ref` and the CPU sides of `--forward-fixture` / `--grad-check` / `--train-step` / `--stream-stress` / `--qat-smoke` stay green. `--stream-stress` reports `gpu_double_buffer: BLOCKED: no D3D12 device` without inventing a dispatch log. Memory contract: [docs/memory-budget.md](../../docs/memory-budget.md). QAT/WSD: [docs/qat-wsd.md](../../docs/qat-wsd.md).

This folder must not pull CUDA, cuDNN, or a DirectML trainer path. DirectML is inference/forward-focused: [docs/platform/directml-scope.md](../../docs/platform/directml-scope.md). Setup: [docs/setup.md](../../docs/setup.md). ggml note: [docs/ggml-baseline.md](../../docs/ggml-baseline.md).
