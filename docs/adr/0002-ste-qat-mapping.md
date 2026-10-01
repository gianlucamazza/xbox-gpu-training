# ADR 0002 — STE / FakeQuant mapping (Fase 3)

- Status: Accepted (Fase 3 scope)
- Superseded by: ADR 0003 for the E0 trainer; still governs the diagnostic lane
- Date: 2026-09-30
- Labels: `adr`, `research`, `kernel`, `phase-3`
- Milestone: [Fase 3 — Backward + AdamW](https://github.com/gianlucamazza/xbox-gpu-training/milestone/4)
- Semantics owner: XGPU QAT & WSD
- HLSL / DX12 grad-path owner: XGPU HLSL Kernels

## Context

[docs/archive/execution-plan.md](../archive/execution-plan.md) § Fase 3 requires a gradient check on a tiny net and one AdamW / straight-through estimator (STE) train step. The plan says to stop and write an ADR if the STE / QAT mapping is unspecified.

Master weights already live in host fp32 (ADR 0001). Fase 2 reconstructed a **scalar** FLP2 decode for deploy-style codes; it did not define how those codes are produced from a trainable master, nor how gradients flow through the quantizer.

This ADR freezes the Fase 3 mapping only. Full QAT schedules, WSD, and isolated cooldowns are Fase 5: [docs/diagnostic/qat-wsd.md](../diagnostic/qat-wsd.md). 2-bit / 4-bit FakeQuant are host midrise kernels there; they are still **not** the Fase 3 gate.

## Decision

1. **Master weights stay FP32.** They are the only trainable primary state. Quantized codes are a derived view, never the AdamW state.
2. **Forward uses FakeQuant.** `W_q = FakeQuant(W_fp32)` is what matmul / the tiny net multiplies. Fase 3 acceptance is **ternary** `{-1, 0, +1}` with a single **absmean** scale `s = mean(|W|)` (BitLinear-style peer). If `s == 0`, `s = 1`.
   - `W_q[i] = s * clip(round(W[i] / s), -1, +1)`
   - `round` is nearest integer, ties to even (HLSL `round` / C++ `nearbyint` default).
   - Deploy arithmetic for those codes must match this FakeQuant (Soul Player spirit: train↔deploy codes agree). Fase 2 scalar decode ` (sym - half) * scale ` with `levels=3` is the same ternary lattice once `sym ∈ {0,1,2}` and `scale = s`.
3. **STE:** `∂Q/∂W ≈ 1` in the backward, with a **hard clip** in the normalized domain: `mask[i] = 1` iff `|W[i] / s| ≤ 1`, else `0`. Scale `s` is treated as a **constant** in the backward (no gradient through absmean). This is the BitLinear-style clip / hard-tanh bound, not a novel estimator.
4. **AdamW runs on master FP32 only.** Host constants (not a DirectML optimizer):

   | Hyperparameter | Value |
   | --- | --- |
   | `lr` | `1e-3` |
   | `β1` | `0.9` |
   | `β2` | `0.999` |
   | `ε` | `1e-8` |
   | `weight_decay` | `0.01` (decoupled) |

   `W ← W - lr * (m̂ / (√v̂ + ε) + wd * W)` after the usual bias-corrected moments.
5. **2-bit / 4-bit FakeQuant** were stubs in the Fase 3 PR. Fase 5 implements them on the **host** (FLP2 midrise; `--bit-width 2|4`). They are still not on the Fase 3 `--train-step 1` / `--grad-check` STE-identity gate. No new HLSL.
6. **Grad-check** compares analytic STE-identity gradients (unquantized forward, same clip-off path) to central finite differences. Discrete FakeQuant is piecewise constant, so finite-diff of `Q(W)` is **not** the STE claim. Documented in [docs/diagnostic/ste-adamw.md](../diagnostic/ste-adamw.md).

## Peers (not ports)

These papers / trees inform the mapping. Nothing is copied as a CUDA or PyTorch runtime.

| Peer | What we take | What we do not |
| --- | --- | --- |
| Soul Player FakeQuantI8 + STE | Train↔deploy arithmetic must match; STE through the quantizer | int8 FakeQuant as the Fase 3 gate; their trainer stack |
| ternary15M BitLinear STE absmean `{-1,0,+1}` | Absmean scale + STE + clip in the `W/s` domain | Their full LLM curriculum or claimed quality |
| MobileLLM-Pro QAT + self-distill vs PTQ | Pointer only — Fase 5 | Any distill loop in this PR |
| Axolotl / Falcon BitNet | **Schedule recipe for Fase 5** | WSD / cooldown / multi-step QAT here |

## Consequences

- HLSL kernels implement FakeQuant, `dW = dy ⊤ x`-style weight grads, relu2 grad, and the STE mask. Host owns FakeQuant policy, AdamW, and step orchestration ([docs/diagnostic/ste-adamw.md](../diagnostic/ste-adamw.md)).
- Linux / no D3D12: CPU grad-check and one train step stay green; GPU path prints `BLOCKED: no D3D12 device`. No invented dispatch log.
- Changing STE clip, scale reduction, or AdamW coupling requires a new ADR (or an update to this one) and the `adr` label.

## Out of scope (this PR)

- Full WSD + isolated cooldowns — implemented in Fase 5 ([qat-wsd.md](../diagnostic/qat-wsd.md)); this ADR does not change AdamW β/ε/wd
- Binary FLP2 envelope / rANS (research issue #10)
- Console benches, tok/s, loss curves, quality metrics
- CUDA / cuDNN
- DirectML as trainer or optimizer
- Modifying [gianlucamazza/xllama](https://github.com/gianlucamazza/xllama)
- Fase 4 streaming / App ~1 GB stress

## Alternatives considered

| Alternative | Why not (now) |
| --- | --- |
| Train quantized codes as primary state | Breaks ADR 0001 master-fp32 rule; deploy mismatch risk |
| STE without clip | Allowed by some write-ups; we document the BitLinear-style `|W/s|≤1` bound instead of leaving it implicit |
| Novel learned quantizer / extra STE temperature | Would be a new estimator — forbidden without an ADR |
| DirectML optimizer | DirectML is inference/forward-focused; not the trainer |
| Finite-diff of discrete `Q(W)` as the gate | Locally zero / jumpy; would fail an honest STE check |
