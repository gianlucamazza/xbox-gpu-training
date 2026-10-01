# E0 overview

E0 is the FloppyLM experiment-0 protocol executed natively on a retail Xbox Series S
in Dev Mode. It is the active trainer of this repository. Current package and
measurements: [status.md](../status.md).

## Responsibilities

| Concern | Owner |
| --- | --- |
| Model, quantization, optimizer semantics, FLP2 packing, evaluation | Companion FloppyLM (Python oracle) |
| Initial fp32 weights, SHA-256-bound corpus, deterministic batch indices | Companion |
| Fixtures, acceptance comparison, campaign scheduling, final-test reservation | Companion |
| Forward/backward on GPU, AdamW, WSD branches, checkpoints | This repo: `src/cpp/e0/`, `src/hlsl/e0_tensor.hlsl` |
| Console worker, job inbox, suspension handling | This repo: `uwp/` |

The console never sees test data and never packs FLP2: it returns master weights
to Python ([claims-policy.md](../claims-policy.md#repository-boundaries)).

## Decisions

- [ADR 0003](../adr/0003-floppylm-e0.md): match FloppyLM exactly — ternary retained-entry
  scales with delta thresholds, fp16 scale/norm round trips, row16/row8log/tensor16
  policies, identity STE, exact zero rows, floor/midrise even-level grids; AdamW
  (β1 0.9, β2 0.95, ε 1e-8, decay on quantized matrices only, global norm clip 1);
  stable-trunk WSD with isolated branches ending at T, 2T, 4T. CPU execution is a
  reference mode, never a GPU fallback.
- [ADR 0004](../adr/0004-independent-e0-gates.md): numerical gates — exact symbols and
  scale bytes; `|actual − reference| ≤ 1e-5 + 1e-4·|reference|` per element; optimizer
  validated on identical inputs; exact checkpoint/resume equality.

The diagnostic-lane ADR 0002 (clipped STE, absmean, nearest rounding) does **not**
apply to E0.

## Data flow

```
 companion (Linux host)                          Series S (UWP App, LocalState)
 ─────────────────────                           ──────────────────────────────
 init weights, corpus, indices  ── upload ──▶   inbox/<asset files>
 <id>.job.json + <id>.ready     ── upload ──▶   worker claims → run_job
                                                 GPU forward/backward per sample
                                                 host AdamW on fp32 masters
 poll results/<id>/status.json  ◀─ download ──  status.json, checkpoint.json,
 branch-<T>.json → FLP2 + eval  ◀─ download ──  branch-<T>.json (weights)
 SIGTERM trial → <id>.cancel    ── upload ──▶   checkpoint + state=interrupted
```

Protocol detail: [job-protocol.md](job-protocol.md). Code structure:
[engine.md](engine.md). Operations: [runbook.md](runbook.md). Gates and evidence:
[acceptance.md](acceptance.md).
