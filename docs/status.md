# Status

Single source of truth for the current state of the project. Other pages link here
instead of restating these values. Numbers below are checked against the evidence
JSON by `scripts/check_doc_claims.py`; tags follow [claims-policy.md](claims-policy.md).

Last updated: 2026-10-03.

## Active package

| Field            | Value                                                                                                         |
| ---------------- | ------------------------------------------------------------------------------------------------------------- |
| Package          | `GianlucaMazza.XgpuE0_0.1.0.98_x64__g0p5dcfz4t9z4`                                                            |
| Engine           | E0.1 GPU-resident ([PR #18](https://github.com/gianlucamazza/xbox-gpu-training/pull/18), merged as `7abd040`) |
| Installed source | `cc134fe4458ffa51c73331ee6dd6af4a6ae8b0d7`                                                                    |
| CI run           | [37112204165](https://github.com/gianlucamazza/xbox-gpu-training/actions/runs/37112204165)                    |
| Hardware         | Retail Xbox Series S, Dev Mode, adapter `SraKmd_arden`                                                        |
| Evidence         | [e0-20261003-098](evidence/e0-20261003-098/notes.md)                                                |

The package passed the full acceptance ([e0/acceptance.md](e0/acceptance.md)); its
bit identity with the previous package is in the table below.

## Measured on Series S (Sourced, functional)

| Item                                         | Value                                                         | Source                                                                     |
| -------------------------------------------- | ------------------------------------------------------------- | -------------------------------------------------------------------------- |
| Independent GPU operation cases              | 52 passed                                                     | [kernel-parity.json](evidence/e0-20261003-098/kernel-parity.json)     |
| Held-out model fixtures                      | 36 passed                                                     | [acceptance.json](evidence/e0-20261003-098/acceptance.json)           |
| Identical-input AdamW, exact resume          | passed                                                        | [acceptance.json](evidence/e0-20261003-098/acceptance.json)           |
| Real Dev Home suspension and recovery        | passed, checkpoint at step 93 of the 1844-step trunk         | [lifecycle.json](evidence/e0-20261003-098/lifecycle.json)             |
| Real published-fence watchdog | exited after 600.460 s; new worker and exact explicit recovery | [watchdog.json](evidence/e0-20261003-098/watchdog.json) |
| Runner recovery                              | exact and idempotent                                          | [runner-recovery.json](evidence/e0-20261003-098/runner-recovery.json) |
| Worker probes                                | wrong identity and oversized dispatch rejected; worker reused | [worker.json](evidence/e0-20261003-098/worker.json)                   |
| Representative throughput                    | 9747.980 token/s                                             | [throughput.json](evidence/e0-20261003-098/throughput.json)           |
| Peak app memory (same run)                   | 127991808 bytes                                               | [throughput.json](evidence/e0-20261003-098/throughput.json)           |
| Bit identity with `0.1.0.95` | 38 of 38 acceptance cases; resumed and uninterrupted branch weights and benchmark branch artifacts identical | [bit-identity.json](evidence/e0-20261003-098/bit-identity.json) |
| Previous package, same benchmark (`0.1.0.95`) | 9752.238 token/s, 123494400 bytes                               | [throughput.json](evidence/e0-20261003-098/reference-095/throughput.json)                    |

Benchmark configuration: d=96, layers=3, heads=6, d_ff=391, ctx=256, batch=32,
ternary core / 4-bit embeddings / row16 scales, 147456 tokens on a synthetic corpus.
The estimate excludes corpus upload and Python serialization/evaluation. These values
establish functional execution, not language-model quality.

## Idle GPU investigation

**Open: no validated GPU reduction.** Twelve controlled diagnostic modes falsified the
initial dashboard-invalidation hypothesis. The global engine-5 counter stayed high even
with a CoreWindow and no XAML or D3D12 initialization. The historical 0.1.0.56 measurement reported about
99.55% on that whole-console counter; it does not establish per-app GPU work or power.
See [investigation](evidence/e0-20261001-dashboard/notes.md#idle-gpu-investigation-remains-open)
and [raw finding](evidence/e0-20261001-dashboard/idle-investigation.json).
An independent GPU trace is needed for attribution. Diagnostic paths were not merged.

## Scientific campaign

No campaign was started for this functional qualification. Historical campaigns
remain bound to their original package and source; never resume them onto the
active package. FloppyLM owns the
[current scientific status](https://github.com/gianlucamazza/floppylm/blob/main/docs/STATUS.md).
The historical stall's initiating cause remains unproven; the controlled watchdog
test qualifies detection, process exit and explicit recovery only.

## Unmeasured

| Target                                                                                   | State            |
| ---------------------------------------------------------------------------------------- | ---------------- |
| Scientific E0 selection, paired seeds, costs, exclusions                                 | stopped; owner decision pending |
| Language-model quality / perplexity                                                      | **UNMEASURED**   |
| Series X                                                                                 | **UNMEASURED**   |
| Matched CPU versus console throughput                                                    | **UNMEASURED**   |
| PIX `.wpix` capture (Windows or console)                                                 | **UNMEASURED**   |
| Diagnostic-lane workloads (hello / matmul / FLP2 / stream-stress / qat-smoke) on console | **UNMEASURED**   |

## Next

1. Obtain independent GPU attribution for the unresolved idle counter.
2. Review the host correctness/runtime and backend qualification PRs; see the
   [accepted evidence](evidence/e0-20261003-098/notes.md).
3. Await the FloppyLM owner's scientific proposal decision before any new campaign.
4. Leave every unmeasured cell empty until measured.

Earlier packages and the full chronology: [history.md](history.md), [evidence index](evidence/README.md).
