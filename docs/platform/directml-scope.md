# DirectML scope (inference / forward only)

**DirectML is not the trainer** in this repository. There is **no** primary Microsoft claim of on-console LLM or GPU training via DirectML or ONNX Runtime (ORT) that this project treats as source of truth.

## How Microsoft frames DirectML

DirectML is framed as **machine learning inferencing** and **hardware-accelerated inferencing**, including the ORT + DirectML pairing:

- [Introduction to DirectML](https://learn.microsoft.com/en-us/windows/ai/directml/dml)
- [Get started with DirectML](https://learn.microsoft.com/en-us/windows/ai/directml/dml-get-started)

Learn’s get-started path is convert → optimize → integrate **hardware-accelerated inferencing** with ORT and DirectML. Samples listed there (Phi, LLMs, Stable Diffusion, style transfer, NPU inference) are **inference** examples, not an Xbox GPU training product.

**Wording this repo uses:** DirectML is primarily a **DX12-style inference / ML-primitive API**. Xbox public messaging is **in-game ML inference**.

## Xbox Wire (Series X|S, in-game ML)

Xbox Wire glossary: Series X|S **support Machine Learning for games with DirectML** (NPC behaviour, animation, visual quality — inference-shaped examples). The same entry quotes marketing **TFLOPS / TOPS** hardware capability. Those figures are **not** training product claims and are **not** benches of this repo’s kernels.

- [Xbox Series X|S Technology Glossary](https://news.xbox.com/en-us/2020/03/16/xbox-series-x-glossary/)

## Native API vs Xbox ORT

The native Win32 DirectML API documents some **training-oriented operators**. That is **Win32 API operator availability**, **not** Xbox ORT support for training, and **not** a claim that ORT or DirectML runs GPU training loops on console.

- [DirectML API (Win32)](https://learn.microsoft.com/en-us/windows/win32/api/_directml/)

## What this repo does not claim

Do **not** claim, without a primary Xbox or ORT source:

- ORT as a trainer on Xbox
- GPU training loops via DirectML on Xbox
- that “DirectML trains LLMs on Xbox”

Until such a primary source exists, **on-console training via DirectML remains unsupported / unclear**. This research path is **DirectX 12** **compute shaders** in **HLSL** ([dx12-hlsl-compute.md](dx12-hlsl-compute.md)), with **CPU ggml** as the numerical baseline.

## Related

- [console-constraints.md](console-constraints.md)
- [docs/adr/0001-architecture.md](../adr/0001-architecture.md)
