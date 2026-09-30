# `src/cpp/` — C++ host (Fase 3)

Portable host for CI job **`build-windows`**, the Windows DirectX 12 hello-compute path, the Fase 1 matmul harness, the Fase 2 FLP2 fixture, and Fase 3 STE / AdamW.

| File | Role |
| --- | --- |
| `main.cpp` | `xbox_gpu_host`: default = hello compute; `--smoke`; `--cpu-ref`; `--bench matmul`; `--forward-fixture`; `--grad-check`; `--train-step`. |
| `dx12_device.*` | D3D12 device / queue / fence (Windows). |
| `hello_dispatch.*` | Load or `dxc`-compile hello_compute, PSO, one dispatch, UAV verify. |
| `cpu_matmul.*` | Portable CPU GEMM + tolerances + `--cpu-ref` tests. ggml is not vendored. |
| `matmul_dispatch.*` | Upload A/B, dispatch `matmul.hlsl`, read back C, write CSV. |
| `cpu_flp2.*` | CPU FLP2 decode + RMSNorm + RoPE + tiny forward. |
| `fixture_loader.*` | JSON fixture (`xbox-gpu-training.fixture.flp2.v1`). |
| `flp2_dispatch.*` | `--forward-fixture` host path. |
| `cpu_ste.*` | Ternary FakeQuant, STE, host AdamW, tiny-net grad-check. |
| `ste_dispatch.*` | `--grad-check` / `--train-step` host path. |
| `CMakeLists.txt` | Static lib `xgpu_dx12` + `xbox_gpu_host`. |

On non-Windows the same sources compile. `RunHelloCompute`, `RunMatmulBench`, `RunFlp2ForwardFixture`, `RunSteGradCheckHost`, and `RunSteTrainStepHost` print `BLOCKED: no D3D12 device`. That is not a GPU success. `--cpu-ref` and the CPU sides of `--forward-fixture` / `--grad-check` / `--train-step` stay green.

This folder must not pull CUDA, cuDNN, or a DirectML trainer path. DirectML is inference/forward-focused: [docs/platform/directml-scope.md](../../docs/platform/directml-scope.md). Setup: [docs/setup.md](../../docs/setup.md). ggml note: [docs/ggml-baseline.md](../../docs/ggml-baseline.md).
