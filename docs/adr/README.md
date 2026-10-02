# Architecture decision records

An architecture change needs a new ADR that explicitly supersedes the affected clauses
and the `adr` label. Start from [TEMPLATE.md](TEMPLATE.md). Decisions are never
rewritten; superseding ADRs say what they replace.

| ADR | Title | Status | Scope |
| --- | --- | --- | --- |
| [0001](0001-architecture.md) | Architecture for quantized LLM training research on Xbox GPU | Accepted, amended 2026-10-01 | Whole repo: DX12/HLSL, no CUDA, fp32 masters, public GDK only |
| [0002](0002-ste-qat-mapping.md) | STE / FakeQuant mapping | Accepted | Diagnostic lane only (superseded for E0 by 0003) |
| [0003](0003-floppylm-e0.md) | Execute the FloppyLM E0 protocol on Xbox GPU | Accepted, amended 2026-10-01 | E0 trainer |
| [0004](0004-independent-e0-gates.md) | Independent E0 numerical gates | Accepted | Historical E0 acceptance decision; current authority follows 0005 |
| [0005](0005-repository-authority.md) | Repository authority and backend evidence | Accepted | FloppyLM semantics, backend execution and hardware proof ownership |

| [0006](0006-runtime-liveness.md) | Bounded GPU waits and explicit worker recovery | Accepted | Worker ownership, fault quarantine and post-E0 hardware gate |
| [0007](0007-uwp-stay-alive.md) | UWP stay-alive during idle gaps | Accepted | Always-on DisplayRequest and best-effort Extended Execution; cooperative suspend stays the lifecycle contract |

Companion decisions (FloppyLM ADR 0011 campaign, ADR 0012 repository boundaries)
live in the [FloppyLM](https://github.com/gianlucamazza/floppylm) repository.
