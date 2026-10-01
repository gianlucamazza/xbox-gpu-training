# Memory budget (Fase 4)

Planning budgets and the host streaming contract. These are **not** measured Xbox Series S|X working-set numbers.

Public source of truth for the UWP caps: [docs/platform/uwp-resources.md](../platform/uwp-resources.md) (Microsoft Learn, Xbox One and Series X|S). This note is the **how we stream against those caps** page.

## Designations (do not mix)

| Designation | Foreground planning budget | This repo |
| --- | --- | --- |
| UWP **App** (AppContainer) | **~1 GB** (1024 MiB) | Default planning budget. `--budget-mb 1024`. |
| Xbox Live **Creators Program** **Game** | **~5 GB** (5120 MiB) | Documented as the **other** designation only. |
| Background app | **≤128 MB** | Out of scope for the trainer host. |

`--budget-mb` is in **MiB** (`1 MiB = 1048576` bytes). `1024` MiB is the App planning figure. `5120` MiB is the Creators Game figure.

**Do not assume Game designation in an App package.** Whether an unpublished research package can use the Creators Game class is uncertain. Fase 4 plans as an **App**.

## Debugger can mask OOM

Microsoft: when the app or game runs from the Visual Studio debugger, these memory constraints **do not apply**. The limit applies when **not** running in debugging mode.

The **non-debug** package is the gate. A debugger session that does not OOM is **not** evidence that the App 1 GB or Creators 5 GB cap holds. Aligns with [uwp-resources.md](../platform/uwp-resources.md).

## Streaming model

Diagnostic-lane design (ADR 0001): **fp32 master weights** stay conceptually in system RAM. The full master is **not** assumed to fit in the App ~1 GB budget. **Chunk streaming** plus a **2-slot double buffer** move working tiles.

```
  logical master (may be >> 1 GB; never allocated as one tensor)
           |
           |  FillChunk(slot[i % 2])
           v
  host double buffer (2 x chunk_bytes)
           |
           |  optional CopyBufferRegion (2 UPLOAD + 2 DEFAULT)
           v
  GPU tiles (same chunk_bytes; not the full master)
```

Fase 4 adds **no new HLSL kernel**. GPU work, when a D3D12 device exists, is a ping-pong `CopyBufferRegion` of the two tiles. Hello-compute / matmul / FLP2 / STE shaders are unchanged.

## Stress fixture

| Piece | Path |
| --- | --- |
| Fixture | [`benchmarks/fixtures/stream_stress.json`](../../benchmarks/fixtures/stream_stress.json) |
| Host | `xbox_gpu_host --stream-stress --budget-mb 1024` |
| Code | [`src/cpp/stream_buffer.*`](../../src/cpp/stream_buffer.h), [`src/cpp/stream_stress.*`](../../src/cpp/stream_stress.h) |

Default fixture (override with `--chunk-mb` / `--logical-mb` if needed):

| Field | Default |
| --- | --- |
| `logical_bytes` | 2 GiB (`2147483648`) — larger than the App budget, so streaming is required |
| `chunk_bytes` | 16 MiB (`16777216`) |
| `passes` | 1 |
| `planning_budget_mb` | 1024 (App) |
| `game_designation_budget_mb` | 5120 (documented, not used) |

The host **never** allocates `logical_bytes` as one allocation. It generates each chunk into one of two slots, checksums it (so pages are touched), and optionally copies that slot to a DEFAULT GPU buffer.

## What the host reports

Honest first line:

- `STATUS: stream-stress ok` — host stream finished; peak working-set ≤ planning budget; no GPU copy (or GPU not required for this line)
- `STATUS: stream-stress dispatched` — same, plus GPU double-buffer copies on a D3D12 device
- `STATUS: stream-stress budget breach` — finished without OOM, but peak working-set **exceeded** `--budget-mb` (measured; not invented)
- `FAILED: stream-stress OOM` — `std::bad_alloc` or GPU `CreateCommittedResource` failure treated as a real error
- GPU-only blocker is printed as `gpu_double_buffer: BLOCKED: no D3D12 device` — not a GPU success

Always printed:

- `peak_working_set_mb` / `peak_working_set_bytes` when the OS sampler works
- `console_appcontainer: UNVALIDATED` on this desktop / CI host
- Game ~5 GB as documented, not assumed

Working-set source:

- Linux: `/proc/self/status` `VmRSS` + `VmHWM`
- Windows: `GetProcessMemoryInfo` `WorkingSetSize` / `PeakWorkingSetSize`

That is **desktop (or CI) process RAM**, not an Xbox AppContainer measurement.

## Console budget is unvalidated here

The Fase 4 host ran on desktop machines only, so it:

- measures peak working-set on the host that actually ran
- **marks console App ~1 GB / Game ~5 GB as UNVALIDATED**
- does **not** invent Series S|X numbers

This stress fixture was never run on a console. The E0 trainer does not stream: its whole training state stays resident and its measured peak app memory is in [../status.md](../status.md).

## VRAM

Xbox App Mode is **not** full title GPU ([dx12-hlsl-compute.md](../platform/dx12-hlsl-compute.md)). The stream keeps at most **two** GPU tiles of `chunk_bytes` plus matching UPLOAD heaps. It does not park the logical 2 GiB master in VRAM.

## Related

- [uwp-resources.md](../platform/uwp-resources.md) — Learn caps; debugger mask
- [ADR 0001](../adr/0001-architecture.md) — do not assume the master fits in App RAM
- [setup.md](setup.md) — how to run the host
