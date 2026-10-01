# Dashboard package acceptance — 2026-10-01

Purpose: **functional**. This record does not certify language-model quality or restart a scientific campaign.

## Artifact and deployment

CI [36885338811](https://github.com/gianlucamazza/xbox-gpu-training/actions/runs/36885338811)
built source `53ab3c25c21dff633b8dba6913ddf3f8020b4f65` (merged backend PR #27).
The installed package is `GianlucaMazza.XgpuE0_0.1.0.56_x64__g0p5dcfz4t9z4`.
[package-lineage.json](package-lineage.json) records the unsigned and signed SHA-256,
unchanged CI payload hashes, existing development signing certificate and pinned Device Portal TLS certificate.
Diagnostic packages were replaced by this production artifact; probe configuration files were removed.

[openappx.json](openappx.json) records merged PR #7, the actual final CodeRabbit review
("No actionable comments" covering commit `5058b56`), local installation of 0.7.0 and source-file identity.
[tls-pin.json](tls-pin.json) proves that the expected TLS pin works and an incorrect pin is rejected.
The pipx installation succeeded; its unrelated post-install listing hit a root-owned cursor-agent symlink.
The installed executable and source files were verified independently.

## Hardware gates

- [kernel-parity.json](kernel-parity.json): 52 GPU operation cases passed.
- [acceptance.json](acceptance.json): 36 fixtures, AdamW and exact resume passed.
- [worker.json](worker.json): wrong identity and oversized dispatch rejected; worker reused.
- [runner-recovery.json](runner-recovery.json): exact interrupted recovery and completed-job idempotence passed.
- [lifecycle.json](lifecycle.json): real Dev Home suspension, checkpoint at step 389, exact recovery to trunk step 1844.
- [throughput.json](throughput.json): 10140.175 token/s, peak app memory 116187136 bytes, synthetic corpus.
- [bit-identity.json](bit-identity.json): identical numerical payloads for 38/38 acceptance cases;
  six raw branch files, two numerical checkpoint states, three benchmark artifact hashes and the shader hash agree with package 0.1.0.28.
  Canonical JSON excludes only explicitly declared execution telemetry and preserves schema, numeric types and negative zero.
- [comparison-negative-probes.json](comparison-negative-probes.json): changed schema/numbers rejected,
  integer/float and negative-zero differences preserved, execution telemetry excluded.

[dashboard.png](dashboard.png) shows the accepted package/source and completed benchmark on the console.
Its rolling UI throughput is not the whole-run benchmark average.

## Idle GPU investigation remains open

[idle-investigation.json](idle-investigation.json) records twelve controlled modes.
The repeated-dashboard-invalidation explanation was **falsified** by the hardware retest.
The merged dashboard change avoids redundant brush/progress updates, but did not reduce the global counter.
Worker-off, timer-off, progress-off, no-content and transparent modes stayed high.
A foreground CoreWindow with neither XAML nor D3D12 initialization also reported engine 5 near 98%.
The production package's [idle-final.json](idle-final.json) reports engine 5 near 99.55%.

These are whole-console Device Portal counters, not per-process GPU activity or power measurements.
Independent attribution remains unavailable: tested ETW/provider/performance endpoints returned 404;
AppCapture was not registered on this retail console. A PIX or equivalent independent trace is required
before claiming a cause or a verified reduction. No diagnostic CoreWindow path was merged.

## Reproduction

Raw inputs remain under `runs/xbox-{acceptance,benchmark,worker,recovery,lifecycle}-20261001-ci36885338811`
in the companion checkout; the reference acceptance/benchmark use suffix `ci36839565773`.
The backend's `scripts/verify_e0_bit_identity.py` accepts explicit reference/candidate run directories,
benchmark directories, lineage JSON files and an output path. It refuses incomplete matrices,
changed benchmark recipes/assets and acceptance/lineage identity mismatches.

The stopped scientific campaign and its owner decision remain in the canonical FloppyLM status.
Audit remediations and ADR 0014 recovery are subsequent, separate PRs and are not part of this package.
