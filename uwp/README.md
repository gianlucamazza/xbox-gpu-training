# `uwp/` — XgpuE0 UWP app

x64 UWP App that hosts the E0 trainer on a Series S in Dev Mode. On launch it starts
a worker that reports `device.json` and runs jobs from `LocalState/inbox`; on
suspension it asks the active job to checkpoint.

| File | Role |
| --- | --- |
| `App.cpp`, `App.h`, `app.idl`, `module.cpp`, `pch.h` | App, worker loop, suspension handling |
| `AppxManifest.xml` | Package manifest; identity and version are stamped at build time |
| `XgpuE0.vcxproj`, `packages.config` | MSBuild project (CppWinRT) |
| `Assets/` | Logos; `e0_tensor.cso` is compiled here by the build |

Build with [`scripts/build-e0-uwp.ps1`](../scripts/build-e0-uwp.ps1) (CI workflow
`e0-uwp.yml`). Identity parameters, signing, install and operations:
[docs/e0/runbook.md](../docs/e0/runbook.md). Protocol: [docs/e0/job-protocol.md](../docs/e0/job-protocol.md).
