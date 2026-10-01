# Evidence index

Committed console evidence, one directory per accepted package. Measured content is
immutable: corrections go in a new directory. Only location metadata (pointers to
raw data in the companion) may be normalised in place. File formats
are described in [../e0/acceptance.md](../e0/acceptance.md#evidence-files).

| Directory | Date | Package | Source | CI run | Content |
| --- | --- | --- | --- | --- | --- |
| [e0-20260930](e0-20260930/notes.md) | 2026-09-30 | `0.1.0.11` | `9744ff7` | 36746732705 | First acceptance (36 fixtures, optimizer), throughput, zero-row diagnostic |
| [e0-20260930-kernels](e0-20260930-kernels/notes.md) | 2026-09-30 | `0.1.0.19` | `6dbc407` | 36751689355 | Final operation set, 52-case kernel parity |
| [e0-20261001](e0-20261001/notes.md) | 2026-10-01 | `0.1.0.24` | `6a12402` | 36792707081 | Full acceptance, lifecycle, recovery, worker fix, corpus upload, data reproducibility, baseline failure, review |
| [e0-20261001-resident](e0-20261001-resident/notes.md) | 2026-10-01 | `0.1.0.28` | `25f8bc3` | 36839565773 | E0.1 engine: acceptance, bit identity, throughput — **active** ([status.md](../status.md)) |

All throughput values are `purpose: functional` on a synthetic corpus.
