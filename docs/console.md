# Fase 6 — Series S|X Dev Mode validation

Status (2026-10-01): **Series S E0 functional execution measured**. Scientific
quality, Series X, matched CPU comparison and PIX capture remain unmeasured.
The original Win32 diagnostics remain a separate lane; their desktop results are
not copied into the E0 console results.

## Package and deployment

The separate x64 UWP Release app in `uwp/` was built by E0 UWP CI run 36792707081,
repacked and signed using the development certificate, then installed via Device
Portal on retail Series S. Executable, shader, resource and manifest payloads were
verified byte for byte against CI. Active package: `0.1.0.24`, source `6a124021`.
[Complete lineage and acceptance](evidence/e0-20261001/notes.md).

The companion Python runner owns corpus preparation, ordered samples, canonical
FLP2, validation and final-test reservation. Device Portal credentials remain
outside jobs and committed evidence. Set `XGPU_E0_PACKAGE` explicitly when several
versions are installed. Acceptance must match the active package and source.

## Results table

| Hardware | Workload | token/s | Peak app memory | Status |
| --- | --- | --- | --- | --- |
| Series S | 52 operation / 36 model fixtures | — | see acceptance | passed on hardware GPU |
| Series S | Representative synthetic E0, ctx=256, batch=32 | 963.571 | 91418624 bytes | completed |
| Series S | Interrupted / resumed / uninterrupted comparison | — | see lifecycle | exact weights, moments and branches |
| Series S | Real Dev Home suspension and runner recovery | — | see lifecycle | passed |
| Series S | Scientific E0 selection and final test | — | — | pending completion |
| Series X | E0 | — | — | no Series X measurement |
| CPU versus console | Matched throughput comparison | — | — | not measured |

These synthetic-corpus measurements establish functional execution. They do not
establish language-model quality or performance of all historical workloads.

## PIX capture checklist

Windows PIX installation is documented in [setup](setup.md#pix). No `.wpix`
capture or console partner tooling access is claimed. GPU timers and dispatch
counters in the actual reports are the measured telemetry available here.

## Platform constraints

The research app uses DirectX 12/HLSL. It does not claim a DirectML optimizer,
CUDA, GDKX entitlements or ID@Xbox partner access. The app-class memory planning
budget remains about 1 GB; Game classification is not assumed. Console acceptance
used the non-debug Release package; debugger memory is not the acceptance oracle.
Real suspension writes a cancel marker and publishes a verified checkpoint.

Primary platform references remain in [resource limits](platform/uwp-resources.md),
[public GDK versus GDKX](platform/gdk-vs-gdkx.md), [Dev Mode](platform/dev-mode.md)
and [DirectX compute](platform/dx12-hlsl-compute.md). The factual constraints in
[platform blockers](platform/blockers-fase6-validation.md) are unchanged by a
successful small E0 run.

## Next execution

Follow the companion's accepted ADR 0011 and sequential campaign. Bind the current
acceptance and benchmark, retain one GPU job at a time, and freeze the final ten
artifact hashes before the exclusive held-out test reservation. Publish actual
costs and failures with the final result. [Roadmap](../ROADMAP.md),
[results](results.md), [execution plan](execution-plan.md#fase-6--series-sx-dev-mode-validation).
