# `src/cpp/` — C++ host (Fase 1)

Portable host for CI job **`build-windows`**, the Windows DirectX 12 hello-compute path, and the Fase 1 matmul harness.

| File | Role |
| --- | --- |
| `main.cpp` | `xbox_gpu_host`: default = hello compute; `--smoke` skips GPU; `--cpu-ref`; `--bench matmul`. |
| `dx12_device.*` | D3D12 device / queue / fence (Windows). |
| `hello_dispatch.*` | Load or `dxc`-compile hello_compute, PSO, one dispatch, UAV verify. |
| `cpu_matmul.*` | Portable CPU GEMM + tolerances + `--cpu-ref` tests. ggml is not vendored. |
| `matmul_dispatch.*` | Upload A/B, dispatch `matmul.hlsl`, read back C, write CSV. |
| `CMakeLists.txt` | Static lib `xgpu_dx12` + `xbox_gpu_host`. |

On non-Windows the same sources compile. `RunHelloCompute` and `RunMatmulBench` print `BLOCKED: no D3D12 device`. That is not a GPU success. `--cpu-ref` stays green.

This folder must not pull CUDA, cuDNN, or a DirectML trainer path. DirectML is inference/forward-focused: [docs/platform/directml-scope.md](../../docs/platform/directml-scope.md). Setup: [docs/setup.md](../../docs/setup.md). ggml note: [docs/ggml-baseline.md](../../docs/ggml-baseline.md).
