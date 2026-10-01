# E0 job protocol

Reference for the file protocol between the companion runner and the UWP worker
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

Stopping a companion campaign cleanly is described in [runbook.md](runbook.md#stop-a-campaign).
