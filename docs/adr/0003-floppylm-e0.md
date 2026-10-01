# ADR 0003 — Execute the FloppyLM E0 protocol on Xbox GPU

- Status: Accepted
- Date: 2026-09-30 (owner request to implement the cross-project plan)
- Amended: 2026-10-01 — repository boundaries follow `floppy_4mb` ADR 0012 (this repo is the only native FloppyLM backend)
- Supersedes: ADR 0002 for the E0 backend only; diagnostic-lane smokes keep their contract
- Superseded in part by: ADR 0004 (numerical thresholds)
- Labels: `adr`, `research`, `phase-6`

## Context

The local FloppyLM bench at `Workspace/experiments/floppy_4mb` is the independent
Python oracle. The owner selected a dedicated Series S in Dev Mode. Existing host
smokes are not a full transformer trainer and are not an Xbox UWP package.

## Decision

- Keep the E0 backend separate from the tiny-network smoke. Match FloppyLM's model,
  quantization and optimizer; do not change FloppyLM to match a GPU implementation.
- Python supplies initial fp32 weights, SHA-256-bound corpus and deterministic batch
  indices. The console reads local windows; test data is not uploaded to training.
- Use ternary retained-entry scales, delta thresholds, fp16 scale/norm round trips,
  row16/row8log/tensor16 policies, identity STE and exact zero rows. Even-level grids
  use FloppyLM's floor/midrise rule rather than the smoke's nearest rounding.
- Implement the full causal transformer forward/backward in DX12 compute shaders.
  CPU execution is explicitly a reference mode, never a successful GPU fallback.
- AdamW runs on fp32 master weights with beta1=0.9, beta2=0.95, eps=1e-8; only
  quantized matrices have weight decay. Clip the global gradient norm to 1.
- Implement stable-trunk WSD with isolated branches ending at T, 2T and 4T, including
  model, moments, step and stream position in portable atomic checkpoints.
- Return master weights to Python for canonical FLP2 packing and evaluation. This
  repository does not invent or duplicate the binary FLP2 envelope.
- Use a separate x64 UWP Release app, hardware adapter only, one training job at a
  time, and a measured 1 GiB memory budget. Existing xllama is not modified.
- Gates precede the campaign: exact symbols/scale bytes; max absolute error 1e-5 and
  relative error 1e-4 for reference magnitudes >=1e-2, against Python per operation,
  logits, loss, gradient and optimizer state. Smaller values are abs-gated. A failed
  gate stops progress; thresholds are not relaxed retrospectively.

## Consequences

CI/Win32/WARP passing is not console acceptance. Host smoke reports, hardware
dispatch evidence and scientific E0 evidence are separate. Dedicated console time
has no prefixed cap; sustained throughput is measured before the campaign.

## Alternatives considered

Using the smoke's clipped STE, absmean quantizer, fixed epsilon or cooldown overlays
would change the experiment. Editing xllama or silently falling back to CPU would
violate the agreed scope. These alternatives are rejected.
