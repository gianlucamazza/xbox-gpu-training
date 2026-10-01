# Public GDK vs GDKX / ID@Xbox

This repository uses the **public GDK / Windows SDK / DirectX 12** path. It does **not** claim **GDKX** or **ID@Xbox** access.

## Public GDK is Windows-only

The public [microsoft/GDK](https://github.com/microsoft/GDK) licence **Purpose** permits use of the GDK solely to develop and test **Microsoft Windows Titles** for the **Microsoft Windows Platform**. The Purpose **does not include** development and testing for platforms other than Windows, **including Xbox Series consoles** (and successor families).

- [GDK LICENSE-EN-US.MD](https://github.com/microsoft/GDK/blob/Main/LICENSE-EN-US.MD)

Publishing, commercialization, and end-user distribution of those Windows titles are also outside that Purpose unless a separate Microsoft agreement says otherwise.

## GDKX

**GDKX** means Xbox Extensions: the licensed-partner / managed-NDA console kit (for example via **ID@Xbox**). This repo does **not** claim GDKX APIs, full title entitlements, or Microsoft-provisioned console devkits.

## ID@Xbox

Public onboarding: register as an Xbox partner, sign the NDA, enroll in Partner Center, apply to ID@Xbox, get a game concept approved, then sign the Title Licensing Agreement and the **Xbox Game Development Kit (GDK) License Agreement**. That path is how partners get the Xbox GDK licence and the full console / development-kit track.

- [Join ID@Xbox](https://learn.microsoft.com/en-us/gaming/game-publishing/onboarding/onboarding-join-id-at-xbox)

This repository does **not** claim that path.

## What is usable here without GDKX

Research-usable without GDKX:

- Retail **Dev Mode** + **UWP** on console ([dev-mode.md](dev-mode.md))
- **DirectX 12** Hardware Feature Level **11.0** on Series X|S UWP Apps and Games ([dx12-hlsl-compute.md](dx12-hlsl-compute.md))
- Under App / Game resource caps ([uwp-resources.md](uwp-resources.md))
- Windows host work with public GDK / Windows SDK (Fase 0–5)

**Not** usable without ID@Xbox / NDA (and not claimed here):

- GDKX APIs
- Full title entitlements
- Microsoft-provisioned console development kits

## Related

- [console-constraints.md](console-constraints.md)
- [docs/adr/0001-architecture.md](../adr/0001-architecture.md)
