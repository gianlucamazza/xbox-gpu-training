# DirectX 12 / HLSL compute (research path)

This repository’s research path on Windows and public GDK is **DirectX 12** compute shaders written in **HLSL**. After first mention, **DX12** is fine.

Xbox has **no CUDA**. That is an absence, not a stack this repo uses.

## Series X|S UWP: feature level 11.0

On **Xbox Series X|S**, both UWP **Apps** and **Games** document **DirectX 11 and DirectX 12 with Hardware Feature Level 11.0**:

- [System resources for UWP apps and games on Xbox](https://learn.microsoft.com/en-us/windows/uwp/xbox-apps/system-resource-allocation)

That page gives Apps a shared GPU slice (about 45%) and Games full access to available GPU cycles; the CPU/GPU table is recorded once in [uwp-resources.md](uwp-resources.md#cpu--gpu-share-same-page).

**App Mode is not full title GPU.** Do not treat an App-class UWP package as a retail title’s GPU budget.

## Compute shader (two words)

UWP documents a Direct3D **compute pipeline**. A **compute shader** is written in HLSL and targets general-purpose work on the GPU:

- [Compute pipeline (UWP)](https://learn.microsoft.com/en-us/windows/uwp/graphics-concepts/compute-pipeline)

That is the documented public compute path this research follows. Kernels live under `src/hlsl/`: the E0 trainer dispatches `e0_tensor.hlsl` inside the UWP App; the diagnostic lane dispatches the other kernels from a Win32 desktop host.

## ATG note (Walbourn): FL 11.0, SM 5.1–6.4, Ultimate-class features

Chuck Walbourn (Microsoft ATG), *DirectX and UWP on Xbox Series X|S* (2022):

- DirectX 12 on Series X|S UWP: **Feature Level 11.0** (App Mode and Game Mode).
- For DirectX 12, **Shader Models 5.1 to 6.4** are supported.
- For **HDR**, **DirectX Raytracing (DXR)**, or other **DirectX Ultimate** features: **contact ID@Xbox**.

- [DirectX and UWP on Xbox Series X|S](https://walbourn.github.io/directx-and-uwp-on-xbox-series-x-s/)

This repo does **not** claim DXR, HDR title paths, or Ultimate-class features on public UWP / Dev Mode without ID@Xbox. We do **not** claim ID@Xbox access.

## Must not claim

- CUDA on Xbox, or that this project has a CUDA / NVIDIA compute stack.
- App Mode = full title GPU.
- DXR / DirectX Ultimate on public UWP or Dev Mode without ID@Xbox.
- Invented tok/s or GPU training benches (including fabricated Series S|X kernel numbers).

## Related

- [gdk-vs-gdkx.md](gdk-vs-gdkx.md) — public GDK is Windows-only
- [directml-scope.md](directml-scope.md) — DirectML is not the trainer
- [uwp-resources.md](uwp-resources.md) — App vs Game budgets
