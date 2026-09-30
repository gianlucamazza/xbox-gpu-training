# STE + AdamW (Fase 3)

Tiny-net gradient check and **one** AdamW step on master fp32, with ternary FakeQuant + STE. Mapping: [docs/adr/0002-ste-qat-mapping.md](adr/0002-ste-qat-mapping.md).

HLSL Kernels own the backward compute-shader **shape**. QAT & WSD own FakeQuant / STE / AdamW **semantics**. DirectML is not the optimizer. No CUDA. xllama is not modified.

## Tiny net

Two-layer MLP, same **relu2** activation as the Fase 2 tiny FLP2 fixture (`max(h, 0)^2`). Sizes stay in the Fase 2 fixture class (tiny, not a transformer backward).

| Tensor | Shape | Role |
| --- | --- | --- |
| `x` | `[B, In]` | `B=2`, `In=4` |
| `W1` | `[H, In]` | master fp32; `H=4` |
| `W2` | `[Out, H]` | master fp32; `Out=3` |
| `t` | `[B, Out]` | MSE target |

```
W1q, W2q = FakeQuantTernaryAbsmean(W1), FakeQuantTernaryAbsmean(W2)
h_pre[b, h] = Σ_i x[b, i] * W1q[h, i]
h            = relu2(h_pre)
y[b, o]      = Σ_h h[b, h] * W2q[o, h]
L            = 0.5 * Σ (y - t)^2
```

Fixed host values (not a learned LLM). See `MakeDefaultTinySteNet()` in [`src/cpp/cpu_ste.cpp`](../src/cpp/cpu_ste.cpp).

## FakeQuant (ternary, absmean)

```
s = mean(|W|)     # 1 if s == 0
W_q = s * clip(round(W / s), -1, +1)
```

Codes are `{-1, 0, +1}`. `round` = nearest, ties to even. 2-bit / 4-bit FakeQuant are **host** midrise kernels in Fase 5 (`FakeQuant2BitAbsmean` / `FakeQuant4BitAbsmean`; FLP2 `levels=4/16` lattice). They are not on the Fase 3 `--train-step 1` path. No new HLSL. See [qat-wsd.md](qat-wsd.md).

Fase 2 scalar decode with `levels=3` is the same lattice: `W = (sym - 1) * s` for `sym ∈ {0,1,2}`.

## STE

`∂Q/∂W ≈ 1` with hard clip in the **normalized** domain:

```
mask[i] = 1  if |W[i] / s| ≤ 1
        = 0  otherwise
dW_master = dW_q ⊙ mask
```

Scale `s` is a constant in the backward (no absmean gradient). This is the documented clip / hard-tanh bound — not a novel estimator.

## Grad-check (chosen TBD tolerance)

Discrete FakeQuant is piecewise constant, so finite-diff of `Q(W)` is **not** the STE claim.

| Check | Method | max-abs | max-rel |
| --- | --- | --- | --- |
| STE-identity backward | analytic vs central finite-diff (`h = 1e-4`) of the **unquantized** tiny net (`Wq := W`) | `1e-3` | `2e-2` when `\|analytic\| ≥ 1e-2` |
| GPU kernel vs CPU (when D3D12 exists) | FakeQuant / `matmul_grad` / `relu2_grad` / `ste_backward` | `1e-5` | `1e-4` |

Relative error is `|actual - ref| / max(|ref|, 1e-8)`. Entries with `|analytic| < 1e-2` are **abs-gated only**: central-diff cancellation makes relative error uninformative there. The abs gate still applies. The `2e-2` rel bound is the measured-fp32 margin (a `1e-2` cut failed W1[7] at `1.025e-2`).

The identity path is the function STE claims to differentiate. Quantized codes and the STE mask are checked separately (not via finite-diff of `Q`).

## AdamW (host, master fp32)

Decoupled AdamW. Constants match ADR 0002: `lr=1e-3`, `β1=0.9`, `β2=0.999`, `ε=1e-8`, `wd=0.01`.

`--train-step 1` runs **one** step: FakeQuant → quantized forward → STE backward → AdamW on `W1`/`W2`. It prints the **measured** loss before/after that step. That is not a quality curve and not tok/s.

## HLSL

| Shader | Role |
| --- | --- |
| [`src/hlsl/fakequant_ternary.hlsl`](../src/hlsl/fakequant_ternary.hlsl) | `W_q = s * clip(round(W/s), -1, +1)` (host supplies `s`) |
| [`src/hlsl/matmul_grad.hlsl`](../src/hlsl/matmul_grad.hlsl) | `dW[o, i] = Σ_b dy[b, o] * x[b, i]` |
| [`src/hlsl/relu2_grad.hlsl`](../src/hlsl/relu2_grad.hlsl) | `dpre = 2 * max(pre, 0) * dh` |
| [`src/hlsl/ste_backward.hlsl`](../src/hlsl/ste_backward.hlsl) | `dW = dW_q * 1_{|W/s| ≤ 1}` |

AdamW is **not** a compute shader in this phase (`src/cpp/` on master weights).

## Commands

```bat
.\build\Release\xbox_gpu_host.exe --grad-check
.\build\Release\xbox_gpu_host.exe --train-step 1
```

Linux / no D3D12:

```bash
./build/xbox_gpu_host --grad-check
./build/xbox_gpu_host --train-step 1
# CPU table / one step, then BLOCKED: no D3D12 device — dispatch log not invented.
```

Honest first lines:

- `STATUS: grad-check ok` then `BLOCKED: no D3D12 device` — CPU finite-diff passed; no GPU dispatch.
- `STATUS: grad-check dispatched` — Windows D3D12 also ran the four kernels vs CPU.
- `STATUS: train-step ok` then `BLOCKED: no D3D12 device` — one CPU AdamW/STE step; no GPU dispatch.
- `STATUS: train-step dispatched` — GPU kernels compared, host AdamW applied.
- `FAILED: …` — load, grad-check, or (if a device existed) GPU parity failed.

No tok/s. No console numbers. No invented loss curves. WSD / isolated cooldowns: [qat-wsd.md](qat-wsd.md) (`--qat-smoke --steps 16`).
