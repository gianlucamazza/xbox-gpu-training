# Architecture (high-level)

This is the GPU research track for quantized LLM work on Xbox Series S|X. Decisions and constraints are recorded in [docs/adr/0001-architecture.md](adr/0001-architecture.md). Execution order is [docs/execution-plan.md](execution-plan.md). Public Microsoft/Xbox constraints (not measured results) live under [docs/platform/](platform/dev-mode.md).

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
4. **Quantized** forward/backward (QAT ternary / 2-bit / 4-bit) with a **straight-through estimator (STE)** and **AdamW**, later a **WSD** schedule with isolated cooldowns.
5. **CPU ggml** remains the numerical and timing baseline until console numbers exist.

```
  system RAM (fp32 master weights)
           |  chunk + double buffer
           v
  GPU (DirectX 12 compute shader: matmul / RMSNorm / RoPE / FLP2 forward)
           |
           v
  host (STE, AdamW, QAT / WSD — Fase 3–5)
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

## Honesty bar

No tok/s or quality claims until measured. Fase 6 may report `BLOCKED: no console`. Public BitNet/peer comparison is Fase 7 only.
