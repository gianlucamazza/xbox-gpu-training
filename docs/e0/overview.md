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

Repository authority is fixed by [ADR 0005](../adr/0005-repository-authority.md).
The canonical E0 model and numerical requirements live in FloppyLM:
[protocol](https://github.com/gianlucamazza/floppylm/blob/main/docs/adr/0008-e0-numeric-protocol.md),
[independent gates](https://github.com/gianlucamazza/floppylm/blob/main/docs/adr/0010-independent-numerical-gates.md),
[scientific scale selection](https://github.com/gianlucamazza/floppylm/blob/main/docs/adr/0011-e0-row-scale-selection.md),
and [repository boundaries](https://github.com/gianlucamazza/floppylm/blob/main/docs/adr/0012-repo-boundaries.md).
Backend ADR 0003/0004 preserve the original implementation decision as history.
The diagnostic lane keeps its own contract and supplies no E0 acceptance.

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
