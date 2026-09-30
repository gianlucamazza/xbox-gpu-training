# `src/cpp/` — C++ host stub

Minimal portable host used as the compile smoke target for job **`build-windows`**.

| File | Role |
| --- | --- |
| `main.cpp` | Prints a not-implemented banner and exits 0. |
| `CMakeLists.txt` | Builds `xbox_gpu_host`. |

DirectX 12 device creation, PIX markers, chunk streaming, and HLSL dispatch belong to later phases ([docs/execution-plan.md](../../docs/execution-plan.md)). This folder must not pull CUDA, cuDNN, or a DirectML trainer path.

C# host is omitted on purpose (one real C++ stub is enough).
