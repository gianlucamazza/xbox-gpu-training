# Backend audit — 2026-10-01

Scope: native reference input validation and immutable result publication. These corrections
are separate from the dashboard package accepted on hardware; a Linux reference test is not
Xbox GPU validation.

| Finding | Evidence | Correction |
| --- | --- | --- |
| P1: malformed Add input reads outside its buffer; Linear with zero output width divides by zero | Baseline Linux reference exited with SIGABRT and SIGFPE, respectively | Shared CPU/D3D12 command validation before execution; `e0_guards` covers both refusals |
| P1: duplicate-job rejection can replace a previous status with `failed` in the UWP worker | Worker catch wrote the same `status.json` after the result-exists refusal | `record_failure` preserves committed status and writes `<id>.rejected.json`; guard test verifies both paths |
| P1: resumed branch is overwritten before its hash is compared with the previous artifact | Publication order in `run_job`; [fault-injection proof](branch-preservation.json) | Write a candidate, verify previous metadata, then publish or discard the identical candidate; retain divergent candidates |

Verification: native CTest 3/3; independent companion suite with the corrected native binary
278/278 at the first guard validation; 52 valid operation cases and model/optimizer/exact-resume
coverage are included. The branch fault injection rewinds a deliberately altered checkpoint:
resume fails explicitly and the trusted original bytes remain unchanged.

Remaining observation: `WaitForGpu` uses an infinite fence wait. A device-hang scenario has not
been reproduced. A bounded fence/device-loss policy needs a separate operational decision and
hardware fault validation; the companion publication ADR does not define that policy.

Historical hardware evidence and the stopped scientific campaign remain untouched.
