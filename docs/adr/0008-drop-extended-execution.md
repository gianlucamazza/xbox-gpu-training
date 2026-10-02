# ADR 0008 — Drop Extended Execution after lifecycle failure

- Status: Accepted
- Date: 2026-10-02
- Supersedes: ADR 0007 clauses that request `ExtendedExecutionSession` at worker
  start and publish `worker.json` `extended_execution`
- Labels: `adr`, `uwp`
- Scope: UWP process lifetime on Xbox Dev Mode; does not change kernels, shaders,
  DisplayRequest process-lifetime, or `App::Suspending`

## Context

ADR 0007 requested `ExtendedExecutionSession` as best-effort stay-alive and
required the lifecycle gate to keep working. Package 0.1.0.84
(`b638f0c99cd19c9f28594d0b0ca95b67666622e1`) published
`extended_execution=allowed`. Canonical acceptance, benchmark, worker and
recovery passed. The lifecycle probe then launched Dev Home while a job was
running at trunk 832; eight seconds later the job was `completed` at trunk
1844 with no `<id>.cancel`. `portal.wait` returned that completion and the
host failed looking for a `suspend` marker.

An allowed Extended Execution grant delayed OS `Suspending` through steal-focus.
Cooperative interrupt/resume is the recovery contract (FloppyLM ADR 0017). A
campaign cannot bind a package that fails that gate.

Always-on `DisplayRequest` is unchanged: it covers idle gaps without refusing
`Suspending`.

## Decision

1. Do not request `ExtendedExecutionSession`.
2. Do not publish `worker.json` `extended_execution` from the worker.
3. Keep the dashboard `DisplayRequest` for the process lifetime (ADR 0007).
4. Leave `App::Suspending` unchanged.
5. A package that carries this decision must pass the lifecycle acceptance
   gate before a campaign binds it.

## Consequences

Dev Home steal-focus can suspend the app again. Idle gaps still hold the TV
stay-awake request. The optional `set_extended_execution` API remains in
`RuntimeLifecycle` for tests; the UWP worker does not call it.

## Alternatives considered

| Alternative | Why not |
| --- | --- |
| Keep EE and skip the lifecycle gate | Breaks FloppyLM resume and ADR 0017. |
| Keep EE and add `allow_suspend` | Host `experiments/` change; still would not restore cooperative suspend on this package. |
| Drop DisplayRequest as well | Returns the idle-gap tombstone that 0007 addressed. |
