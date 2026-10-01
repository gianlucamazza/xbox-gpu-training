# Console constraints

Four public-platform constraints that every console result in this repository must
respect. A successful E0 run on Series S does not retire them; it operates within them.

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

## Related

- [../status.md](../status.md) — what has been measured on the console
- [series-s-vs-x.md](series-s-vs-x.md) — public SKU specs, not our benches
- [dx12-hlsl-compute.md](dx12-hlsl-compute.md) — FL 11.0 compute path
