# QAT + WSD (Fase 5)

Schedule source of truth for quantized-aware training on **host** AdamW. Mapping for FakeQuant / STE / master fp32: [docs/adr/0002-ste-qat-mapping.md](adr/0002-ste-qat-mapping.md). Fase 3 one-step contract: [docs/ste-adamw.md](ste-adamw.md).

QAT & WSD owns **schedule semantics**. HLSL Kernels owns compute-shader **shape**. This phase adds **no new HLSL**. 2-bit / 4-bit FakeQuant run on the host. DirectML is not the trainer or optimizer. No CUDA. xllama is not modified.

## Chosen smoke N

**N = 16.** Config: [`examples/qat-wsd-smoke.json`](../examples/qat-wsd-smoke.json).

| Segment | Steps | Notes |
| --- | --- | --- |
| warmup | 4 | LR linear `0 → base_lr` |
| stable | 8 | constant `base_lr` |
| decay | 4 | LR **linear** `base_lr → min_lr` |
| isolated cooldown `stable-mid` | 4 | overlay on steps `[8, 12)` — **not** added after decay |

`16 = warmup + stable + decay`. The cooldown **overlays** the WSD timeline (it is not `16 + 4`). N is a prefix of the WSD length that **crosses** the cooldown start at step `8` and the cooldown end at step `12`.

## WSD (global curve)

AdamW hyperparameters stay ADR 0002. Only `lr` is scheduled.

| Field | Example | Role |
| --- | --- | --- |
| `optimizer.base_lr` | `1e-3` | peak / stable LR |
| `optimizer.beta1` | `0.9` | frozen (dry-run rejects drift) |
| `optimizer.beta2` | `0.999` | frozen |
| `optimizer.eps` | `1e-8` | frozen |
| `optimizer.weight_decay` | `0.01` | frozen, decoupled |
| `wsd.warmup_steps` | `4` | linear `0 → base_lr` |
| `wsd.stable_steps` | `8` | constant `base_lr` |
| `wsd.decay_steps` | `4` | **linear** (chosen) `base_lr → min_lr` |
| `wsd.min_lr` | `1e-4` | `0.1 × base_lr` |
| `wsd.decay` | `linear` | cosine is **not** implemented |

Step index `s` is **0-based**. `WsdLength = warmup + stable + decay`.

```
# lerp first→last over n steps: i=0 → a, i=n-1 → b (n==1 → b)
lerp(a, b, i, n) = a + (b - a) * i / (n - 1)   if n > 1
                 = b                            if n == 1

if s < warmup:
    lr = lerp(0, base_lr, s, warmup)
elif s < warmup + stable:
    lr = base_lr
elif s < warmup + stable + decay:
    lr = lerp(base_lr, min_lr, s - warmup - stable, decay)
else:
    lr = min_lr
```

The first warmup step has `lr = 0`. The last warmup step has `lr = base_lr`. The last decay step has `lr = min_lr`.

## Isolated cooldowns (mandatory)

Cooldowns are **first-class overlay windows**. They are **not** a tail of `wsd.decay_steps` and they do **not** rewrite the global WSD curve outside their window.

Config:

```json
"cooldowns": [
  { "id": "stable-mid", "start_step": 8, "steps": 4, "end_lr": 1e-5 }
]
```

Rules (schema + host/Python dry-run):

1. `cooldowns` must be a **non-empty** array.
2. Each window is `[start_step, start_step + steps)` on the same 0-based axis as WSD.
3. Windows must **not overlap**. Overlap → validation **FAILED**.
4. `start_step + steps` must be `≤ WsdLength` (overlay stays on the WSD timeline).
5. Inside a window, LR is **its own** segment: starts at **WSD-only** `lr(start_step)`, then linear to `end_lr` over `steps`.
6. Outside every cooldown window, LR is **WSD only**.

```
start_lr = WsdLr(start_step)          # ignore other cooldowns
local    = s - start_step
lr       = lerp(start_lr, end_lr, local, steps)
```

Example (`stable-mid`): WSD at step `8` is still `base_lr` (late stable). Cooldown then linear `1e-3 → 1e-5` on steps `8,9,10,11`. Steps `12..15` return to WSD linear decay (`base_lr → min_lr`). The global decay knot points are unchanged.

## Bit-widths

| Width | Smoke | FakeQuant | STE clip (normalized) | HLSL |
| --- | --- | --- | --- | --- |
| **ternary** (default) | green | absmean, codes `{-1,0,+1}` — ADR 0002 | `\|W/s\| ≤ 1` | Fase 3 `fakequant_ternary.hlsl` / `ste_backward.hlsl` **unchanged**; `--qat-smoke` does **not** re-dispatch them |
| **2-bit** | host-selectable | absmean midrise `{±0.5, ±1.5} * s` (Fase 2 `levels=4` lattice) | `\|W/s\| ≤ 1.5` | **no new kernel** |
| **4-bit** | host-selectable | absmean midrise `{±0.5 … ±7.5} * s` (Fase 2 `levels=16` lattice) | `\|W/s\| ≤ 7.5` | **no new kernel** |

Train↔deploy: 2/4-bit codes use `code = clip(round(W/s + half), 0, levels-1)`, `W_q = (code - half) * s`, `half = (levels-1)/2`. That matches [flp2-forward.md](flp2-forward.md) scalar decode. Scale `s = mean(|W|)` (`1` if `0`) is a **constant** in the backward (no absmean gradient).

CLI override: `--bit-width ternary|2|4`. Schedule field: `qat.bit_width`. Default smoke path is **ternary**. 2/4-bit host path is real FakeQuant (Fase 3 stubs are no longer no-ops). No GPU parity numbers are claimed for 2/4-bit.

## Commands

```bat
python scripts\validate_qat_schedule.py examples\qat-wsd-smoke.json --dry-run
.\build\Release\xbox_gpu_host.exe --qat-smoke --steps 16
.\build\Release\xbox_gpu_host.exe --qat-smoke --dry-run
.\build\Release\xbox_gpu_host.exe --qat-smoke --steps 16 --bit-width 2
```

Linux / no D3D12:

```bash
python3 scripts/validate_qat_schedule.py examples/qat-wsd-smoke.json --dry-run
./build/xbox_gpu_host --qat-smoke --steps 16
# CPU N-step loop, then BLOCKED: no D3D12 device — dispatch log not invented.
```

Honest first lines:

- `STATUS: qat-schedule dry-run ok` — schema + overlap + LR table only.
- `STATUS: qat-smoke ok` then `BLOCKED: no D3D12 device` — host loop finished; no GPU dispatch.
- `STATUS: qat-smoke ok` on Windows — host loop finished; Fase 3 HLSL **not** re-dispatched by this command.
- `FAILED: …` — load, overlap, or a train step failed.

Measured `loss_before` / `loss_after` (if printed) are **not** a quality curve and **not** tok/s.

## Schema

- Config: [`examples/qat-wsd-smoke.json`](../examples/qat-wsd-smoke.json)
- JSON Schema (documentation): [`examples/qat-wsd.schema.json`](../examples/qat-wsd.schema.json)
- Validator: [`scripts/validate_qat_schedule.py`](../scripts/validate_qat_schedule.py) (hand-rolled; no `jsonschema` package)

## Out of scope

- New `fakequant_2bit` / `fakequant_4bit` HLSL kernels (left for HLSL Kernels if a later phase needs GPU FakeQuant beyond ternary)
- Cosine WSD decay
- Invented loss / tok/s / Series S\|X benches
- DirectML optimizer
- Modifying [gianlucamazza/xllama](https://github.com/gianlucamazza/xllama)
- Fase 6 console validation — active E0 evidence and unmeasured historical host workloads in [docs/console.md](console.md)
