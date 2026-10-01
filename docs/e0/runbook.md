# E0 runbook

How to build, install, accept and run an E0 package on a Series S in Dev Mode.
Companion commands run from the FloppyLM repository (`floppy_4mb`) with
`PYTHONPATH=src`. Never commit Device Portal credentials or certificates.

## 1. Build

CI is the source of every installed package: workflow
[`e0-uwp.yml`](../../.github/workflows/e0-uwp.yml) runs
[`scripts/build-e0-uwp.ps1`](../../scripts/build-e0-uwp.ps1) on `windows-latest` and
uploads artifact `e0-uwp-<sha>` (unsigned Release package and AppX layout).

The script restores CppWinRT, compiles `e0_tensor.hlsl` with `dxc -Gis`, stamps the
package identity and version into the manifest (restoring the original afterwards)
and builds `uwp/XgpuE0.vcxproj` for x64 Release with the source commit baked in.

| Parameter | Environment / CI variable | Default |
| --- | --- | --- |
| `-IdentityName` | `XGPU_E0_IDENTITY_NAME` | `GianlucaMazza.XgpuE0` |
| `-Publisher` | `XGPU_E0_PUBLISHER` | `CN=uwp-crossbuild-dev` (must equal the signing certificate subject) |
| `-PublisherDisplayName` | `XGPU_E0_PUBLISHER_DISPLAY_NAME` | `Gianluca Mazza` |
| `-Version` | `XGPU_E0_VERSION` | `0.1.0.<GITHUB_RUN_NUMBER>`, or `0.1.0.0` locally |
| `-Commit` | `GITHUB_SHA` | `unknown` |

In CI the identity variables come from optional repository variables; unset keeps
the defaults. Changing the name or publisher installs a **separate app** with an
empty `LocalState` (corpus re-upload, no in-place upgrade), and the companion must
be pointed at it with `XGPU_E0_PACKAGE`. Do not edit the manifest by hand. A local
build has no CI lineage; use it only for build checks.

## 2. Sign

```bash
gh run download <run-id> -n e0-uwp-<sha> -D artifact/
openappx unpack --package artifact/<package>.msix --out ci-layout/
openappx pack --root ci-layout/ --out XgpuE0.msix
openappx sign --package XgpuE0.msix --pfx <dev-cert.pfx>
openappx unpack --package XgpuE0.msix --out signed-layout/
diff -r ci-layout/ signed-layout/   # only signature/block-map metadata may differ
```

Record the unsigned and signed package SHA-256 and every payload hash in
`package-lineage.json` of the evidence directory ([acceptance.md](acceptance.md)).

## 3. Install and start

```bash
export OPENAPPX_DEVICE_PASSWORD=…        # Device Portal password, never on the command line
openappx deploy --device https://<console-ip>:11443 --user <user> --insecure --package XgpuE0.msix
openappx deploy --device https://<console-ip>:11443 --user <user> --insecure \
  --start <PackageFullName> --app-id App
```

An in-place upgrade keeps `LocalState`, including the uploaded corpus (~2 GB).
Keep the app in the foreground while training. The worker writes `device.json`
([job-protocol.md](job-protocol.md#device-record)); check that `package` and
`commit` match the build.

The companion reads `XBOX_IP`, `XBOX_USER`, `XBOX_PASS` from the environment (or its
local env file). When several E0 versions are installed, set
`XGPU_E0_PACKAGE=<PackageFullName>` explicitly.

## 4. Accept

A package may train only after its own acceptance. Run in order, binding each to
the acceptance output:

```bash
python experiments/xbox_acceptance.py --out <dir>/acceptance.json
python experiments/xbox_worker_acceptance.py --out <dir>/worker.json
python experiments/xbox_recovery_acceptance.py --out <dir>/runner-recovery.json --acceptance <dir>/acceptance.json
python experiments/xbox_lifecycle_acceptance.py --out <dir>/lifecycle.json --acceptance <dir>/acceptance.json
python experiments/xbox_benchmark.py --out <dir>/throughput.json --acceptance <dir>/acceptance.json
```

The lifecycle step needs a real suspension through Dev Home. For an engine change,
also produce the bit-identity proof ([engine.md](engine.md#bit-identity-rule)).
Copy the results into `docs/evidence/e0-<date>[-<tag>]/` with a `notes.md`, then
update [status.md](../status.md).

## 5. Run a campaign

The campaign is owned by the companion (`experiments/e0_campaign.py`, one trial at a
time via `experiments/e0_v2.py`), following its accepted ADRs. Check progress without
mutating jobs:

```bash
python scripts/e0_status.py --campaign <campaign-dir> --xbox
```

## Stop a campaign

Send `SIGTERM` to the **trial** process (`e0_v2.py`) only. Its handler uploads the
console `<id>.cancel` marker; the worker checkpoints and reports `interrupted`, and
the campaign records itself as stopped.

Never signal the campaign process or the process group: `SIGINT` on the group makes
the trial's subprocess handling kill the child after 0.25 s and orphans the console
job.

## 6. Recover

- **Interrupted job.** Resume through the companion runner with an explicit `resume`
  asset (the checkpoint from `status.json`). The worker refuses a silent restart.
- **Failed job.** `results/<id>/status.json` carries `state: failed` and `error`;
  diagnose before resubmitting. Thresholds are never relaxed after a failure (ADR 0004).
- **Worker failure at start.** `device.json` reports `state: failed`; typical causes are
  a missing shader asset or no hardware adapter.

## PIX

PIX on Windows can be installed ([diagnostic/setup.md](../diagnostic/setup.md#pix)),
but no `.wpix` capture exists, on Windows or console. The E0 reports' GPU
timestamps (`gpu_seconds`, `dispatches`) are the available telemetry.
