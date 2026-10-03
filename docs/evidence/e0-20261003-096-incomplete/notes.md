# Functional watchdog attempt: incomplete qualification

Package `0.1.0.96`, source `c30a596dc0c387e4e50d7f23f343598cd79c45cd`,
CI [37110462015](https://github.com/gianlucamazza/xbox-gpu-training/actions/runs/37110462015).
This candidate is **not accepted**. No scientific campaign was run.

The [numerical acceptance](acceptance.json) passed. The real published-fence watchdog
exited the process after [600.165 seconds](process-exit.json), with independent
heartbeat advancement and a bound [600856 ms timeout fault](watchdog-fault.json).
[Observations](runtime-events.jsonl) contain the package, worker and exact job binding.

Explicit restart produced an idle new worker. Recovery removed the functional probe
as specified, but the native immutable-recipe comparison did not normalize that
removal. It rejected publication with `claim_resume_binding_mismatch`; the host
then reached its [acknowledgment timeout](failure.json). Exact recovery, worker,
lifecycle and benchmark gates were not completed for this package.

The repair belongs in native claim validation: normalize the probe in the original
payload only, retaining exact checkpoint/recipe checks and forbidding a probe in
the resumed payload. The replacement package requires its own complete hardware
qualification. These results are not transferred to a later package.

Raw files remain in the companion run directory
`runs/watchdog-release-20261003-ci37110462015/`.
