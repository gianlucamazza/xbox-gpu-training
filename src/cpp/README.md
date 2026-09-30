# `src/cpp/` — C++ host (Fase 0)

Portable host for CI job **`build-windows`** and the Windows DirectX 12 hello-compute path.

| File | Role |
| --- | --- |
| `main.cpp` | `xbox_gpu_host`: default = device + dispatch; `--smoke` skips GPU. |
| `dx12_device.*` | D3D12 device / queue / fence (Windows). |
| `hello_dispatch.*` | Load or `dxc`-compile hello_compute, PSO, one dispatch, UAV verify. |
| `CMakeLists.txt` | Static lib `xgpu_dx12` + `xbox_gpu_host`. |

On non-Windows the same sources compile and `RunHelloCompute` prints `BLOCKED: no D3D12 device`. That is not a GPU success.

This folder must not pull CUDA, cuDNN, or a DirectML trainer path. DirectML is inference/forward-focused: [docs/platform/directml-scope.md](../../docs/platform/directml-scope.md). Setup: [docs/setup.md](../../docs/setup.md).
