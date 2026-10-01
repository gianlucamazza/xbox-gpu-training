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

Polls the active job's `job.json` and `results/<id>/status.json` once per second on the
UI thread and derives nothing about the schedule: phase, progress and markers come from
the `schedule` and `phase` fields that `run_job` publishes ([job protocol](../docs/e0/job-protocol.md)).
It shows job state and the age of the last update (stale after 10 min), progress over all
optimizer steps, a trunk-loss chart with warmup and cooldown markers (the EMA appears from 16 points on; before that the raw curve is the main line, since a short EMA lags far behind the data; marker labels stack and stay inside the chart), tokens/s,
tokens processed, GPU/wall time, peak memory, a labelled remaining-time estimate and the
`device.json` footer. While a job runs it holds a `DisplayRequest`.

Layout follows Microsoft's Xbox/TV guidance: 960×540 effective pixels inside the default
TV-safe bounds, text ≥ 12 epx (values 24 epx), and an explicit dark palette within RGB 16–235.
It does not change training state; a package carrying it still needs acceptance and the
[bit-identity rule](../docs/e0/engine.md#bit-identity-rule) before a campaign binds it.
