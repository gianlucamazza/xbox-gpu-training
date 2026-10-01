# E0 acceptance

A package trains only after it passes acceptance on the console. Gates come from
[FloppyLM ADR 0010](https://github.com/gianlucamazza/floppylm/blob/main/docs/adr/0010-independent-numerical-gates.md) and are never relaxed after a failure.
How to run them: [runbook.md](runbook.md#4-accept).

## Gates

Numerical and scientific requirements are authoritative in FloppyLM's
[independent gates](https://github.com/gianlucamazza/floppylm/blob/main/docs/adr/0010-independent-numerical-gates.md)
and [Xbox operations](https://github.com/gianlucamazza/floppylm/blob/main/docs/operations/xbox-e0.md).
Use its acceptance producer and the [pinned contracts](../../contracts/floppylm/PIN.json);
case counts and outcomes for an installed package live in [status.md](../status.md).
The backend does not define a second set of acceptance thresholds.

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
