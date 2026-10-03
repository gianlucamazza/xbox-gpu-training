# ADR 0009: Functional qualification of the published-fence watchdog

## Status

Accepted — 2026-10-03, owner-approved host-and-Xbox implementation plan.
Completes ADR 0006 hardware qualification without changing its timeout or
explicit-recovery boundary. Companion semantics are owned by FloppyLM ADR 0018.

## Context

The published-fence watchdog has portable tests but the accepted package gates did
not trigger it. Directly injecting a fault result would not exercise the heartbeat
thread, actual timeout, process exit and subsequent checkpoint reconciliation.

## Decision

Consume the companion's optional `runtime_fault_probe` training-job contract.
Only explicitly functional, fresh jobs may request `published_fence_stall` at a
positive, reachable checkpoint step. Validate before dispatch. After atomically
publishing that checkpoint and status, park only the execution thread without
holding locks. Keep the ordinary heartbeat and 600-second watchdog active.

The host removes the probe when explicitly resuming, using the existing publication
journal. Run the probe against a synthetic uninterrupted control on an exact CI
Release package. Require fault evidence, process exit, a new worker identity,
verified recovery and identical final numerical artifacts. Preserve scientific data.

## Consequences

Normal jobs and shaders are unchanged. The hook is dormant unless explicitly
requested and is forbidden in scientific jobs and resumed payloads. No automatic
restart is introduced. Hardware evidence is required separately from portable tests.

## Alternatives

Calling the fault handler directly, shortening the hardware deadline, and using a
CPU-only simulation do not exercise the production watchdog path on the console.
