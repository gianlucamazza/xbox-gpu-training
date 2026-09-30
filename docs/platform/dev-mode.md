# Xbox Dev Mode (public UWP path)

Fact pack (XGPU Dev Mode Counsel, 2026-09-30). Public Microsoft documentation only. Dev Mode is **not** a GDKX entitlement and is **not** the full console stack.

## Purpose

Retail Xbox can be switched into **Developer Mode** so you can develop and test Universal Windows Platform (UWP) apps on the console. Switch back to **Retail Mode** to play store titles.

- [Getting started with UWP app development on Xbox](https://learn.microsoft.com/en-us/windows/uwp/xbox-apps/getting-started)
- [UWP on Xbox: frequently asked questions](https://learn.microsoft.com/en-us/windows/uwp/xbox-apps/frequently-asked-questions)

In Dev Mode you get **Dev Home** and a deploy path from Visual Studio. Retail store apps and games generally **do not** run there. Microsoft’s getting-started note: *“Your retail games and apps won’t run in Developer Mode, but the apps or games you create will. Switch back to Retail Mode to run your favorite games and apps.”*

## Legal device cap (Xbox One–titled page)

The published activation agreement still uses **Xbox One** in the title and body. Purpose: activate a retail console as a development console to test application development (including demonstrating those applications). Device limitation: **three consoles at any one time** for yourself as an individual **or** for your company (including affiliates).

- [Xbox One Developer Mode Activation Program](https://learn.microsoft.com/en-us/legal/windows/agreements/xbox-one-developer-mode-activation)

Treat **≤3 consoles** as the publicly documented cap. Confirm Series S|X enforcement in Partner Center before treating that page as a hard source of truth for current Series hardware.

## UWP resource docs cover Xbox One and Series X|S

Memory, CPU, GPU share, and DirectX feature-level tables for UWP Apps vs Xbox Live Creators Program games apply to **Xbox One and Xbox Series X|S**:

- [System resources for UWP apps and games on Xbox](https://learn.microsoft.com/en-us/windows/uwp/xbox-apps/system-resource-allocation)

See [uwp-resources.md](uwp-resources.md) and [dx12-hlsl-compute.md](dx12-hlsl-compute.md).

## What Dev Mode is not

- **Not** the full console / title stack.
- **Not** GDKX hardware access, Microsoft-provisioned devkits, or ID@Xbox entitlements. This repo does **not** claim those.
- **Not** a GPU farm licence. Purpose is develop/test apps (and demonstrate them), not operate training hardware at scale.

Fase 0–5 work on Windows and the public GDK / DirectX 12 path is **not** gated on Dev Mode. Fase 6 console validation is. See [blockers-fase6-validation.md](blockers-fase6-validation.md) and [gdk-vs-gdkx.md](gdk-vs-gdkx.md).
