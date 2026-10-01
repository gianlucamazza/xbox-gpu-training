# Status

Single source of truth for the current state of the project. Other pages link here
instead of restating these values. Numbers below are checked against the evidence
JSON by `scripts/check_doc_claims.py`; tags follow [claims-policy.md](claims-policy.md).

Last updated: 2026-10-01.

## Active package

| Field            | Value                                                                                                         |
| ---------------- | ------------------------------------------------------------------------------------------------------------- |
| Package          | `GianlucaMazza.XgpuE0_0.1.0.28_x64__g0p5dcfz4t9z4`                                                            |
| Engine           | E0.1 GPU-resident ([PR #18](https://github.com/gianlucamazza/xbox-gpu-training/pull/18), merged as `7abd040`) |
| Installed source | `25f8bc3966ffae940658be94161d31edd83492c9`                                                                    |
| CI run           | [36839565773](https://github.com/gianlucamazza/xbox-gpu-training/actions/runs/36839565773)                    |
| Hardware         | Retail Xbox Series S, Dev Mode, adapter `SraKmd_arden`                                                        |
| Evidence         | [e0-20261001-resident](evidence/e0-20261001-resident/notes.md)                                                |

The package passed the full acceptance ([e0/acceptance.md](e0/acceptance.md)); its
bit identity with the previous package is in the table below.

## Measured on Series S (Sourced, functional)

| Item                                         | Value                                                         | Source                                                                     |
| -------------------------------------------- | ------------------------------------------------------------- | -------------------------------------------------------------------------- |
| Independent GPU operation cases              | 52 passed                                                     | [kernel-parity.json](evidence/e0-20261001-resident/kernel-parity.json)     |
| Held-out model fixtures                      | 36 passed                                                     | [acceptance.json](evidence/e0-20261001-resident/acceptance.json)           |
| Identical-input AdamW, exact resume          | passed                                                        | [acceptance.json](evidence/e0-20261001-resident/acceptance.json)           |
| Real Dev Home suspension and recovery        | passed, checkpoint at step 461 of the 1844-step trunk         | [lifecycle.json](evidence/e0-20261001-resident/lifecycle.json)             |
| Runner recovery                              | exact and idempotent                                          | [runner-recovery.json](evidence/e0-20261001-resident/runner-recovery.json) |
| Worker probes                                | wrong identity and oversized dispatch rejected; worker reused | [worker.json](evidence/e0-20261001-resident/worker.json)                   |
| Representative throughput                    | 10224.282 token/s                                             | [throughput.json](evidence/e0-20261001-resident/throughput.json)           |
| Peak app memory (same run)                   | 110366720 bytes                                               | [throughput.json](evidence/e0-20261001-resident/throughput.json)           |
| Bit identity with `0.1.0.24` | 38 of 38 acceptance cases; resumed and uninterrupted branch weights and benchmark branch artifacts identical | [bit-identity.json](evidence/e0-20261001-resident/bit-identity.json) |
| Previous engine, same benchmark (`0.1.0.24`) | 963.571 token/s, 91418624 bytes                               | [throughput.json](evidence/e0-20261001/throughput.json)                    |

Benchmark configuration: d=96, layers=3, heads=6, d_ff=391, ctx=256, batch=32,
ternary core / 4-bit embeddings / row16 scales, 147456 tokens on a synthetic corpus.
The estimate excludes corpus upload and Python serialization/evaluation. These values
establish functional execution, not language-model quality.

## Scientific campaign

The first campaign (`e0-20261001T074326Z-503df0`, package `0.1.0.24`, GPU busy about
8.6% of wall time) was stopped cleanly at trunk step 455 to switch engines. Campaign
`e0-20261001T090514Z-4236fd` started on 2026-10-01 on the active package. Its live
state is tracked by the companion runner; results are published here only after all
gates and the single reserved final test pass.

## Unmeasured

| Target                                                                                   | State            |
| ---------------------------------------------------------------------------------------- | ---------------- |
| Scientific E0 selection, paired seeds, costs, exclusions                                 | pending campaign |
| Language-model quality / perplexity                                                      | **UNMEASURED**   |
| Series X                                                                                 | **UNMEASURED**   |
| Matched CPU versus console throughput                                                    | **UNMEASURED**   |
| PIX `.wpix` capture (Windows or console)                                                 | **UNMEASURED**   |
| Diagnostic-lane workloads (hello / matmul / FLP2 / stream-stress / qat-smoke) on console | **UNMEASURED**   |

## Next

1. Complete the sequential row16/row8log campaign with frozen artifact hashes.
2. Diagnose any failed native job; recover bound interrupted trials explicitly.
3. Publish selection, paired statistics, costs and exclusions in [results.md](results.md).
4. Leave every unmeasured cell empty until measured.

Earlier packages and the full chronology: [history.md](history.md), [evidence index](evidence/README.md).
