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

Device Portal user and password are never accepted on the command line.

`tls_verify=pin` requires a SHA-256 pin. `none` skips pin and hostname checks.
`system` uses the platform CA store and hostname check (self-signed portals
fail that mode unless the operator has installed the console certificate).

`poll_interval_seconds` keeps millisecond precision in
`LocalState/lifecycle.json` (0.5 → 500 ms) and is clamped to a 50 ms minimum
so a truncated value cannot busy-loop.

## Idle / orphan check

Orphan detection uses LocalState, not GPU utilisation:

- no job running or queued (worker/device `ready`, no `.ready` / `.claimed`)
- host heartbeat missing or older than `heartbeat_stale_seconds`
- time since the last job ended (`inbox/results/*/status.json` mtime) at or
  above `idle_exit_seconds`

A freshly launched idle/ready app has no last-job age and is **OK to reuse**.
`idle-gpu --when before` fails only on `job_active` or `orphan_stale_idle`.
`idle-gpu --when after` fails only if the process is still listed.

GPU samples remain advisory: every adapter's `EnginesUtilization` is averaged
over `gpu_sample_count` reads and reported as `gpu_advisory` with
`decisive: false`. `--gpu-engine-exclude` omits engines from that report.
Nothing is hardcoded to one Xbox engine. On Xbox the foreground engine often
reads ~100% for any idle foreground app; do not treat that as an orphan.

## Teardown

`prepare` deletes a leftover `inbox/shutdown` before launch (a consumed marker
that was not removed used to quit the next process within seconds).

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
