# History

Chronology of the project. Current state lives in [status.md](status.md).

## Timeline

| Date | Change | Reference |
| --- | --- | --- |
| 2026-09-30 | Repository scaffold, platform fact packs | #1, #7 |
| 2026-09-30 | Fase 0–5 diagnostic lane: DX12 host and hello compute, HLSL matmul, scalar FLP2 forward, STE + AdamW, streaming, QAT/WSD smoke | #8, #9, #11–#14 |
| 2026-09-30 | Fase 6 scaffolding for the Win32 host, reported `BLOCKED: no console` (honest for that lane at that commit) | #15 (`66224e0`) |
| 2026-09-30 | E0 backend accepted on Series S: package `0.1.0.11`, then `0.1.0.19` with the final operation set | [e0-20260930](evidence/e0-20260930/notes.md), [e0-20260930-kernels](evidence/e0-20260930-kernels/notes.md) |
| 2026-10-01 | E0 transformer backend and UWP app merged; package `0.1.0.24` accepted with lifecycle, recovery and worker proofs | #17 (`aec1a2a`), [e0-20261001](evidence/e0-20261001/notes.md) |
| 2026-10-01 | Fase 7 publication of functional results, owner-reviewed | #16 (`af4f963`), [results.md](results.md) |
| 2026-10-01 | First scientific campaign `e0-20261001T074326Z-503df0` stopped cleanly at trunk step 455 (GPU busy ~8.6%) | [e0-20261001-resident](evidence/e0-20261001-resident/notes.md) |
| 2026-10-01 | E0.1 GPU-resident engine, package `0.1.0.28`, bit-identical to `0.1.0.24`; campaign `e0-20261001T090514Z-4236fd` started | #18 (`7abd040`) |
| 2026-10-01 | Repository boundaries aligned with FloppyLM ADR 0012 | #19 (`b044192`) |
| 2026-10-01 | Owner stopped the E0.1 scientific campaign; no new campaign started | [canonical FloppyLM status](https://github.com/gianlucamazza/floppylm/blob/main/docs/STATUS.md) |
| 2026-10-01 | ADR 0005, canonical documentation links and dashboard update caching merged | [PR #27](https://github.com/gianlucamazza/xbox-gpu-training/pull/27) (`53ab3c2`) |
| 2026-10-01 | Actual CodeRabbit review accepted; openappx PR #7 merged and 0.7.0 installed locally | [openappx record](evidence/e0-20261001-dashboard/openappx.json) |
| 2026-10-01 | Package 0.1.0.56 signed/deployed with TLS pin; full hardware gates and bit identity passed; idle GPU hypothesis falsified | [release evidence](evidence/e0-20261001-dashboard/notes.md) |

## Phases (GitHub milestones)

Milestones keep their Italian titles. Phases 0–5 form the [diagnostic lane](diagnostic/README.md).

| Phase | Milestone | Gate | Outcome |
| --- | --- | --- | --- |
| 0 | [Fase 0 — Setup ambiente](https://github.com/gianlucamazza/xbox-gpu-training/milestone/1) | DX12 device, `dxc`, hello compute, CI | Done; GPU dispatch `BLOCKED` on hosts without D3D12 |
| 1 | [Fase 1 — Kernel HLSL di base](https://github.com/gianlucamazza/xbox-gpu-training/milestone/2) | Matmul parity vs CPU | Done; tolerances chosen, committed CSV is CPU self-check |
| 2 | [Fase 2 — Forward FLP2 su GPU](https://github.com/gianlucamazza/xbox-gpu-training/milestone/3) | Forward match on tiny fixture | Done; binary FLP2 envelope out of scope |
| 3 | [Fase 3 — Backward + AdamW](https://github.com/gianlucamazza/xbox-gpu-training/milestone/4) | Grad check + one step | Done ([ADR 0002](adr/0002-ste-qat-mapping.md)) |
| 4 | [Fase 4 — Streaming memoria](https://github.com/gianlucamazza/xbox-gpu-training/milestone/5) | Double buffer under App ~1 GB | Done on desktop; console UNVALIDATED |
| 5 | [Fase 5 — QAT completo WSD](https://github.com/gianlucamazza/xbox-gpu-training/milestone/6) | Schedule + N-step smoke | Done; N=16 schedule smoke only |
| 6 | [Fase 6 — Validazione Series S\|X](https://github.com/gianlucamazza/xbox-gpu-training/milestone/7) | Console table or `BLOCKED` | Series S E0 measured; Series X and diagnostic workloads UNMEASURED |
| 7 | [Fase 7 — Pubblicazione](https://github.com/gianlucamazza/xbox-gpu-training/milestone/8) | Honest results page | Functional results published; scientific results pending |
