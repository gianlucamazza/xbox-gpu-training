# Dashboard UI package — 2026-10-01

Purpose: **functional**. This record covers the redesigned on-console dashboard (PR #31). It certifies
no language-model quality and binds no scientific campaign.

## Artifact and deployment

CI [36921727698](https://github.com/gianlucamazza/xbox-gpu-training/actions/runs/36921727698) built the
pull-request merge commit `f8c9f69` (PR head `0965acb` on main `4af66cb`; identical tree to `0965acb`).
Installed package: `GianlucaMazza.XgpuE0_0.1.0.66_x64__g0p5dcfz4t9z4`, signed with the existing development
certificate and deployed with the pinned Device Portal certificate. Unsigned and signed SHA-256 and the payload
hashes are in [package-lineage.json](package-lineage.json); the shader `e0_tensor.cso` is unchanged.

Installing it replaced `0.1.0.56`, so FloppyLM campaign `e0-20261001T163456Z-fdab67` (trial `-001`
interrupted at trunk step 1583) can no longer resume on its bound package. That was an explicit owner decision;
resuming it needs an explicit package decision in FloppyLM.

## Hardware proofs

- [acceptance.json](acceptance.json): 36 fixtures, kernels, AdamW and exact resume passed on this package.
- [bit-identity.json](bit-identity.json): 38/38 acceptance payloads, branch weights, checkpoint state and the three
  benchmark branch artifacts identical to package `0.1.0.56`. The change is display-only.
- [throughput.json](throughput.json): reference benchmark recipe, synthetic corpus.
- Worker, runner-recovery and Dev Home lifecycle proofs were **not** rerun for this package; the current record for
  those remains [e0-20261001-dashboard](../e0-20261001-dashboard/notes.md).

## Screenshots

Captured with [`scripts/e0_dashboard_shots.py`](../../../scripts/e0_dashboard_shots.py) during a functional
benchmark with T = 700 (`benchmark-20261001T204059Z-150e8c`). [shots.json](shots.json) lists each PNG with its
SHA-256, package, commit and the status fields it shows.

| Shot | Shows |
| --- | --- |
| [idle-cold.png](idle-cold.png) | Fresh start, no job yet |
| [cooldown-1.png](cooldown-1.png) | First status of branch T's cooldown (trunk step 630) |
| [running.png](running.png) | Trunk at step 1 024 after branch T, EMA and raw curves |
| [cooldown-2.png](cooldown-2.png) | First status of branch 2T's cooldown (trunk step 1 260) |

The completed and idle-after-job states were not captured: publication was requested before the job ended.

An earlier attempt on package `0.1.0.65` recorded Dev Home as its final shot after the app was closed mid-job;
that frame was discarded and the recorder now refuses shots while the app is not running.

## Not on hardware

Commit `15fc8ed` (8 epx gap between marker labels and the curve) came after this package. It passed CI and host
tests only and is not part of `0.1.0.66`.

The idle GPU investigation of [e0-20261001-dashboard](../e0-20261001-dashboard/notes.md#idle-gpu-investigation-remains-open)
is unchanged by this package.
