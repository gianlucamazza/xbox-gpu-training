# Fase 6 — Series S|X Dev Mode validation

Status (2026-10-01, [PR #17](https://github.com/gianlucamazza/xbox-gpu-training/pull/17), `aec1a2a`): **Series S E0 functional execution measured**. Scientific quality, Series X, matched CPU comparison, PIX capture, and the historical Win32 host workloads remain **UNMEASURED**. This page does **not** invent tok/s beyond the recorded E0 trial. It does **not** claim **GDKX** or **ID@Xbox**.

[PR #15](https://github.com/gianlucamazza/xbox-gpu-training/pull/15) (`66224e05`) shipped this page as **`BLOCKED: no console`**. That was honest for the Win32 host lane at that commit. #17 later landed a separate x64 UWP E0 backend with Device Portal evidence. Do **not** overwrite that evidence with empty placeholders. Do **not** copy E0 numbers onto historical host rows.

Honest Fase 7 write-up: [docs/results.md](results.md).

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

The original Win32 host (`xbox_gpu_host`) remains a separate desktop diagnostic
lane. Its results are **not** console AppContainer measurements.

## Results table — measured E0

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

Representative synthetic E0 (sourced): 147456 training tokens in 153.030679 seconds,
963.571 token/s and 91418624 bytes peak app memory. Extrapolation excludes corpus
transfer and Python evaluation. [throughput.json](evidence/e0-20261001/throughput.json).

## Results table — historical Win32 host workloads

Schema intent: `xbox-gpu-training.benchmark.console.v1`. These rows are the Fase 0–5
desktop smokes. #17’s E0 package does **not** fill them. Empty metric cells stay empty.

| sku | workload | tok/s | quality | cpu_tok/s | pix | status | note |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Series S | hello_compute | | | | | `UNMEASURED` | not the E0 UWP package |
| Series S | matmul FP32/FP16 | | | | | `UNMEASURED` | not the E0 UWP package |
| Series S | FLP2 forward fixture | | | | | `UNMEASURED` | not the E0 UWP package |
| Series S | stream-stress App ~1 GB | | | | | `UNMEASURED` | AppContainer unvalidated for this host |
| Series S | qat-smoke N=16 | | | | | `UNMEASURED` | no quality curve |
| Series X | hello_compute | | | | | `UNMEASURED` | no Series X measurement |
| Series X | matmul FP32/FP16 | | | | | `UNMEASURED` | no Series X measurement |
| Series X | FLP2 forward fixture | | | | | `UNMEASURED` | no Series X measurement |
| Series X | stream-stress App ~1 GB | | | | | `UNMEASURED` | no Series X measurement |
| Series X | qat-smoke N=16 | | | | | `UNMEASURED` | no Series X measurement |
| CPU vs console | any of the above | | | | | not filled | both sides not measured |

Public marketing TFLOPS / GDDR6 sizes in [series-s-vs-x.md](platform/series-s-vs-x.md) are **SKU copy**, not rows of this table.

Windows Fase 1–5 host numbers (portable GEMM CSV, Linux `VmHWM`, QAT smoke loss pairs) are **not** console results and are **not** copied here as Series benches.

## PIX capture checklist

Windows PIX installation is documented in [setup](setup.md#pix). No `.wpix`
capture or console partner tooling access is claimed. GPU timers and dispatch
counters in the actual E0 reports are the measured telemetry available here.

| Step | Done? |
| --- | --- |
| PIX on Windows install documented | yes — [setup.md](setup.md#pix) |
| Windows DX12 capture of one compute dispatch (`hello_compute` / matmul / FLP2) | **no** |
| Attach a `.wpix` / timing table | **no** — none exists |
| Console / Dev Mode PIX capture of one compute shader dispatch | **UNMEASURED** |
| Treat WARP / Basic Render Driver as Series S\|X | **never** |

## Platform constraints

The research app uses DirectX 12/HLSL. It does not claim a DirectML optimizer,
CUDA, GDKX entitlements or ID@Xbox partner access. The app-class memory planning
budget remains about 1 GB; Game classification is not assumed. Console acceptance
used the non-debug Release package; debugger memory is not the acceptance oracle.
Real suspension writes a cancel marker and publishes a verified checkpoint.

The factual constraints in [platform blockers](platform/blockers-fase6-validation.md)
are unchanged by a successful small E0 run:

1. **No primary Microsoft claim of on-console LLM / GPU training via DirectML or ORT.** This path is DirectX 12 / HLSL. Detail: [directml-scope.md](platform/directml-scope.md).
2. **UWP memory caps; debugger can mask OOM.** App **~1 GB**; Creators Game **~5 GB**. Non-debug package is the gate. Detail: [uwp-resources.md](platform/uwp-resources.md).
3. **Public GDK is Windows-only; full console is GDKX / ID@Xbox.** Not claimed. Detail: [gdk-vs-gdkx.md](platform/gdk-vs-gdkx.md).
4. **Dev Mode is UWP develop/test only — not GDKX entitlements.** Detail: [dev-mode.md](platform/dev-mode.md).

Primary platform references remain in [resource limits](platform/uwp-resources.md),
[public GDK versus GDKX](platform/gdk-vs-gdkx.md), [Dev Mode](platform/dev-mode.md)
and [DirectX compute](platform/dx12-hlsl-compute.md).

## Next execution

The first scientific campaign (package `0.1.0.24`) was stopped cleanly on 2026-10-01 at trunk step 455 of its first trial: its console job measured 329 GPU seconds in 3848 wall seconds. It is superseded by the GPU-resident E0.1 execution engine, which needs a new acceptance before a new campaign.


Follow the companion's accepted ADR 0011 and sequential campaign. Bind the current
acceptance and benchmark, retain one GPU job at a time, and freeze the final ten
artifact hashes before the exclusive held-out test reservation. Publish actual
costs and failures with the final result. Leave every unmeasured cell empty.

[Roadmap](../ROADMAP.md), [results](results.md),
[execution plan](execution-plan.md#fase-6--series-sx-dev-mode-validation).
