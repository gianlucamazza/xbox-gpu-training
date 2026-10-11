# ADR 0010 — Idle releases the compositor hold

- Status: Accepted
- Date: 2026-10-11
- Supersedes: ADR 0007 clause 1 (process-lifetime `DisplayRequest`); ADR 0008
  clause 3 (keep `DisplayRequest` for the process lifetime)
- Labels: `adr`, `uwp`
- Scope: Dashboard timer and `DisplayRequest` while the worker is idle. Does
  not change kernels, shaders, cooperative `Suspending`, or the scientific
  job contract.

## Context

ADR 0007 held a `DisplayRequest` from dashboard construction until process
teardown so idle gaps would not drop the TV stay-awake request. ADR 0008 kept
that hold after dropping `ExtendedExecutionSession`.

Package `0.1.0.109` then stayed listed for about 40 h after campaign
`e0-campaign-20261009-109` ended, with CPU 0% and a GPU engine pinned at
100% until a manual Device Portal kill
([RCA](../rca/2026-10-11-xgpue0-orphan-gpu.md)). The worker never exited the
ready loop, and the host never terminated the process. The dashboard
`DispatcherTimer` and `DisplayRequest` remained a compositor client with no
job. Engine identity is not proven; a persistent UI tick with no work is.

Process stay-alive during a campaign is now the host heartbeat plus the
configurable idle/shutdown watchdog ([lifecycle.md](../e0/lifecycle.md)). A
process-lifetime compositor hold is no longer the stay-alive mechanism.

## Decision

1. Hold `DisplayRequest` and start the dashboard `DispatcherTimer` only while
   a job is active.
2. When idle, stop the timer and release `DisplayRequest`, independent of
   process exit. A later job starts both again.
3. The worker still stays in the ready loop until `inbox/shutdown` or the
   idle/heartbeat deadline. That is process stay-alive, not compositor hold.
4. Leave `App::Suspending` unchanged: write `.cancel` `suspend` and hold the
   4 s deferral. Do not request `ExtendedExecutionSession`.
5. Do not auto-relaunch the package.

## Consequences

Idle no longer keeps a compositor tick. OS steal-focus can still suspend;
cooperative interrupt/resume remains recovery. Idle gaps without a host
heartbeat expire at the configured deadline (default 30 minutes). Campaign
end still requires the host teardown tool. A package that carries this
decision must pass the lifecycle acceptance gate before a campaign binds it.

Clarification — 2026-10-11 on-console (`0.1.0.115`): releasing the timer and
`DisplayRequest` did not drop engine 5 (still 99.7–100% at CPU 0%). Process
exit did. GPU utilisation is not the orphan predicate; see
[the RCA](../rca/2026-10-11-xgpue0-orphan-gpu.md).

## Alternatives considered

| Alternative | Why not |
| --- | --- |
| Keep process-lifetime `DisplayRequest` (ADR 0007/0008) | Leaves a compositor client after the last job. On-console evidence later showed it is not what pins the foreground GPU engine. |
| Exit the process immediately when idle | Breaks in-campaign gaps the host still intends to fill; the heartbeat covers those. |
| Auto-relaunch sidecar | Forbidden by FloppyLM ADR 0017. |
