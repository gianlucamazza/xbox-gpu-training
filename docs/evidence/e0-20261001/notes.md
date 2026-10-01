# Series S E0 execution acceptance (2026-10-01)

Installed package `GianlucaMazza.XgpuE0_0.1.0.24_x64__g0p5dcfz4t9z4`, exact source
`6a124021d7ae450c5e12056bd70503ed9298f9bd`, push CI run
[36792707081](https://github.com/gianlucamazza/xbox-gpu-training/actions/runs/36792707081).
Installed executable, shader, resources and manifest match CI byte for byte;
`package-lineage.json` records both unsigned and signed package hashes.

- 52 independent GPU operation cases and 36 held-out model fixtures passed.
- Identical-input AdamW and exact checkpoint/branch resume passed.
- Wrong job identity and oversized dispatch are rejected; subsequent valid GPU work succeeds.
- Actual runner interruption and `--resume` complete; completed recovery leaves summary bytes unchanged.
- Launching Dev Home produced a real `suspend` marker and checkpoint at step 66.
  Recovery and uninterrupted reference both completed the 1844-step trunk and all
  cooldowns with identical weights, moments, stream position and branch hashes.
- Representative d=96/layers=3/d_ff=391/ctx=256/batch=32 synthetic benchmark:
  147456 tokens, 153.030679 seconds,
  963.571 token/s, peak app memory 91418624 bytes.
- Companion host suite: 150 tests passed; Ruff checks and native/Windows/UWP builds passed.
- Raw corpus regeneration reproduced all three prepared splits. The 2166391771-byte
  training asset was uploaded in 1034 hash-verified chunks; each trial shares the
  immutable local corpus by hard link.

The old package accepted a wrong identity and poisoned its worker on oversized
dispatch (`worker-baseline.json`). Package 0.1.0.22 then failed status publication
at step 704 with Win32 error 5 (`baseline-publish-failure.json`). The corrected
publisher uses platform file replacement for existing JSON files. The long real
lifecycle retest above passed on 0.1.0.24; no numerical thresholds were changed.
See Microsoft [ReplaceFileFromAppW](https://learn.microsoft.com/en-us/windows/win32/api/fileapifromapp/nf-fileapifromapp-replacefilefromappw)
and [ReplaceFileW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew).

PR #17 was made ready and received an effective CodeRabbit review on source
`1965dc2`: four actionable findings (workflow trigger, dispatch validation,
job identity, bounded suspension deferral) plus action pinning were fixed.
The recorded review and findings are included; all four review threads resolved.
Later green CodeRabbit statuses were rate limited, not a fresh review of the final
publisher change. HLSL was excluded by the review filter; independent GPU parity
provides its validation. CI's benchmark-xbox job was skipped; console evidence
here comes from actual Device Portal runs.

Accepted companion ADR 0011 excludes tensor16 from scientific E0 while keeping
row16 and row8log. No legacy operation aliases or implicit trial migration were
introduced. This evidence certifies functional execution, not language-model
quality. The sequential scientific campaign and its single reserved final test
must finish before quality, selection or paired statistics can be reported.
