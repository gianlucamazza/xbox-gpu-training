# E0 acceptance

A package trains only after it passes acceptance on the console. Gates come from
[ADR 0004](../adr/0004-independent-e0-gates.md) and are never relaxed after a failure.
How to run them: [runbook.md](runbook.md#4-accept).

## Gates

| Gate | Requirement | Oracle |
| --- | --- | --- |
| Quantization | Exact symbols and canonical serialized scale bytes | FloppyLM Python |
| Operations | 52 independent cases (forward and every input gradient) within `1e-5 + 1e-4·|ref|` per element | Independent PyTorch operations and autograd |
| Model fixtures | 36 held-out fixtures: logits, loss, gradients within the same bound | FloppyLM Python |
| Optimizer | AdamW on identical weights, moments, gradients, clipping, LR and step: weights and both moments within the bound | FloppyLM Python |
| Resume | Interrupted + resumed run equals the uninterrupted run exactly (weights, moments, branches) | Native run |
| Hardware | `hardware_gpu: true` on the expected adapter, package and commit | `device.json` |

The integrated one-step parameter error is kept as a diagnostic only (ADR 0004).

## Functional proofs

Not numerical gates, but required before a campaign:

| Proof | Shows |
| --- | --- |
| Worker probes | A job with a mismatched id and an oversized dispatch are rejected; the worker then accepts valid work |
| Runner recovery | Completed retrieval is unchanged; runner resume is idempotent; interrupted recovery is exact |
| Lifecycle | A real Dev Home suspension writes a checkpoint; the recovered run ends identical to an uninterrupted one |
| Throughput | Representative synthetic benchmark: wall time, GPU time, dispatches, transfers, peak app memory |
| Bit identity | Engine changes only: outputs identical to the previous package ([engine.md](engine.md#bit-identity-rule)) |

## Evidence files

Each accepted package gets a directory under [`docs/evidence/`](../evidence/README.md):

| File | Content |
| --- | --- |
| `notes.md` | Human summary: package, source, CI run, results, limits |
| `package-lineage.json` | CI run, unsigned/signed package SHA-256, per-payload hashes, `payloads_preserved` |
| `acceptance.json` | Device record, kernel gates, 36 fixture gates, optimizer gates, `ok` |
| `kernel-parity.json` | Per-case `max_abs`, `max_rel`, `max_bound_excess`, `ok` for the 52 cases |
| `worker.json` | Probe results |
| `runner-recovery.json` | Recovery checks |
| `lifecycle.json` | Suspension checkpoint and resumed/uninterrupted comparison |
| `throughput.json` | Benchmark config and counters, `purpose: functional` |
| `bit-identity.json` | Engine-change comparison (when applicable) |

Evidence certifies functional execution and numerical agreement with the oracle,
not language-model quality ([claims-policy.md](../claims-policy.md)).
