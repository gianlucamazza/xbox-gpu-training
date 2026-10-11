# Console process lifecycle

Portable host and app settings for starting, watching and stopping the UWP
worker. Nothing here is bound to one console, package version or campaign.
The 2026-10-11 orphan is recorded in
[the RCA](../rca/2026-10-11-xgpue0-orphan-gpu.md); this page is the config
reference.

Host commands: [`scripts/e0_console_lifecycle.py`](../../scripts/e0_console_lifecycle.py).
On-console overrides: `LocalState/lifecycle.json`.

Precedence on the host: documented default < credentials/settings file <
environment < CLI flag. The file path itself comes from `--credentials` or
`XGPU_PORTAL_CREDENTIALS`. No default host, user or path is compiled in.
Integer settings accept float-looking values (`20.0` → 20) so companion
env files can pass floats.

## Config reference

| Setting | Default | CLI | Environment | Console `lifecycle.json` |
| --- | ---: | --- | --- | --- |
| Idle exit | 1800 s | `--idle-exit-seconds` | `XGPU_IDLE_EXIT_SECONDS` | `idle_exit_seconds` |
| Host heartbeat interval | 30 s | `--heartbeat-interval-seconds` | `XGPU_HEARTBEAT_INTERVAL_SECONDS` | — (host only) |
| Heartbeat stale | 1800 s | `--heartbeat-stale-seconds` | `XGPU_HEARTBEAT_STALE_SECONDS` | `heartbeat_stale_seconds` |
| Poll interval | 1 s | `--poll-interval-seconds` | `XGPU_POLL_INTERVAL_SECONDS` | `poll_interval_seconds` |
| GPU pin threshold | 90 % | `--gpu-threshold-percent` | `XGPU_GPU_THRESHOLD_PERCENT` | — |
| GPU sample count | 3 | `--gpu-sample-count` | `XGPU_GPU_SAMPLE_COUNT` | — |
| GPU sample interval | 1 s | `--gpu-sample-interval-seconds` | `XGPU_GPU_SAMPLE_INTERVAL_SECONDS` | — |
| GPU engine filter | all engines | `--gpu-engine` (repeat) | `XGPU_GPU_ENGINES` (comma indices) | — |
| GPU engine exclude | none | `--gpu-engine-exclude` (repeat) | `XGPU_GPU_ENGINE_EXCLUDE` | — |
| Idle CPU threshold | 1 % | `--idle-cpu-percent` | `XGPU_IDLE_CPU_PERCENT` | — |
| Teardown wait | 60 s | `--teardown-wait-seconds` | `XGPU_TEARDOWN_WAIT_SECONDS` | — |
| Process image names | `xgpue0.exe` | `--process-image` (repeat) | `XGPU_PROCESS_IMAGES` | — |
| Package full name | (none) | `--package` | `XGPU_E0_PACKAGE` | — |
| Package family | (none) | `--package-family` | `XGPU_PACKAGE_FAMILY` | — |
| Terminate by | `package`, `pid` | `--terminate-by` | `XGPU_TERMINATE_BY` | — |
| Portal URL | (none) | `--portal-url` | `XGPU_PORTAL_URL` or `UWP_DEVICE_URL` | — |
| Portal port | 11443 | `--portal-port` | `XGPU_PORTAL_PORT` | — |
| TLS verify | `pin` | `--tls-verify pin\|none\|system` | `XGPU_PORTAL_TLS_VERIFY` | — |
| TLS pin | (none) | `--tls-pin` | `XGPU_PORTAL_TLS_PIN` or `OPENAPPX_DEVICE_PIN` | — |
| Portal user | (none) | — | `XGPU_PORTAL_USER` or `UWP_DEVICE_USER` | — |
| Portal password | (none) | — | `XGPU_PORTAL_PASSWORD` or `OPENAPPX_DEVICE_PASSWORD` | — |
| Credentials file | (none) | `--credentials` | `XGPU_PORTAL_CREDENTIALS` | — |
| Instance document name | `instance.json` | `--instance-name` | `XGPU_INSTANCE_NAME` | — (app writes `LocalState/instance.json`) |
| Orphan strict | off | `--orphan-strict` / `--no-orphan-strict` | `XGPU_ORPHAN_STRICT` | — |

Device Portal user and password are never accepted on the command line.

`tls_verify=pin` requires a SHA-256 pin. `none` skips pin and hostname checks.
`system` uses the platform CA store and hostname check (self-signed portals
fail that mode unless the operator has installed the console certificate).

`poll_interval_seconds` keeps millisecond precision in
`LocalState/lifecycle.json` (0.5 → 500 ms) and is clamped to a 50 ms minimum
so a truncated value cannot busy-loop.

## Idle / orphan check

Orphan detection uses this app instance, not GPU utilisation and not
historical inbox files:

- `job_active` comes from the live `worker.json` (`state` / `active_job`)
  whenever the process is present and that document has a state. Finished
  `.claimed` files are ignored in that case — fixtures, kernels and
  optimizer write `inbox/<id>.actual.json` rather than
  `inbox/results/<id>/`, so a leftover `.claimed` is not in-flight.
  `.claimed` is only a fallback when `worker.json` is missing. A
  completion artefact (`results/<id>/status.json`, `<id>.actual.json`,
  `<id>.rejected.json`, or a `.cancel` the worker has processed) marks
  the job done. A `.ready` file is current queue, not history.
- App start and instance identity come from `LocalState/instance.json`
  (`schema` `xgpu.instance.v1`: `instance_id`, `started_at` UTC ISO-8601,
  `pid`). The worker writes that file at startup. It is not part of
  `floppylm.worker.v1` (`additionalProperties` is false; do not add
  `started_at` to `worker.json`). If `instance.json` is missing, a Device
  Portal process `CreateTime` / `StartTime` is used when the listing
  publishes one. Some consoles publish neither field.
- Idleness is `now - max(app start, last job end in this instance, last
  host heartbeat in this instance)`. Results from before this instance
  do not count. `host.heartbeat` carries `instance_id`. A heartbeat
  whose `instance_id` differs from `instance.json`, or that was written
  before `started_at`, is ignored — a leftover file from an earlier
  process does not bind to the current one.
- A ready app younger than `idle_exit_seconds` is never an orphan. The
  host before-check may run before the first heartbeat.
- When the start time is unknown (old build, missing `instance.json`,
  and no process start field) the verdict is never orphan:
  `orphan: null`, `reason: start_time_unknown`. The before-check is
  `ok: true` and emits a warning. `--orphan-strict` /
  `XGPU_ORPHAN_STRICT=1` refuses instead (default off).
- With no process, `worker_state` and `app_age_seconds` are `null`
  rather than leftover values from a stale `worker.json` /
  `instance.json`. The in-app idle timer and the host tool both ignore
  a `host.heartbeat` whose `instance_id` differs from `instance.json`
  or that predates `started_at`. `teardown` deletes `host.heartbeat`
  after the process is gone.

No process plus stale history is `job_active=false`, `orphan=false`,
`process_present=false` — OK to launch. `idle-gpu --when after` fails
only if the process is still listed.

GPU samples remain advisory: every adapter's `EnginesUtilization` is averaged
over `gpu_sample_count` reads and reported as `gpu_advisory` with
`decisive: false`. `--gpu-engine-exclude` omits engines from that report.
Nothing is hardcoded to one Xbox engine. On Xbox the foreground engine often
reads ~100% for any idle foreground app; do not treat that as an orphan.

## Teardown

`prepare` lists `inbox` and deletes `inbox/shutdown` only if that name is
present. Device Portal accepts DELETE of a missing file; `cleared` is
reported only when the marker was actually there. After the process is
gone, `teardown` also deletes `host.heartbeat` so the next instance
does not inherit a leftover file time.

`teardown` uploads `inbox/shutdown`, waits, then `DELETE`s
`/api/taskmanager/app?package=<base64>` and/or `/api/taskmanager/process?pid=…`.
A package `DELETE` failure does not skip the pid fallback. `terminated` is
true only when that kill path ran; a process that exited on the shutdown
marker reports `terminated: false`. After the process is gone, teardown
clears a leftover shutdown marker.

The worker deletes `inbox/shutdown` when it consumes it, before `ExitProcess`.

Mutating calls discover any cookie whose name contains `csrf` or `xsrf` and
echo its value as `X-CSRF-Token`. HTTP 400/403 refresh the cookie and retry
once; a second 400/403 is an auth error.

## On-console idle

With no job, the worker honors `inbox/shutdown` or the idle/heartbeat
deadlines from `lifecycle.json`. Independently, the dashboard stops its
`DispatcherTimer` and releases `DisplayRequest`
([ADR 0010](../adr/0010-idle-compositor-release.md)). On-console evidence
showed that release does not drop the foreground GPU engine; process exit
does.
