# `uwp/` — XgpuE0 UWP app

x64 UWP App that hosts the E0 trainer on a Series S in Dev Mode. On launch it starts
a worker that reports `device.json` and runs jobs from `LocalState/inbox`; on
suspension it asks the active job to checkpoint. The window shows a read-only
training dashboard.

| File | Role |
| --- | --- |
| `App.cpp`, `App.h`, `app.idl`, `module.cpp`, `pch.h` | App, worker loop, suspension handling |
| `DashboardView.cpp`, `DashboardView.h` | Dashboard window (XAML built in code) |
| `dashboard.cpp`, `dashboard.h`, `tests/` | WinRT-free view model and its host unit tests (`xgpu_e0_dashboard_test`) |
| `AppxManifest.xml` | Package manifest; identity and version are stamped at build time |
| `XgpuE0.vcxproj`, `packages.config` | MSBuild project (CppWinRT) |
| `Assets/` | Logos; `e0_tensor.cso` is compiled here by the build |

Build with [`scripts/build-e0-uwp.ps1`](../scripts/build-e0-uwp.ps1) (CI workflow
`e0-uwp.yml`). Identity parameters, signing, install and operations:
[docs/e0/runbook.md](../docs/e0/runbook.md). Protocol: [docs/e0/job-protocol.md](../docs/e0/job-protocol.md).

## Dashboard

Polls `worker.json`, the active job's `job.json` and `results/<id>/status.json` once per
second on the UI thread and derives nothing about the schedule: phase, progress and markers come from
the `schedule` and `phase` fields that `run_job` publishes ([job protocol](../docs/e0/job-protocol.md)).
The header shows job state, the age of the last status (stale after 10 min), a one-line
model/optimizer summary from `job.json` (`config`, `spec`; `T … tokens` is the trunk budget),
and a worker liveness line from `worker.json` (heartbeat age, last completed fence and
operation; unreachable after 30 s without a rewrite; a runtime fault is shown in red).
Below it:
progress in optimizer steps (trunk and cooldowns) with the published phase, and a strip with the state of the three branches (done with the
published `cooldown_seconds`, cooling with its percentage, pending). The trunk-loss chart has round
loss gridlines, step labels, warmup and cooldown markers (green once that branch is done) and a cursor
on the current step, the curve kept below the marker labels (the EMA appears from 16 points on; before that the raw curve is the main line, since
a short EMA lags far behind the data; marker labels stack and stay inside the chart). The curve is a
view of published `loss_series` when present: each poll replaces RAM history from that field, so
interrupt and resume reconstruct the trail instead of starting empty at the resume step. Status
without the field (older packages) still plots `last_loss` at `trunk_step`. Eight tiles show
trunk loss, tokens/s over the last 64 steps with the run-segment average, tokens processed of the total,
elapsed time of the run segment, a labelled remaining-time estimate, GPU/wall time, peak memory and the
trunk step of the last checkpoint; the footer shows `device.json`. It holds a `DisplayRequest` for the process lifetime so idle gaps do not release the TV stay-awake request; OS steal-focus still suspends ([ADR 0007](../docs/adr/0007-uwp-stay-alive.md), [ADR 0008](../docs/adr/0008-drop-extended-execution.md)). When idle,
the header names the last job and its final state, the progress bar takes that state's colour and the
tiles are labelled "last job". The worker line still updates. With no last job the chart shows
an empty-state caption instead of a blank surface. After that transition the idle layout is
stable; only `set_text`/`recolor` no-ops change.

Layout follows Microsoft's Xbox/TV guidance: 960×540 effective pixels inside the default
TV-safe bounds, text ≥ 12 epx (values 24 epx), and an explicit dark palette within RGB 16–235.
It does not change training state; a package carrying it still needs acceptance and the
[bit-identity rule](../docs/e0/engine.md#bit-identity-rule) before a campaign binds it.
