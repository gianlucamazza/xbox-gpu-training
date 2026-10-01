# Architecture (high-level)

This is the GPU research track for quantized LLM work on Xbox Series S|X. Decisions and constraints are recorded in [docs/adr/0001-architecture.md](adr/0001-architecture.md) (E0 backend: [0003](adr/0003-floppylm-e0.md), [0004](adr/0004-independent-e0-gates.md)). Execution order is [docs/execution-plan.md](execution-plan.md). Fase 0 install and host run: [docs/setup.md](setup.md). Fase 6 console lane: [docs/console.md](console.md) (Series S E0 measured; other targets pending). Fase 7 honest write-up: [docs/results.md](results.md). Public Microsoft/Xbox constraints (not measured results) live under [docs/platform/](platform/dev-mode.md).

## Problem

Retail Xbox Series S|X in **Dev Mode** expose a capable RDNA 2 GPU, but:

- There is **no CUDA** on Xbox.
- **DirectML** on console is useful for some inference/forward graphs (as studied in the companion app) and is **not** treated here as a trainer.
- A UWP **App** designation typically sees on the order of **~1 GB** usable RAM; a **Game** designation is on the order of **~5 GB**. Those figures are planning budgets, not measured results from this repo.
- The memory bus and **AppContainer** constrain how weights and activations move.
- Dev Mode is a purpose licence to develop and test apps (commonly ≤3 consoles), not to operate a GPU farm.

## Approach

1. **DirectX 12 compute shaders** written in **HLSL** for matmul, RMSNorm, RoPE, and a forward pass reconstructed from the **FLP2** codec.
2. **fp32 master weights** stay in system RAM.
3. **Chunk streaming** plus **double buffering** move working tiles to the GPU.
4. **Quantized** forward/backward (QAT ternary / 2-bit / 4-bit) with a **straight-through estimator (STE)** and **AdamW**, plus a **WSD** schedule with isolated cooldowns (Fase 5: [qat-wsd.md](qat-wsd.md)).
5. **CPU ggml** remains the numerical and timing baseline until console numbers exist. Fase 1 uses a portable GEMM with the same `C = A @ B` contract; ggml is not vendored yet ([ggml-baseline.md](ggml-baseline.md)). Fase 2 reconstructs scalar FLP2 decode + RMSNorm + RoPE + a tiny forward ([flp2-forward.md](flp2-forward.md)); it does not unpack a binary FLP2 envelope. Fase 3 FakeQuant + STE + host AdamW: [ste-adamw.md](ste-adamw.md), [adr/0002-ste-qat-mapping.md](adr/0002-ste-qat-mapping.md). Fase 4 streams a logical corpus through a 2-slot double buffer under the App ~1 GB planning budget ([memory-budget.md](memory-budget.md)); Game ~5 GB is documented only. Console AppContainer is unvalidated on desktop RAM. Fase 5 host QAT/WSD smoke (N=16) with isolated cooldown overlays: [qat-wsd.md](qat-wsd.md). No new HLSL in Fase 4–5.

```
  system RAM (fp32 master weights + AdamW moments)
           |  chunk + double buffer
           v
  GPU (DirectX 12 compute shader: matmul / RMSNorm / RoPE / FLP2 forward
       + FakeQuant / matmul_grad / relu2_grad / STE mask)
           |
           v
    host (STE policy, AdamW on master — Fase 3; stream + budget — Fase 4;
          QAT / WSD + isolated cooldowns — Fase 5)
```

## Companion

[gianlucamazza/xllama](https://github.com/gianlucamazza/xllama) holds the FloppyLM **CPU** path and the shipped UWP chat/diffusion app. This repository does not modify xllama and does not claim GPU training inside xllama.

## Platform fact packs

These are public SoT notes. They do **not** complete Fase 6 and do **not** invent kernel benches.

| Pack | Topic |
| --- | --- |
| [platform/dev-mode.md](platform/dev-mode.md) | Dev Mode purpose, ≤3 consoles (Xbox One–titled legal page), not GDKX |
| [platform/uwp-resources.md](platform/uwp-resources.md) | Apps 1 GB / Creators games 5 GB; debugger can mask OOM |
| [platform/dx12-hlsl-compute.md](platform/dx12-hlsl-compute.md) | DirectX 12 / HLSL compute shader path; FL 11.0; no CUDA |
| [platform/directml-scope.md](platform/directml-scope.md) | DirectML = inference / ML primitives; not the trainer |
| [platform/gdk-vs-gdkx.md](platform/gdk-vs-gdkx.md) | Public GDK is Windows-only; GDKX / ID@Xbox not claimed |
| [platform/series-s-vs-x.md](platform/series-s-vs-x.md) | Public SKU specs only; no fabricated Series S\|X benches |
| [platform/blockers-fase6-validation.md](platform/blockers-fase6-validation.md) | Fase 6 blockers; Fase 0–5 is not gated by Fase 6 |
| [console.md](console.md) | Fase 6 deployment and measured Series S E0 results; other Series cells UNMEASURED |
| [results.md](results.md) | Fase 7 honest publication (host sourced; E0 cited; rest UNMEASURED) |

## Honesty bar

No tok/s or quality claims until measured. Fase 6 records measured Series S E0 hardware evidence in [console.md](console.md). Fase 7 publishes that evidence in [results.md](results.md) and keeps every other Series / quality / PIX / peer cell **UNMEASURED** — an empty cell is **not** hardware. BitNet/peer numbers are paper-sourced or marked UNMEASURED. Do not invent tok/s beyond the E0 already on `main`.
