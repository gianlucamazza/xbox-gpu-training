# Hello compute

Fase 0 example host. Builds `hello_compute` and dispatches [`src/hlsl/hello_compute.hlsl`](../../src/hlsl/hello_compute.hlsl) on a Windows **DirectX 12** device.

The same dispatch path is shared with `xbox_gpu_host` in [`src/cpp/`](../../src/cpp/). Setup, `dxc`, and BLOCKED rules: [docs/setup.md](../../docs/setup.md). Public DX12 notes: [docs/platform/dx12-hlsl-compute.md](../../docs/platform/dx12-hlsl-compute.md).

## What it does

1. Creates a D3D12 device (hardware adapter, then WARP). Public Windows SDK / DirectX 12 — not GDKX/ID@Xbox ([gdk-vs-gdkx.md](../../docs/platform/gdk-vs-gdkx.md)).
2. Loads `hello_compute.cso` or compiles the HLSL with `dxc -T cs_6_0 -E CSMain`.
3. Creates a compute PSO, a 64-uint UAV, dispatches one thread group, reads back.
4. Verifies `Output[i] == i + 1`. A no-op shader would fail this check.

No PIX capture is taken. No tok/s. WARP is a software Windows device, not a Series S|X result.

## Build and run

```bat
cmake -S . -B build -A x64
cmake --build build --config Release --target hello_compute
.\build\Release\hello_compute.exe
```

Linux / no D3D12 prints `BLOCKED: no D3D12 device` and exits 0.

```bat
hello_compute.exe --shader src\hlsl\hello_compute.hlsl
```
