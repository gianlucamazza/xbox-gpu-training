# E0 runbook

How to build, install, accept and run an E0 package on a Series S in Dev Mode.
Companion commands run from a clone of the FloppyLM repository (`gianlucamazza/floppylm`) with
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

The existing development certificate signs the package; the Device Portal TLS
certificate is a separate trust boundary. Use its existing trusted SHA-256 pin,
also accepted through `OPENAPPX_DEVICE_PIN`. Do not replace a mismatched pin automatically.
See the companion runbook for private connection settings.

## 3. Install and start

```bash
export OPENAPPX_DEVICE_PASSWORD=…        # Device Portal password, never on the command line
openappx deploy --device https://<console-ip>:11443 --user <user> --pin-sha256 <device-cert-sha256> --package XgpuE0.msix
openappx deploy --device https://<console-ip>:11443 --user <user> --pin-sha256 <device-cert-sha256> \
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

Acceptance, benchmark, worker probes, runner recovery, real suspension and campaign
operations are owned by the companion [Xbox runbook](https://github.com/gianlucamazza/floppylm/blob/main/docs/operations/xbox-e0.md).
Run that procedure with the exact installed package and commit, only when no scientific
job owns the GPU queue. Its acceptance scripts take an exclusive **output directory**,
not a JSON filename. An execution-preserving release also needs the
[bit-identity proof](engine.md#bit-identity-rule).

Commit package-bound hardware proofs here under `docs/evidence/` and reference them
from FloppyLM's companion record. Never resume a scientific campaign with a different
package implicitly. The companion owns source freezing, recovery and final-test reservation.

## PIX

PIX on Windows can be installed ([diagnostic/setup.md](../diagnostic/setup.md#pix)),
but no `.wpix` capture exists, on Windows or console. The E0 reports' GPU
timestamps (`gpu_seconds`, `dispatches`) are the available telemetry.
