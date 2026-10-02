# ADR 0007 — UWP stay-alive during idle gaps

- Status: Accepted
- Date: 2026-10-02
- Labels: `adr`, `uwp`
- Scope: UWP process lifetime on Xbox Dev Mode; does not change kernels, shaders
  or the scientific job contract

## Context

Xbox suspends a UWP App that leaves the foreground. The E0 dashboard previously
held a `DisplayRequest` only while `status.state` was `running` and released it
on idle. Idle gaps and Dev Home stealing focus then tombstoned `XgpuE0.exe`
while Device Portal still served a stale `device.json` `ready`. Two 0.1.0.80
acceptance attempts and campaign `40a67c` trial 000 died this way.

Cooperative `Suspending` is the lifecycle contract: the app writes `<id>.cancel`
containing `suspend`, holds a 4 s deferral, and the host resumes from the
interrupted checkpoint. FloppyLM ADR 0017 forbids automatic restart. A
`DisplayRequest` held during a running job did not block Dev Home steal-focus
(0.1.0.68 lifecycle passed). Always-on `DisplayRequest` therefore cannot replace
`Suspending`; it only covers idle and opportunistic gaps.

`ExtendedExecutionSession` may be denied or immediately revoked on Xbox. If a
grant actually blocked suspend, the lifecycle gate would fail until the host
could opt into suspend — that host change is out of this decision.

## Decision

1. Hold a `DisplayRequest` from dashboard construction until process teardown.
   Idle, last-job and running paths must not release it.
2. Leave `App::Suspending` unchanged: write `.cancel` `suspend` and hold the
   4 s deferral. Do not refuse OS suspend.
3. At worker start, request `ExtendedExecutionSession` with reason
   `Unspecified` and description `FloppyLM E0 GPU trainer`. Keep the session
   object alive only while the OS reports `Allowed`.
4. Treat grant, denial, revocation and API absence as non-fatal. Do not fail
   the worker. Publish the outcome on optional `worker.json` field
   `extended_execution` (`requested` / `allowed` / `denied` / `revoked` /
   `unsupported`). The host ignores unknown keys.
5. Do not auto-relaunch the package. Do not add an `allow_suspend` host flag in
   this change. Lifecycle evidence remains a required acceptance gate for any
   package that carries this decision.

## Consequences

Idle gaps no longer drop the TV stay-awake request. An active
`ExtendedExecutionSession` grant can delay OS `Suspending` while the app is
minimized; the `Suspending` handler itself is unchanged. OS steal-focus still
suspends; the existing interrupt/resume path remains the recovery. Xbox may
deny or revoke extended execution; `worker.json` records that without changing
training. A package that actually blocks Dev Home suspend fails the lifecycle
gate and must drop extended execution before a campaign binds it.

## Alternatives considered

| Alternative | Why not |
| --- | --- |
| Keep `DisplayRequest` only while a job runs | Idle gaps released it and Xbox suspended the app. |
| Refuse `Suspending` / skip `.cancel` | Breaks the lifecycle gate and FloppyLM resume. |
| Auto-relaunch sidecar | Forbidden by FloppyLM ADR 0017. |
| Game designation | Out of scope; changes identity and memory class. |
| `allow_suspend` host flag | Requires a FloppyLM `experiments/` change; not in this decision. |
