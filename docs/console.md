# Fase 6 — Series S|X Dev Mode validation

**Status: `BLOCKED: no console`.** (merged on `main` as `66224e05`, [PR #15](https://github.com/gianlucamazza/xbox-gpu-training/pull/15))

This page is the Fase 6 deploy / PIX / results scaffolding. It does **not** invent tok/s, quality scores, PIX captures, or Series S|X kernel benches. It does **not** claim **GDKX** or **ID@Xbox**.

At merge, this lane had **no** Dev Mode kit. On **2026-09-30** the owner re-enabled Xbox Dev Mode, so a kit is **now available for a follow-up measured PR**. That does **not** change this page: every Series metric cell stays empty / **UNVALIDATED** until that follow-up lands real numbers. Do **not** back-fill.

Stopping here for a human with a kit is a successful honest phase ([docs/execution-plan.md](execution-plan.md#fase-6--series-sx-dev-mode-validation)).

Platform SoT (do not duplicate; do not soften):

| Pack | Why it gates this page |
| --- | --- |
| [platform/blockers-fase6-validation.md](platform/blockers-fase6-validation.md) | Four known Fase 6 blockers |
| [platform/uwp-resources.md](platform/uwp-resources.md) | App **~1 GB** vs Creators Game **~5 GB**; debugger can mask OOM |
| [platform/gdk-vs-gdkx.md](platform/gdk-vs-gdkx.md) | Public GDK is **Windows-only**; GDKX / ID@Xbox not claimed |
| [platform/directml-scope.md](platform/directml-scope.md) | DirectML is **not** the trainer |
| [platform/dev-mode.md](platform/dev-mode.md) | Dev Mode = UWP develop/test; **≠** GDKX |
| [platform/series-s-vs-x.md](platform/series-s-vs-x.md) | Public SKU specs only — not our benches |
| [platform/dx12-hlsl-compute.md](platform/dx12-hlsl-compute.md) | FL 11.0; App Mode is **not** full title GPU |

Windows host install/run stays in [docs/setup.md](setup.md). This file is console validation only.

## What Fase 6 is

| This phase does | This phase does not |
| --- | --- |
| Document the public Dev Mode / Device Portal deploy path | Claim a package was deployed |
| Ship an empty / `BLOCKED` results table | Invent Series S\|X tok/s or quality |
| Ship a PIX checklist (no `.wpix` attached) | Fake a capture or timing table |
| Cite the four known blockers | Treat DirectML as the trainer |
| Stop for a human if there is no kit | Claim GDKX / ID@Xbox / CUDA |

## Verdict

```
BLOCKED: no console
reason (merge 66224e05): no Dev Mode kit in that lane; GDKX / ID@Xbox not claimed
note (2026-09-30): kit re-enabled — follow-up measured PR only; cells stay empty
```

This Cursor / cloud VM is Linux. There is no retail Xbox attached here, no Dev Home pairing, and no Microsoft-provisioned GDKX kit. Public GDK does **not** target Series consoles ([gdk-vs-gdkx.md](platform/gdk-vs-gdkx.md)). A Windows DX12 box or GitHub `windows-latest` (including WARP / Basic Render Driver) is **not** Series S|X hardware. The owner's re-enabled Dev Mode console is **not** a measurement until a follow-up PR records one.

## Four known blockers (must stay accurate)

Copied in meaning from [blockers-fase6-validation.md](platform/blockers-fase6-validation.md). Do not rewrite these as “solved.”

1. **No primary Microsoft claim of on-console LLM / GPU training via DirectML or ORT.** DirectML is a DX12-style inference / ML-primitive API. Xbox public messaging is in-game ML inference. On-console training via DirectML remains **unsupported / unclear**. This research path is **DirectX 12 / HLSL** compute shaders. Detail: [directml-scope.md](platform/directml-scope.md).

2. **UWP memory caps; debugger can mask OOM.** Foreground: UWP **Apps ~1 GB**; Xbox Live **Creators Program games ~5 GB**. Background apps **≤128 MB**. Visual Studio debugger **does not apply** these caps — the **non-debug** package is the real gate. **Do not assume Game designation** for an unpublished research **App** package. Detail: [uwp-resources.md](platform/uwp-resources.md). Fase 4 desktop peak working-set is **not** an AppContainer measurement ([memory-budget.md](memory-budget.md)).

3. **Public GDK is Windows-only; full console is GDKX / ID@Xbox.** Public GDK Purpose excludes Xbox Series console targeting. This repo does **not** claim GDKX APIs, title entitlements, or Microsoft-provisioned devkits. Detail: [gdk-vs-gdkx.md](platform/gdk-vs-gdkx.md).

4. **Dev Mode is UWP develop/test only — not GDKX entitlements.** Dev Mode turns a retail Xbox into a development console to develop and test UWP apps (Dev Home / deploy). Retail store titles generally do not run there. Legal public text still uses an **Xbox One**–titled activation agreement and a **≤3 consoles** cap (confirm Series enforcement in Partner Center). Dev Mode **≠** full console stack and **≠** GDKX hardware access. Detail: [dev-mode.md](platform/dev-mode.md).

## Deploy notes (scaffolding; not executed)

This repository ships a **Win32 desktop** host (`xbox_gpu_host`). It is **not** an Xbox UWP App package. There is **no** in-tree `.appx` / MSIX / Partner Center title for Dev Home or Device Portal.

When a human has a **Dev Mode** kit **and** a real UWP package, the public path is:

1. Activate Developer Mode on the retail console (Dev Mode Activation app + Partner Center). Switch and restart into Dev Mode. Retail store titles generally will not run there. ([Getting started with UWP app development on Xbox](https://learn.microsoft.com/en-us/windows/uwp/xbox-apps/getting-started), [dev-mode.md](platform/dev-mode.md).)
2. Open **Dev Home**. Under Remote Access, enable **Xbox Device Portal**, set a username/password, and note the HTTPS URL Dev Home shows. ([Device Portal for Xbox](https://learn.microsoft.com/en-us/previous-versions/windows/uwp/xbox-apps/device-portal-xbox).)
3. From Visual Studio: UWP workload, **x64**, deploy target **Remote Machine**, authentication **Universal (Unencrypted Protocol)**, pair with the PIN from Dev Home. ([Deploying and debugging UWP apps](https://learn.microsoft.com/en-us/windows/uwp/debug-test-perf/deploying-and-debugging-uwp-apps).)
4. Or side-load a **signed** AppX plus dependencies from the Device Portal Home tab.
5. Run the **non-debug** package for any memory-budget claim. A debugger session that does not OOM is **not** evidence that the App ~1 GB (or Creators Game ~5 GB) cap holds ([uwp-resources.md](platform/uwp-resources.md)).
6. Target **x64**. App Mode is **not** full title GPU (FL **11.0**, ~45% GPU share) ([dx12-hlsl-compute.md](platform/dx12-hlsl-compute.md)).

```bat
REM Device Portal / Dev Mode deploy — exact package command TBD when a UWP
REM package exists. This phase did not run a deploy.
REM Do not treat xbox_gpu_host.exe (Win32 desktop) as a console package.
```

**This phase did not deploy.** No Device Portal session, no pairing PIN, no AppX. `BLOCKED: no console`.

## PIX capture checklist

[PIX on Windows](https://devblogs.microsoft.com/pix/download/) is the public DirectX 12 capture/timing tool documented in [docs/setup.md](setup.md#pix). This repository does **not** include a `.wpix` artifact. Do not invent one.

Xbox-title PIX / partner capture tooling is **not** claimed here (that sits on the GDKX / ID@Xbox path we do not have).

| Step | Done? |
| --- | --- |
| PIX on Windows install documented | yes — [setup.md](setup.md#pix) |
| Windows DX12 capture of one compute dispatch (`hello_compute` / matmul / FLP2) | **no** — not produced this phase |
| Attach a `.wpix` / timing table | **no** — none exists |
| Console / Dev Mode PIX capture of one compute shader dispatch | **`BLOCKED: no console`** |
| Treat WARP / Basic Render Driver as Series S\|X | **never** |

```bat
REM PIX: capture one compute shader dispatch on console if tooling allows
REM Not run. No .wpix committed. Not a timing result.
```

## Results table

Schema intent: `xbox-gpu-training.benchmark.console.v1`. Empty metric cells are required until a **real** Dev Mode run fills them. CPU-only comparison is filled **only** if both sides were measured.

| sku | workload | tok/s | quality | cpu_tok/s | pix | status | note |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Series S | hello_compute | | | | | `BLOCKED: no console` | UNVALIDATED at 66224e05; follow-up kit not measured |
| Series S | matmul FP32/FP16 | | | | | `BLOCKED: no console` | UNVALIDATED at 66224e05; follow-up kit not measured |
| Series S | FLP2 forward fixture | | | | | `BLOCKED: no console` | UNVALIDATED at 66224e05; follow-up kit not measured |
| Series S | stream-stress App ~1 GB | | | | | `BLOCKED: no console` | AppContainer unvalidated |
| Series S | qat-smoke N=16 | | | | | `BLOCKED: no console` | no quality curve |
| Series X | hello_compute | | | | | `BLOCKED: no console` | UNVALIDATED at 66224e05; follow-up kit not measured |
| Series X | matmul FP32/FP16 | | | | | `BLOCKED: no console` | UNVALIDATED at 66224e05; follow-up kit not measured |
| Series X | FLP2 forward fixture | | | | | `BLOCKED: no console` | UNVALIDATED at 66224e05; follow-up kit not measured |
| Series X | stream-stress App ~1 GB | | | | | `BLOCKED: no console` | AppContainer unvalidated |
| Series X | qat-smoke N=16 | | | | | `BLOCKED: no console` | no quality curve |
| CPU vs console | any of the above | | | | | not filled | both sides not measured |

Public marketing TFLOPS / GDDR6 sizes in [series-s-vs-x.md](platform/series-s-vs-x.md) are **SKU copy**, not rows of this table.

Windows Fase 1–5 host numbers (portable GEMM CSV, Linux `VmHWM`, QAT smoke loss pairs) are **not** console results and are **not** copied here as Series benches.

## Memory (repeat, so this page cannot be misread)

- Planning budget for an **App** package: **~1 GB** (1024 MiB).
- Creators **Game ~5 GB** is the **other** designation. Unpublished research must **not** assume Game class.
- Debugger can **mask OOM**. Gate = **non-debug** package.
- Desktop / CI working-set is not AppContainer.

Detail: [uwp-resources.md](platform/uwp-resources.md), [memory-budget.md](memory-budget.md).

## What a human with a kit should do next

Fase 7 may publish this table as still explicitly `BLOCKED` ([docs/results.md](results.md)). Filling cells is a **follow-up measured PR**, not a rewrite of `66224e05`.

1. Build a real **x64 UWP** package (does not exist in this repo today).
2. Deploy via Dev Home / Device Portal as above.
3. Run the same host workloads **non-debug** under the App ~1 GB plan (or record a measured breach).
4. Fill **only** measured cells. Leave the rest empty.
5. Attach a real PIX capture if public tooling allows; otherwise write why it is still blocked.
6. CPU vs console: fill `cpu_tok/s` only when the same workload was timed on CPU **and** on that console.

## Related

- [ROADMAP.md](../ROADMAP.md) — Fase 6 status
- [docs/setup.md](setup.md) — Windows toolchain
- [docs/execution-plan.md](execution-plan.md#fase-6--series-sx-dev-mode-validation)
- [docs/architecture.md](architecture.md)
