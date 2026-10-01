# ADR 0006 — Bounded GPU waits and worker lifecycle

- Status: Accepted
- Date: 2026-10-01 (owner approved the coordinated implementation plan)
- Scope: native synchronization, worker ownership, runtime fault evidence

## Context

An E0 stall exposed an infinite unchecked fence wait and insufficient distinction
between a live worker and a stale job report. The scientific cause is unproven.
FloppyLM ADR 0017 owns the coordinated operational contract and explicit recovery.

## Decision

Implement the approved 250 ms polling / 600 s per-fence bound, verify wait results
and device health, preserve fault context, and quarantine a failed GPU context.
Do not reuse in-flight resources or wait again during fault teardown.

Publish a unique locked worker instance and five-second heartbeat through the
FloppyLM-owned worker contract. Persist a claim containing immutable submitted
job bytes before execution. Reconcile orphaned claims and verify checkpoints and
branches before advertising readiness; never automatically execute abandoned jobs.

Use explicit recovery only. Keep numerical kernels and training schedules fixed.
Local/CI development is isolated; installation and hardware qualification happen
only after the active scientific E0 campaign has closed. Fault-injection hooks
are functional-only and must be rejected by scientific jobs.

## Consequences

Windows/native tests and hardware fault/lifecycle evidence are separate gates.
Generic scientific failures are not silently retried. Resource quarantine is
bounded to one failed worker lifetime and released by explicit process restart.

## Alternatives

Infinite waits, automatic restart/retraining, and treating stored running status
as evidence of live execution were rejected.
