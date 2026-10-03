# Real-deadline watchdog qualification — 0.1.0.98

Purpose: **functional**. No scientific campaign was started or resumed.

## Package and scope

The installed Release package is `GianlucaMazza.XgpuE0_0.1.0.98_x64__g0p5dcfz4t9z4`, built by
CI [37112204165](https://github.com/gianlucamazza/xbox-gpu-training/actions/runs/37112204165)
from PR merge source `cc134fe4458ffa51c73331ee6dd6af4a6ae8b0d7`. Its tree matches PR head
`3ecac8a687e75e895707c505c9b6cf5f3d7fe163`. [Lineage](package-lineage.json) binds unsigned/signed package
hashes and all preserved CI payloads. Signing used the existing development
certificate. Deployment used an Odroid SSH tunnel and the existing Device Portal
TLS pin; no pin or credential configuration was changed and no uninstall occurred.

The post-install start command returned HTTP 400, but the process listing and two
advancing heartbeat observations then verified the exact new package and source,
idle and ready. All gates below started only after that verification.

The [previous attempt](../e0-20261003-096-incomplete/notes.md) on 0.1.0.96 found a native
resume-binding defect. This package includes the claim-validation repair and was
qualified afresh. The intermediate 0.1.0.97 artifact was not installed.

## Measured hardware gates

- [Acceptance](acceptance.json): 36 fixtures, 52 independent GPU operation cases
  ([kernel proof](kernel-parity.json)), AdamW and exact resume passed.
- [Watchdog](watchdog.json): execution parked after checkpoint step 2, ordinary
  heartbeat advanced, published fence stayed frozen, and the actual process exited
  after 600.460 seconds. The
  [fault](watchdog-fault.json) records the real 600-second deadline; the runner
  did not invoke the fault handler or terminate the process. Full temporal evidence
  is in [runtime-events.jsonl](runtime-events.jsonl).
- One explicit restart produced a [new idle worker](worker-after-restart.json).
  [Verified checkpoint recovery](recovery-comparison.json) matched the uninterrupted control's three branch
  hashes and checkpoint tensors, optimizer moments, step and stream position.
- [Worker guards](worker.json), [runner recovery and completed-job idempotence](runner-recovery.json)
  passed. [Real Dev Home suspension](lifecycle.json) interrupted at step
  93 and recovered exactly.
- [Bit identity](bit-identity.json) with accepted 0.1.0.95: 38/38 canonical
  acceptance payloads, six raw branch files, two numerical checkpoint states,
  three benchmark branch hashes and shader CSO agree. Only declared execution
  telemetry is excluded. Reference [lineage](reference-095/package-lineage.json)
  and [benchmark](reference-095/throughput.json) are byte-preserved copies from the
  companion's `xbox-e0-20261003-095` record.
- [Synthetic benchmark](throughput.json): 9747.980 token/s;
  peak app memory 127991808 bytes. This is functional throughput,
  not a quality result or a controlled performance improvement claim.
- [Preservation](preservation.json): pre-existing inbox inventory and committed
  scientific job JSON hashes/status descriptors match the baseline. Large archived
  payloads were checked by inventory, not rehashed. Unique functional test files
  are retained; stale worker state was not restored. The final worker is idle.

[Dashboard](idle-final.png) and [capture binding](shots.json) record the final state.

The initiating cause of the historical scientific stall remains unproven. This
controlled test qualifies the published-fence watchdog and explicit recovery;
it does not establish a scientific baseline or authorize another E0 campaign.
Historical campaigns retain their original package/source bindings.

## Reproduction and raw evidence

Raw run: `runs/watchdog-release-20261003-ci37112204165/` in the companion repository.
Run `xbox_acceptance.py`, then `xbox_watchdog_acceptance.py` with that exact acceptance,
then worker, recovery, lifecycle and benchmark harnesses. Native
`scripts/verify_e0_bit_identity.py` compares against the accepted 0.1.0.95 run.
Host validation: 399 tests including native conformance; backend: five CTest
tests and 30 script unit tests. CI checks must be assessed separately from these
package-bound hardware measurements.
