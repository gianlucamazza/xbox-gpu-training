# ADR 0005 — Repository authority and backend evidence

- Status: Accepted
- Date: 2026-10-01 (explicit owner confirmation during release planning)
- Supersedes: ownership and documentation clauses of ADR 0001/0003/0004 only; execution and numerical requirements stay unchanged
- Labels: `adr`, `documentation`

## Context

FloppyLM [ADR 0012](https://github.com/gianlucamazza/floppylm/blob/main/docs/adr/0012-repo-boundaries.md)
already assigns repository ownership. Backend pages repeat model semantics, scientific
thresholds and companion command lines; those copies drift. Accepted ADRs are historical
records and must not be rewritten to repair that drift.

## Decision

1. FloppyLM owns model/codec/optimizer semantics, FLP2, versioned contracts, fixtures,
   corpus preparation, scientific gates, campaign orchestration and protected evaluation.
2. This repository owns the only native FloppyLM training executor: DX12/HLSL/UWP,
   its build and device lifecycle, and package-bound hardware evidence. CPU reference
   execution and the historical Win32 lane certify no console or scientific result.
3. Backend pages link the canonical FloppyLM decisions, schemas and operation procedures;
   they describe only backend implementation, build/sign/install and hardware evidence.
   Contracts remain vendored at an explicit commit with hash-checked golden fixtures.
4. New hardware evidence is immutable and owned here. FloppyLM records its companion
   proofs and references the backend evidence; a shared release/package identity binds
   both records. Existing evidence stays in place as history.
5. ADR 0001–0004 remain unchanged. This ADR supersedes their ownership/documentation
   clauses without changing shaders, reduction order, optimizer, checkpoints or gates.

## Consequences

There is one authoritative statement of scientific semantics. A backend fix must pass
its pinned contracts and independent acceptance. Build success, TLS pinning and screenshots
are separate proofs; none establishes language-model quality. A second native backend
requires a superseding FloppyLM ADR and cross-backend parity.

## Alternatives considered

| Alternative | Why not |
| --- | --- |
| Keep copied protocol thresholds and companion CLI examples | Copies can drift while link and unit checks still pass. |
| Rewrite accepted ADRs or move old evidence | Changes the historical decision or breaks provenance. |
| Share a new GPU/deploy library with xllama | FloppyLM ADR 0012 rejects it; the execution workloads differ. |
