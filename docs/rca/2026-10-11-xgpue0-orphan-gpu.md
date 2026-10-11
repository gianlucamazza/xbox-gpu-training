# RCA: XgpuE0 stayed alive with GPU engine 5 pinned after campaign e0-campaign-20261009-109

Package `GianlucaMazza.XgpuE0_0.1.0.109` (historical; this note does not bump a release).
Operator-reported Device Portal observations: the process remained listed for about 40 h
after the host lifecycle unit stopped, with CPU 0% and `EnginesUtilization[5]` at 100%.
Manual process kill on 2026-10-11 dropped that engine to 0%. Code citations below are
from `c9a197a` unless marked as a companion path.

The UWP app and DX12 runtime live in this repository (`uwp/`, `src/cpp/e0/`).
Campaign orchestration, `campaign.json`, and the host lifecycle unit live in
[`gianlucamazza/floppylm`](https://github.com/gianlucamazza/floppylm).

## (a) Does the app exit, or get told to exit, when the campaign completes or the host unit stops?

**No. Proven.**

The worker loop never leaves ready-state polling. After each job it publishes
`ready` and sleeps one second (`uwp/App.cpp:105` `while (true)`, `:213-216`
`ready()` then `sleep_for(1s)`). `run_job` writes `status: completed` and
returns to that loop (`src/cpp/e0/job.cpp:650`). The only process exit on the
worker path is the published-fence handler (`uwp/App.cpp:63-64`), which fires
only while `state == "running"` (`src/cpp/e0/runtime_lifecycle.cpp:191-194`).
There is no inbox marker, host heartbeat, or campaign-complete signal that asks
the process to exit.

On the host, FloppyLM `experiments/e0_campaign.py` sets `campaign.json`
`status=completed` and writes `summary.json` (`finish_paired`, companion
lines 598-604) then returns. `Portal.wait` returns when the job state is not
`running` (companion `src/floppylm_xbox/portal.py` ~640). `Portal.cancel`
uploads `<id>.cancel` only (companion `portal.py:661`). The recover helper
documents terminate as an operator step and does not call Device Portal DELETE
(companion `scripts/e0_recover.py:10-12`). The lifecycle unit stop therefore
stops the host Python process; it does not terminate `XgpuE0.exe`.

## (b) Busy-wait, spin loop, or a persistent dispatch/present loop with no work?

**No compute spin in the trainer. Dashboard hold is not what pins the GPU. Proven on console.**

After a job the worker sleeps (`uwp/App.cpp` ready loop). `GpuKernel` records
`Dispatch` only inside `run` (`src/cpp/e0/dx12_kernel.cpp:199`). There is no
D3D12 `Present` or swap chain in the E0 kernel. In-flight GPU objects are kept
until process exit (`docs/e0/job-protocol.md` around the quarantine paragraph;
`src/cpp/e0/dx12_kernel.cpp:134-151`).

The pre-fix dashboard `DispatcherTimer` ticked every second for the process
lifetime and held a `DisplayRequest` (`c9a197a` `uwp/DashboardView.cpp`). That
is a persistent present/composition client, not a compute dispatch loop. It
was a candidate for engine 5, not a proof.

**On-console evidence, package `0.1.0.115` (tip `94769836` + pch `edbcefc3`):**
a healthy, freshly launched, idle app still showed engine 5 at 99.7–100% with
CPU 0% after the dashboard timer and `DisplayRequest` were released. When the
app exited, that engine dropped to 0%. The orphan signature
(engine ≥ threshold + CPU idle) therefore matches every normal idle
foreground app. The “dashboard holds the GPU” hypothesis is false on this
console. Most likely the foreground UWP/compositor engine is busy for any
foreground app; that mechanism is unverified. GPU utilisation is advisory
only. The real leftover is the process: it never exited, and the host never
terminated it.

## (c) A fence or wait that never signals (hung dispatch / TDR-like wait)?

**Not proven. Idle published-fence watch cannot fire. Hung leftover dispatch is a hypothesis.**

`PublishedFenceWatch` clears tracking when `job_active` is false
(`src/cpp/gpu_wait.h:92-95`). After `lifecycle->ready()` the heartbeat will not
classify a frozen fence. `BoundedGpuWait` polls `GetCompletedValue` every 250 ms
and fails at 600 s (`src/cpp/gpu_wait.h:55-70`). A hang inside that wait would
show periodic CPU wake-ups and would have tripped the 600 s deadline long
before 40 h. CPU 0% for tens of hours does not match that path.

**Hypothesis:** a dispatch submitted after the last completed status never
signaled, with the CPU no longer in `BoundedGpuWait`. The completed job at
2026-10-10 12:02 Europe/Rome argues the last `run_job` flush returned. There is
no on-console PIX/ETW trace in this repository for package `.109`.

## (d) Host teardown missing a console-app terminate?

**Yes. Proven.**

See (a). Companion campaign completion and unit stop have no Device Portal
`DELETE /api/taskmanager/process` (CSRF cookie must be echoed as `X-CSRF-Token`;
the companion portal client already does that for its own POST/DELETE file
calls, companion `portal.py:184-208`). This backend had no equivalent teardown
command before this RCA.

## Root cause vs hypotheses

| Item | Status |
| --- | --- |
| Worker stays in `while (true)` after the last job | Root cause |
| No shutdown / host-heartbeat / idle-exit on the app | Root cause |
| Host campaign end and unit stop do not terminate `XgpuE0.exe` | Root cause |
| Engine 5 pinned by the dashboard timer + `DisplayRequest` | False on console (`0.1.0.115`) |
| Foreground UWP/compositor engine busy for any foreground app | Unverified |
| Hung leftover compute dispatch after `status=completed` | Hypothesis |
| Why `campaign.json` completed after the lifecycle unit stopped | Not visible from this repo; companion host logs are required |

The leftover GPU reading is a side effect of a process that should already
have exited. Root cause = the app never exits after the last job, plus no
host teardown.

## Fix (this change)

The implementation is portable: no console address, user, campaign id or engine
index is compiled in. Defaults and overrides are in
[lifecycle.md](../e0/lifecycle.md).

- App exits on `LocalState/inbox/shutdown` once idle, or after the configured
  idle deadline with no work and no fresh `LocalState/host.heartbeat`
  (default 30 minutes; `LocalState/lifecycle.json` overrides).
- When no job is running the dashboard stops its timer and releases
  `DisplayRequest`, independent of process exit
  ([ADR 0010](../adr/0010-idle-compositor-release.md)). That does not drop
  the foreground GPU engine; it is not the stay-alive or orphan signal.
- Host tool `scripts/e0_console_lifecycle.py`: CSRF cookie discovery, 400/403
  retry, terminate by package (base64) and/or pid, and idle checks that use
  this instance's start time (`LocalState/instance.json`, else a process
  `CreateTime` when the portal publishes one). A leftover `host.heartbeat`
  from an earlier instance is ignored. When start time is unknown the
  before-check reports `orphan: null` / `start_time_unknown` (strict mode
  refuses). GPU samples stay advisory. The consumed `inbox/shutdown`
  marker is deleted; `prepare` clears a stale one before launch.
- Companion runner and the host unit `ExecStop` still need to invoke that tool;
  that wiring lives in `gianlucamazza/floppylm`.
