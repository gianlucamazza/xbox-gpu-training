# E0 job protocol

Implementation map of the file protocol owned by the [FloppyLM schemas](https://github.com/gianlucamazza/floppylm/tree/main/schemas)
and vendored at [PIN.json](../../contracts/floppylm/PIN.json). Describes the exchange between the companion runner and the UWP worker
(`uwp/App.cpp`, `src/cpp/e0/job.cpp`). All paths are under the package
`LocalState` folder. All JSON is written with `atomic_json` (temporary file, then
replace; retried on transient Device Portal sharing violations).

## Device record

On start the worker writes `LocalState/device.json`:

| Key | Value |
| --- | --- |
| `schema` | `floppylm.device.v1` |
| `state` | `ready`, or `failed` with `error` if the GPU kernel could not start |
| `hardware_gpu` | `true` only for a hardware adapter (WARP is rejected) |
| `adapter` | DXGI adapter name (Series S: `SraKmd_arden`) |
| `package` | installed package full name |
| `commit` | source commit baked in at build (`XGPU_COMMIT`) |
| `capabilities` | config values `Config` accepts (`e0::capabilities()`): vocab, embedding and core formats, MLPs, scale policies, delta range; the host refuses other configs before upload |

Acceptance must match `package` and `commit` against the expected build.

## Inbox

```
LocalState/inbox/
  <id>.job.json        job or fixture description (uploaded first)
  <asset files>        initialization, corpus, indices, resume checkpoint
  <id>.ready           marker uploaded last; worker renames it to <id>.claimed
  <id>.cancel          optional; requests interruption
  <id>.actual.json     fixture result (fixture schemas only)
  results/<id>/        training job outputs
```

The worker polls once per second and runs one job at a time. A job's `job_id`
must equal its file stem.

## Schemas dispatched

| `schema` | Handler | Output |
| --- | --- | --- |
| `floppylm.e0.fixture.v1` | `fixture_report` — model fixture on the GPU | `<id>.actual.json` |
| `floppylm.e0.kernels.v1` | `kernel_fixture_report` — per-operation cases | `<id>.actual.json` (`floppylm.e0.kernels.result.v1`) |
| `floppylm.e0.optimizer.v1` | `optimizer_fixture_report` — AdamW on identical inputs | `<id>.actual.json` |
| `floppylm.e0.job.v1` | `run_job` — training | `results/<id>/…` |

## Training job

Required fields: `job_id` (`[A-Za-z0-9_-]+`), `config`, `spec`, and asset
descriptors `initialization`, `data`, `indices` (plus `resume` when resuming).
An asset descriptor is `{path, bytes, sha256}` with a relative path inside the
inbox; absolute paths, `..`, and reparse points are rejected. A descriptor may list
`chunks` to be assembled and re-hashed on the console.

`spec` must declare `branches: 3`, `warmup_frac: 0.02`, `cooldown_frac: 0.1`,
positive `batch`, `tokens`, `lr`, and non-negative `wd`. The index plan must hold
exactly `4·T·batch` little-endian u64 offsets, where `T = tokens / (batch·ctx)`.

Outputs in `results/<id>/`:

| File | Content |
| --- | --- |
| `status.json` | `floppylm.e0.result.v1`: `state` (`running`, `interrupted`, `completed`, `failed`), `trunk_step`, `last_loss`, `branches[]`, `dispatches`, `gpu_seconds`, `transfer_bytes`, `peak_memory_bytes`, `wall_seconds`, `checkpoint`, `error`, and the executed `schedule` (`T`, `warmup`, `tokens_per_step`, `ends`, `cooldown_starts`) with `phase` (`trunk` or `cooldown`; during a cooldown also `cooldown_end` and `cooldown_step`). Rewritten every 64 steps of the trunk and of each cooldown. |
| `checkpoint.json` | `floppylm.checkpoint.v1`: tensors, both AdamW moments, step, stream position, bound job fields and initialization hash. Written every 64 trunk steps and on interruption. |
| `branch-<end>.json` | `floppylm.e0.weights.v1` master weights at the end of each cooldown branch (`end` = T, 2T, 4T) |

A new job refuses an existing result directory. Resuming requires an explicit
`resume` asset; the worker checks the bound fields and that previously reported
branch artifacts still verify.

## Interruption

- **Host request.** The companion uploads `<id>.cancel`. At the next trunk step the
  worker writes a checkpoint and `state: interrupted`. During a cooldown branch it
  stops without a new checkpoint (the trunk checkpoint before the branch stands).
- **Suspension.** On `Suspending` the app writes `<id>.cancel` containing `suspend`
  for the active job and holds the deferral up to 4 s for the job to stop.
- **Stay-alive (best effort).** The dashboard holds a `DisplayRequest` for the
  process lifetime so idle gaps do not drop the TV stay-awake request
  ([ADR 0007](../adr/0007-uwp-stay-alive.md), [ADR 0008](../adr/0008-drop-extended-execution.md)).
  The app does not request `ExtendedExecutionSession`: a grant on 0.1.0.84
  let a lifecycle job run to completion under Dev Home with no `.cancel`.
  Cooperative `Suspending` remains the lifecycle contract.

Stopping a companion campaign cleanly is described in [FloppyLM Xbox runbook](https://github.com/gianlucamazza/floppylm/blob/main/docs/operations/xbox-e0.md#recover).

## Runtime ownership and explicit recovery

The scientific contract and operational policy remain owned by
[FloppyLM ADR 0017](https://github.com/gianlucamazza/floppylm/blob/main/docs/adr/0017-runtime-liveness.md).
The backend imports its worker and claim schemas from the pinned contracts
copy. Neither runtime faults nor lifecycle reconciliation change weights,
optimizer formulas, schedules or the final-test reservation.

Before readiness, the worker acquires an exclusive process-lifetime `worker.lock`,
creates a random instance ID and reconciles abandoned claims. `worker.json` is
atomically published every five seconds by an independent thread. Heartbeat ticks
only increment `heartbeat_seq`; completed GPU fences and optimizer steps update
`progress.sequence`. The snapshot distinguishes trunk/cooldown steps and records
the last completed operation and fence. A heartbeat therefore proves process
liveness, not training progress. The on-console dashboard reads `worker.json`
every second and shows that line; thirty seconds without a rewrite is
unreachable.

After claiming `<id>.ready`, the worker persists the exact submitted payload in
`<id>.owner.json` and executes `<id>.owned.job.json`, which contains the same bytes.
An immutable hash-addressed owner archive preserves the previous submission's
binding across an explicit resume publication. A pending upload cannot rewrite
that old binding. On startup, reconciliation checks ownership, package/commit,
submission hash, checkpoint descriptor and model/stream identity, plus all
published branch hashes. Verified abandoned work becomes `interrupted`; failed
verification is recorded as an integrity failure. Completed results are untouched.
Leftover fixture, kernel and optimizer claims that already published
`<id>.actual.json` are skipped; they never receive a fabricated
`results/<id>/status.json`. One malformed `.claimed` stem is skipped and does
not abort the rest of the pass. A leftover `.ready` beside a reconciled claim
is quarantined; startup never executes a pending replacement automatically.
Pre-admission rejection preserves
existing status bytes and writes `<id>.rejected.json` with the submitted hash.
Nothing is automatically enqueued. An explicit resume requires verified interrupted
state and an unchanged scientific recipe; published branches are not recomputed.

A new job writes the step-0 checkpoint before the first `running` status, so a
GPU fault before the first 64-step rewrite still has a verifiable restore
point.

Every submitted GPU fence has a 600-second deadline, polled at 250 ms. Wait results,
completed fence values and device-removal status are checked before accepting
completion. A runtime fault records native error details, requested/completed fence
values and elapsed time. The last atomically published checkpoint survives; partial
optimizer state is never checkpointed in a fault handler. The worker and device
remain permanently failed until the app is explicitly restarted. In-flight GPU
resources and synchronization objects are deliberately retained until process exit;
they are not returned to reusable pools or flushed during destruction.

Functional kernel fixtures may request `runtime_fault_probe` with `kind` equal to
`gpu_wait_timeout`, `gpu_wait_failed` or `gpu_device_removed`. These hooks exercise
the same fault/quarantine path; simulated timeout does not wait ten minutes.
Scientific training rejects this field. Hooks are inactive by default and are not
exposed in the dashboard. Portable tests exercise the synchronization policy and
checkpoint/claim safety; Windows/UWP compilation and exact-package Xbox fault
qualification remain separate evidence gates after the active E0 campaign closes.

The wait handling follows Microsoft's contracts for
[GetCompletedValue](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12fence-getcompletedvalue)
and [WaitForSingleObjectEx](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobjectex).
