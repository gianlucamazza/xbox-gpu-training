# Environment setup (Fase 0)

Windows **DirectX 12** host path for this repository. Public **GDK** / **Windows SDK** only. This page does **not** claim **GDKX** or **ID@Xbox** access.

Platform facts (Microsoft/Xbox public documentation, not our benches) live under [`docs/platform/`](platform/dev-mode.md). This file is the **how to install and run** note; those packs are the **source of truth** for Dev Mode, GDK vs GDKX, UWP budgets, and DirectML scope.

## What Fase 0 is

| This phase does | This phase does not |
| --- | --- |
| Document public GDK, Windows SDK, DirectX 12, `dxc`, PIX | Claim GDKX / ID@Xbox |
| Create a Windows **DX12** device and dispatch `hello_compute` | Invent dispatch logs if no device exists |
| Compile HLSL with `dxc` when the tool is present | Treat a missing `dxc` as a GPU success |
| Keep CI jobs `lint-docs` and `build-windows` | Pin CMake to `-G "Visual Studio 17 2022"` |
| Report `BLOCKED: …` honestly | Invent tok/s, PIX captures, or console numbers |

Fase 0–5 Windows work is **not** gated on a Dev Mode console. Console validation is Fase 6: [blockers-fase6-validation.md](platform/blockers-fase6-validation.md).

## Toolchain

### Windows SDK

Install the [Windows SDK](https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/). You need:

- `d3d12.h` / `dxgi1_6.h` and `d3d12.lib` / `dxgi.lib` for the host
- `dxc.exe` (usually `Windows Kits\10\bin\<version>\x64\dxc.exe`) for HLSL → DXIL

GitHub-hosted `windows-latest` already has a Windows SDK. This cloud/Linux agent does **not**.

### Public GDK

The public [microsoft/GDK](https://github.com/microsoft/GDK) is optional for Fase 0. The hello-compute host is a **Win32 desktop** exe that links the system DirectX 12 runtime. It is **not** an Xbox console package.

Public GDK Purpose is **Windows titles only** and does **not** include Xbox Series console targeting. Full console APIs, title entitlements, and Microsoft-provisioned kits are the **GDKX** / **ID@Xbox** path. This repo does **not** claim that path.

Detail: [gdk-vs-gdkx.md](platform/gdk-vs-gdkx.md). Dev Mode (retail UWP develop/test) is a separate public path: [dev-mode.md](platform/dev-mode.md).

### DirectX 12

Research path: **DirectX 12** compute shaders in **HLSL**, Shader Model 6.0 (`cs_6_0`) for this hello shader. On Series X|S UWP the documented hardware feature level is **11.0**. App Mode is **not** full title GPU.

Detail: [dx12-hlsl-compute.md](platform/dx12-hlsl-compute.md). UWP App ~1 GB vs Creators Game ~5 GB planning budgets: [uwp-resources.md](platform/uwp-resources.md).

Xbox has **no CUDA**. **DirectML** is inference/forward-focused and is **not** the trainer here: [directml-scope.md](platform/directml-scope.md).

### `dxc` (DirectX Shader Compiler)

Compile the Fase 0 shader with the Windows SDK `dxc`:

```bat
dxc -T cs_6_0 -E CSMain -Fo build\hello_compute.cso src\hlsl\hello_compute.hlsl
```

Also supported (Fase 1 stub only — not a real matmul):

```bat
dxc -T cs_6_0 -E CSMain -Fo build\matmul.cso src\hlsl\matmul.hlsl
```

If `dxc` is missing, CI and CMake print a skip notice. That is a missing-toolchain signal, **not** a GPU result.

Upstream releases: [microsoft/DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler).

### PIX

[PIX on Windows](https://devblogs.microsoft.com/pix/download/) is the public GPU capture/timing tool for DirectX 12.

Fase 0 documents the install. This repository does **not** include a PIX capture, a `.wpix` artifact, or a timing table. Do not invent one.

Later (Fase 6) a real capture on Dev Mode hardware may be attached, or the phase reports `BLOCKED: no console`.

## CMake (do not pin Visual Studio 2022)

`windows-latest` images may ship a newer Visual Studio than 2022. **Do not** pass `-G "Visual Studio 17 2022"`. Let CMake auto-detect the generator and pass `-A x64`:

```bat
REM from an x64 Native Tools / VS developer prompt
cmake -S . -B build -A x64
cmake --build build --config Release
```

The playbook bat in [docs/execution-plan.md](execution-plan.md) still shows a VS 2022 generator as a historical local example. **CI and this setup note use auto-detect + `-A x64`.**

Host binaries are forced to the build root so both of these layouts work:

- `build\Release\xbox_gpu_host.exe` (this repo’s CMake)
- `build\src\cpp\Release\xbox_gpu_host.exe` (if someone drops the output-directory override)

The same applies to `hello_compute.exe`.

Linux / this cloud agent (no D3D12):

```bash
cmake -S . -B build && cmake --build build
./build/xbox_gpu_host --smoke
./build/xbox_gpu_host
# prints BLOCKED: no D3D12 device — honest, not a GPU success
```

## Hello compute

| Piece | Path |
| --- | --- |
| Shader | [`src/hlsl/hello_compute.hlsl`](../src/hlsl/hello_compute.hlsl) — `CSMain`, `[numthreads(64, 1, 1)]`, writes `Output[i] = i + 1` |
| Shared host | [`src/cpp/`](../src/cpp/) — device create, DXIL load or `dxc` compile, compute PSO, one dispatch, UAV readback |
| Example exe | [`examples/hello-compute/`](../examples/hello-compute/) — `hello_compute` |
| Smoke / default exe | `xbox_gpu_host` (default = hello compute; `--smoke` skips device create) |

On Windows:

```bat
.\build\Release\xbox_gpu_host.exe
.\build\Release\hello_compute.exe
.\build\Release\xbox_gpu_host.exe --shader src\hlsl\hello_compute.hlsl
```

Honest first lines:

- `STATUS: hello_compute dispatched` — a D3D12 device existed, the shader ran, UAV verify passed. WARP is a **software** Windows device, not Series S\|X hardware.
- `BLOCKED: no D3D12 device` — factory/device create failed (including WARP). Exit 0. Not a GPU result.
- `BLOCKED: no compiled shader (dxc missing or HLSL not found)` — device existed but no DXIL and no `dxc`. Exit 0.
- `FAILED: …` — a device existed and the pipeline, dispatch, or verify failed. Exit 1.

Do **not** invent a dispatch log when the process never created a device.

`matmul.hlsl` stays a Fase 1 stub.

## Docs lint

```bash
python3 scripts/check_required_docs.py
python3 scripts/check_glossary.py
python3 scripts/check_relative_links.py
```

## Related platform packs (do not duplicate)

| Pack | Use when |
| --- | --- |
| [gdk-vs-gdkx.md](platform/gdk-vs-gdkx.md) | Public GDK vs GDKX / ID@Xbox |
| [dev-mode.md](platform/dev-mode.md) | Retail Dev Mode, ≤3 consoles, UWP deploy |
| [dx12-hlsl-compute.md](platform/dx12-hlsl-compute.md) | FL 11.0, compute shader path, no CUDA |
| [uwp-resources.md](platform/uwp-resources.md) | App 1 GB / Creators 5 GB; debugger masks OOM |
| [directml-scope.md](platform/directml-scope.md) | DirectML is not the trainer |
| [series-s-vs-x.md](platform/series-s-vs-x.md) | Public SKU specs, not our benches |
| [blockers-fase6-validation.md](platform/blockers-fase6-validation.md) | Why console tables wait |
