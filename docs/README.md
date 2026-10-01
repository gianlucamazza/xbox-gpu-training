# Documentation map

Each fact has one home; pages link instead of restating. Start with
[status.md](status.md) for the current state and [claims-policy.md](claims-policy.md)
for what may be claimed.

## Explanation

| Page | Content |
| --- | --- |
| [architecture.md](architecture.md) | Problem, the two lanes, E0 system diagram, memory |
| [e0/overview.md](e0/overview.md) | E0 responsibilities, decisions, data flow |
| [claims-policy.md](claims-policy.md) | Evidence tags, non-claims, repository boundaries |
| [adr/](adr/README.md) | Architecture decision records |
| [platform/](platform/README.md) | Public Xbox / UWP / DirectX facts |

## How-to

| Page | Content |
| --- | --- |
| [e0/runbook.md](e0/runbook.md) | Build, sign, install, accept, run and stop E0 on the console |
| [diagnostic/setup.md](diagnostic/setup.md) | Windows toolchain and diagnostic-host commands |

## Reference

| Page | Content |
| --- | --- |
| [status.md](status.md) | Active package, measured values, open items (single source of truth) |
| [e0/engine.md](e0/engine.md) | E0 code map, training step, execution engine, bit-identity rule |
| [e0/job-protocol.md](e0/job-protocol.md) | Console inbox, job / status / device schemas |
| [e0/acceptance.md](e0/acceptance.md) | Gates, functional proofs, evidence file formats |
| [evidence/](evidence/README.md) | Committed console evidence per package |
| [glossary.md](glossary.md) | Terms and spelling |

## Results and history

| Page | Content |
| --- | --- |
| [results.md](results.md) | Published results, peer comparison, limitations |
| [history.md](history.md) | Timeline and Fase 0–7 outcomes |
| [figures/](figures/README.md) | Figure placeholders |
| [diagnostic/](diagnostic/README.md) | Historical Fase 0–5 lane |
| [archive/execution-plan.md](archive/execution-plan.md) | Superseded agent playbook |

## Moved pages

Old paths kept as redirect stubs: `setup.md`, `ggml-baseline.md`, `flp2-forward.md`,
`ste-adamw.md`, `memory-budget.md`, `qat-wsd.md` → [diagnostic/](diagnostic/README.md);
`execution-plan.md` → [archive/](archive/execution-plan.md); `console.md` →
[status.md](status.md) and [e0/runbook.md](e0/runbook.md);
`platform/blockers-fase6-validation.md` → [platform/console-constraints.md](platform/console-constraints.md).
