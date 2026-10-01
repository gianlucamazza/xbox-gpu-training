# Series S active-kernel acceptance

CI run `36751689355`, package `GianlucaMazza.XgpuE0_0.1.0.19_x64__g0p5dcfz4t9z4`,
source PR merge commit `6dbc407c2f9de06f369c7f24bbbcf671246ce579`. The complete merge tree equals branch
`762628a910ecf8d5130beff90f277bffc02c9f73`. Every installed payload matches CI
byte for byte; signing/repacking metadata is recorded in `package-lineage.json`.

The unused fused attention implementation and its diagnostic cases were removed;
E0 uses Scores, causal Softmax and Weighted. No compatibility aliases remain in
this operation protocol. Necessary input gradients remain part of training.
Device Portal discovery requires its observed `InstalledPackages` response.

- 52/52 independent PyTorch/autograd operation cases passed on GPU.
- 36/36 held-out model/scale/MLP/QK fixtures passed.
- Three-step identical-gradient AdamW and exact checkpoint/branch resume passed.
- Companion host suite: 143 tests passed; Ruff checks and native build passed.
- Representative d=96/layers=3/d_ff=391/ctx=256/batch=32 synthetic run:
  147456 tokens, 153.758310 seconds, 959.012 token/s.
  Peak app memory 91238400 bytes; GPU 13.056601 seconds.
  92736 dispatches; 86828875776 host/device transfer bytes.

Before the fixes, individual operation tests found masked future-position gradients
and shifted-logit cross-entropy cancellation. The failed baseline remains in
`runs/kernel-baseline-20260930T171719Z-0be030` in the companion workspace.
Thresholds were unchanged. Local raw acceptance and benchmark artifacts are in
`runs/xbox-acceptance-20260930-ci36751689355` and
`runs/xbox-benchmark-20260930-ci36751689355`.

These are functional synthetic-corpus results. Scientific E0 and its held-out final
test have not run; the tensor16/S9 protocol proposal still awaits owner acceptance.
CodeRabbit skipped review because PR #17 is a draft; its green status is not review.
