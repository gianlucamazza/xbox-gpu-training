# Series S E0.1 GPU-resident engine acceptance (2026-10-01)

Installed package `GianlucaMazza.XgpuE0_0.1.0.28_x64__g0p5dcfz4t9z4`, exact source
`25f8bc3966ffae940658be94161d31edd83492c9`, push CI run
[36839565773](https://github.com/gianlucamazza/xbox-gpu-training/actions/runs/36839565773).
It was repacked with OpenAppx and signed with the development certificate. The
executable, shader, resources and manifest match CI byte for byte
(`package-lineage.json`).

## Why

The 0.1.0.24 engine created eight committed resources, uploaded every input and
waited on a fence for each operation, and it re-quantized weights for every sample.
Its first scientific trial measured 329 GPU seconds in 3848 wall seconds (about
8.6%) and 2.19 TB transferred by trunk step 455. That trial was stopped cleanly
with a console checkpoint; campaign `e0-20261001T074326Z-503df0` is recorded as
stopped in the companion.

## What changed

- Tensors stay on the GPU. Host leaves are uploaded once; the effective
  (quantized/fp16) weights are built once per optimizer step.
- One open command list uses pooled buffers, explicit state transitions and a UAV
  barrier per dispatch. The only fence wait is a read: once per sample, for the
  loss and master gradients.
- The shader bytecode (`e0_tensor.cso` sha `a6e6c2f6…`), dispatch parameters,
  reduction order and gradient accumulation order are unchanged.
- `transfer_bytes` now counts actual uploads and readbacks. The 0.1.0.24 value
  counted every operation's inputs and output.

## Results

- Hardware acceptance `ok`: 52 operation cases, 36 model fixtures,
  identical-input AdamW and exact resume. ADR 0004 thresholds are unchanged.
- **Bit identity with 0.1.0.24** (`bit-identity.json`):
  - all 38 acceptance cases;
  - the full/resumed branch weights after 32 steps;
  - the three benchmark branch artifacts (same SHA-256).
    The CPU reference outputs are byte-identical on all fixtures.
- Representative synthetic benchmark, same config (d=96/layers=3/d_ff=391/ctx=256/batch=32):

  | Metric                 | 0.1.0.24    | 0.1.0.28          |
  | ---------------------- | ----------- | ----------------- |
  | Tokens                 | 147456      | 147456            |
  | Wall time              | 153.03 s    | 14.42 s           |
  | GPU time               | 13.04 s     | 11.85 s           |
  | Speed                  | 963.6 tok/s | **10224.3 tok/s** |
  | Dispatches             | 92736       | 92736             |
  | Transferred            | 86.8 GB     | 0.86 GB           |
  | Peak app memory        | 87.2 MiB    | 105.3 MiB         |
  | Estimated trial length | ~8.9 h      | ~50 min           |

- Worker probes: a wrong identity and an oversized dispatch are both rejected, and
  later work reuses the worker (`worker.json`).
- Runner recovery: completed retrieval is unchanged, runner resume is idempotent,
  and interrupted recovery is exact (`runner-recovery.json`).
- Real Dev Home suspension: the trainer published a checkpoint at step 461. The
  recovered and uninterrupted runs finished the 1844-step trunk with identical state
  (`lifecycle.json`).

Campaign `e0-20261001T090514Z-4236fd` started on this package and these proofs. This
evidence certifies functional execution and numerical identity with the previous
package, not language-model quality.
