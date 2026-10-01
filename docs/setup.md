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
| Fase 1: real matmul HLSL + CPU baseline + CSV | Treat DirectML as the matmul trainer; vendor ggml unless documented |
| Fase 2: RMSNorm / RoPE / FLP2 decode + tiny fixture | Unpack a guessed binary FLP2 envelope; modify xllama |
| Fase 3: STE + host AdamW + `--grad-check` / `--train-step 1` | Invent loss curves / tok/s; treat DirectML as the optimizer |
| Fase 5: QAT/WSD schedule + `--qat-smoke --steps 16` | Invent quality curves; skip cooldown isolation; add CUDA |
| Fase 6: [docs/console.md](console.md) deploy / PIX / `BLOCKED` table | Invent Series S\|X tok/s; claim GDKX / ID@Xbox; treat DirectML as trainer |

Fase 0–5 Windows work is **not** gated on a Dev Mode console. Console validation is Fase 6: [docs/console.md](console.md), [blockers-fase6-validation.md](platform/blockers-fase6-validation.md). The separate E0 UWP lane has measured Series S evidence; see console status.

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

Fase 1 matmul (real kernel, not a no-op). `CSMain` is the FP32 alias so CI can keep `-E CSMain`:

```bat
dxc -T cs_6_0 -E CSMain     -Fo build\matmul.cso      src\hlsl\matmul.hlsl
dxc -T cs_6_0 -E CSMainFP32 -Fo build\matmul_fp32.cso src\hlsl\matmul.hlsl
dxc -T cs_6_0 -E CSMainFP16 -Fo build\matmul_fp16.cso src\hlsl\matmul.hlsl
```

If `dxc` is missing, CI and CMake print a skip notice. That is a missing-toolchain signal, **not** a GPU result.

Upstream releases: [microsoft/DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler).

### PIX

[PIX on Windows](https://devblogs.microsoft.com/pix/download/) is the public GPU capture/timing tool for DirectX 12.

Fase 0 documents the install. This repository does **not** include a PIX capture, a `.wpix` artifact, or a timing table. Do not invent one.

Fase 6 checklist: [docs/console.md](console.md#pix-capture-checklist). PIX remains unmeasured: no Dev Mode capture or `.wpix`. PIX on Windows install notes are **not** a console timing result.

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

## Matmul (Fase 1)

| Piece | Path |
| --- | --- |
| Shader | [`src/hlsl/matmul.hlsl`](../src/hlsl/matmul.hlsl) — `CSMain` / `CSMainFP32` / `CSMainFP16` |
| CPU reference | [`src/cpp/cpu_matmul.*`](../src/cpp/cpu_matmul.h) — portable GEMM; ggml not vendored ([docs/ggml-baseline.md](ggml-baseline.md)) |
| Harness | `xbox_gpu_host --bench matmul --out benchmarks\results\matmul.csv` |

```bat
.\build\Release\xbox_gpu_host.exe --cpu-ref
.\build\Release\xbox_gpu_host.exe --bench matmul --out benchmarks\results\matmul.csv
```

Linux / no D3D12:

```bash
./build/xbox_gpu_host --cpu-ref
./build/xbox_gpu_host --bench matmul --out benchmarks/results/matmul.csv
# BLOCKED: no D3D12 device — CSV status=blocked. CPU tests still run. Dispatch log not invented.
```

Tolerances (chosen TBD): FP32 max-abs `1e-4` / max-rel `1e-3`; FP16 max-abs `5e-2` / max-rel `5e-2`. No tok/s column.

## FLP2 forward (Fase 2)

| Piece | Path |
| --- | --- |
| Shaders | [`src/hlsl/rmsnorm.hlsl`](../src/hlsl/rmsnorm.hlsl), [`rope.hlsl`](../src/hlsl/rope.hlsl), [`flp2_decode.hlsl`](../src/hlsl/flp2_decode.hlsl), [`flp2_forward.hlsl`](../src/hlsl/flp2_forward.hlsl) |
| CPU reference | [`src/cpp/cpu_flp2.*`](../src/cpp/cpu_flp2.h) |
| Fixture | [`benchmarks/fixtures/tiny_flp2.json`](../benchmarks/fixtures/tiny_flp2.json) |
| Contract | [`docs/flp2-forward.md`](flp2-forward.md) |

```bat
dxc -T cs_6_0 -E CSMain -Fo build\rmsnorm.cso      src\hlsl\rmsnorm.hlsl
dxc -T cs_6_0 -E CSMain -Fo build\rope.cso         src\hlsl\rope.hlsl
dxc -T cs_6_0 -E CSMain -Fo build\flp2_decode.cso  src\hlsl\flp2_decode.hlsl
dxc -T cs_6_0 -E CSMain -Fo build\flp2_forward.cso src\hlsl\flp2_forward.hlsl
.\build\Release\xbox_gpu_host.exe --forward-fixture fixtures\tiny_flp2.json
```

Linux / no D3D12:

```bash
./build/xbox_gpu_host --forward-fixture benchmarks/fixtures/tiny_flp2.json
# CPU fixture match, then BLOCKED: no D3D12 device. Dispatch log not invented.
```

Tolerance (chosen TBD): max-abs `1e-5` / max-rel `1e-4`. Binary FLP2 envelope is **not** unpacked.

## STE + AdamW (Fase 3)

| Piece | Path |
| --- | --- |
| Mapping | [`docs/adr/0002-ste-qat-mapping.md`](adr/0002-ste-qat-mapping.md) |
| Contract | [`docs/ste-adamw.md`](ste-adamw.md) |
| Shaders | [`src/hlsl/fakequant_ternary.hlsl`](../src/hlsl/fakequant_ternary.hlsl), [`matmul_grad.hlsl`](../src/hlsl/matmul_grad.hlsl), [`relu2_grad.hlsl`](../src/hlsl/relu2_grad.hlsl), [`ste_backward.hlsl`](../src/hlsl/ste_backward.hlsl) |
| Host | `xbox_gpu_host --grad-check` / `--train-step 1` (AdamW on master fp32) |

```bat
dxc -T cs_6_0 -E CSMain -Fo build\fakequant_ternary.cso src\hlsl\fakequant_ternary.hlsl
dxc -T cs_6_0 -E CSMain -Fo build\matmul_grad.cso       src\hlsl\matmul_grad.hlsl
dxc -T cs_6_0 -E CSMain -Fo build\relu2_grad.cso        src\hlsl\relu2_grad.hlsl
dxc -T cs_6_0 -E CSMain -Fo build\ste_backward.cso      src\hlsl\ste_backward.hlsl
.\build\Release\xbox_gpu_host.exe --grad-check
.\build\Release\xbox_gpu_host.exe --train-step 1
```

Linux / no D3D12:

```bash
./build/xbox_gpu_host --grad-check
./build/xbox_gpu_host --train-step 1
# CPU table / one step, then BLOCKED: no D3D12 device. Dispatch log not invented.
```

Grad-check tolerance (chosen TBD): STE-identity max-abs `1e-3`; max-rel `2e-2` when `|analytic| ≥ 1e-2` (smaller grads are abs-gated). One measured train-step loss pair is **not** a quality curve.

## Memory stream (Fase 4)

| Piece | Path |
| --- | --- |
| Contract | [`docs/memory-budget.md`](memory-budget.md) |
| SoT caps | [`docs/platform/uwp-resources.md`](platform/uwp-resources.md) — App **~1 GB**; Creators Game **~5 GB** |
| Fixture | [`benchmarks/fixtures/stream_stress.json`](../benchmarks/fixtures/stream_stress.json) |
| Host | `xbox_gpu_host --stream-stress --budget-mb 1024` |

```bat
.\build\Release\xbox_gpu_host.exe --stream-stress --budget-mb 1024
```

Linux / no D3D12:

```bash
./build/xbox_gpu_host --stream-stress --budget-mb 1024
# Host double-buffer stream; peak working-set printed.
# gpu_double_buffer: BLOCKED: no D3D12 device. Console AppContainer UNVALIDATED.
```

No new HLSL kernel. GPU work (when a D3D12 device exists) is a ping-pong `CopyBufferRegion` of two tiles. Debugger can mask OOM; the **non-debug** package is the gate. **Do not assume Game designation** in an App package. Desktop / CI peak working-set is **not** a Series S|X number.

## QAT + WSD (Fase 5)

| Piece | Path |
| --- | --- |
| Contract | [`docs/qat-wsd.md`](qat-wsd.md) |
| Mapping | [`docs/adr/0002-ste-qat-mapping.md`](adr/0002-ste-qat-mapping.md) |
| Config | [`examples/qat-wsd-smoke.json`](../examples/qat-wsd-smoke.json) |
| Host | `xbox_gpu_host --qat-smoke --steps 16` (N=16) |

```bat
python scripts\validate_qat_schedule.py examples\qat-wsd-smoke.json --dry-run
.\build\Release\xbox_gpu_host.exe --qat-smoke --steps 16
.\build\Release\xbox_gpu_host.exe --qat-smoke --dry-run
```

Linux / no D3D12:

```bash
python3 scripts/validate_qat_schedule.py examples/qat-wsd-smoke.json --dry-run
./build/xbox_gpu_host --qat-smoke --steps 16
# Host N-step QAT/WSD loop, then BLOCKED: no D3D12 device. Dispatch log not invented.
```

Default FakeQuant is ternary absmean (ADR 0002). `--bit-width 2|4` selects host midrise FakeQuant. **No new HLSL.** Isolated cooldowns overlay WSD; they are not merged into decay. Measured loss pairs are **not** a quality curve.

## Series S|X Dev Mode (Fase 6)

| Piece | Path |
| --- | --- |
| Validation page | [`docs/console.md`](console.md) — deploy notes, PIX checklist, results table |
| Blockers SoT | [`docs/platform/blockers-fase6-validation.md`](platform/blockers-fase6-validation.md) |
| Memory SoT | [`docs/platform/uwp-resources.md`](platform/uwp-resources.md) — App **~1 GB**; Creators Game **~5 GB**; debugger masks OOM |
| GDK SoT | [`docs/platform/gdk-vs-gdkx.md`](platform/gdk-vs-gdkx.md) — public GDK Windows-only |

```bat
REM Device Portal / Dev Mode deploy — exact cmd TBD in docs/console.md
REM PIX: capture one compute shader dispatch on console if tooling allows
```

Historical Win32 host workloads remain unmeasured on console. The separate E0 UWP lane is measured in [console.md](console.md); its results do not validate these host workloads. Do **not** assume Game designation for an App package. The **non-debug** package is the memory gate.

## Docs lint

```bash
python3 scripts/check_required_docs.py
python3 scripts/check_glossary.py
python3 scripts/check_relative_links.py
python3 scripts/validate_qat_schedule.py examples/qat-wsd-smoke.json --dry-run
```

## Related platform packs (do not duplicate)

| Pack | Use when |
| --- | --- |
| [gdk-vs-gdkx.md](platform/gdk-vs-gdkx.md) | Public GDK vs GDKX / ID@Xbox |
| [dev-mode.md](platform/dev-mode.md) | Retail Dev Mode, ≤3 consoles, UWP deploy |
| [dx12-hlsl-compute.md](platform/dx12-hlsl-compute.md) | FL 11.0, compute shader path, no CUDA |
| [uwp-resources.md](platform/uwp-resources.md) | App 1 GB / Creators 5 GB; debugger masks OOM |
| [memory-budget.md](memory-budget.md) | Fase 4 streaming vs those caps; console unvalidated |
| [qat-wsd.md](qat-wsd.md) | Fase 5 WSD + isolated cooldowns + bit-widths |
| [directml-scope.md](platform/directml-scope.md) | DirectML is not the trainer |
| [series-s-vs-x.md](platform/series-s-vs-x.md) | Public SKU specs, not our benches |
| [blockers-fase6-validation.md](platform/blockers-fase6-validation.md) | Why console tables wait |
| [console.md](console.md) | Fase 6 deployment, measured E0 and pending targets |
