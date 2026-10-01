# Fase 6 blockers (Series S|X validation)

Fact pack (XGPU Dev Mode Counsel, 2026-09-30). **Fase 6** is the console-validation lane. It is **not** complete. This page lists explicit blockers. It does **not** invent tok/s, quality scores, or Series S|X kernel benches.

**Fase 0–5** (Windows, public GDK, DirectX 12 / HLSL) is **not** gated by Fase 6. Those phases may proceed on a Windows DX12 device. Console speed/quality tables wait for Fase 6 hardware or an explicit `BLOCKED: no console`.

## Blockers

### 1. No primary Microsoft claim of on-console LLM / GPU training via DirectML or ORT

DirectML is a DX12-style inference / ML-primitive API. Xbox public messaging is in-game ML inference. There is **no** primary Xbox or ORT source of truth that this repo treats as “train LLMs on Xbox via DirectML.” On-console training via DirectML remains **unsupported / unclear** until such a source exists.

Detail: [directml-scope.md](directml-scope.md).

### 2. UWP memory caps; debugger can mask OOM

Foreground: UWP **Apps 1 GB**; Xbox Live **Creators Program games 5 GB**. Background apps **≤128 MB**. Visual Studio debugger **does not apply** these caps — the **non-debug** package is the real gate.

Whether unpublished research can use the Creators **Game** class is uncertain. Do not assume Game designation for an App package.

Detail: [uwp-resources.md](uwp-resources.md).

### 3. Public GDK is Windows-only; full console is GDKX / ID@Xbox

Public GDK Purpose excludes Xbox Series console targeting. **GDKX** and **ID@Xbox** are the NDA / partner path for full console APIs, title entitlements, and Microsoft-provisioned devkits. This repo does **not** claim that path.

Detail: [gdk-vs-gdkx.md](gdk-vs-gdkx.md).

### 4. Dev Mode is UWP develop/test only — not GDKX entitlements

Dev Mode turns a retail Xbox into a development console to develop and test UWP apps (Dev Home / deploy). Retail store titles generally do not run there. Legal public text still uses an **Xbox One**–titled activation agreement and a **≤3 consoles** cap (confirm Series enforcement in Partner Center). Dev Mode **≠** full console stack and **≠** GDKX hardware access.

Detail: [dev-mode.md](dev-mode.md).

## Glossary (phase lanes)

| Lane | What is in scope |
| --- | --- |
| **Fase 0–5** | Public GDK + Windows **DirectX 12** / **HLSL** compute shaders. CPU ggml baseline. No console tok/s. |
| **Fase 6** | Series S|X **Dev Mode** validation. Series S E0 functional execution is measured; quality / Series X / PIX remain **UNMEASURED**. |
| **Fase 7** | Honest publication of measured (or explicitly unmeasured) results. |

## Related

- [docs/console.md](../console.md) — Fase 6 deploy / PIX / measured Series S E0 and remaining UNMEASURED cells
- [docs/results.md](../results.md) — Fase 7 honest summary (E0 cited; other Series cells UNMEASURED)
- [series-s-vs-x.md](series-s-vs-x.md) — public SKU specs, not our benches
- [dx12-hlsl-compute.md](dx12-hlsl-compute.md) — FL 11.0 research path
- [ROADMAP.md](../../ROADMAP.md)
- [docs/execution-plan.md](../execution-plan.md)
