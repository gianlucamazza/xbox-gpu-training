# ADR 0004: Independent E0 numerical gates

## Status

Accepted — 2026-09-30, explicit owner acceptance of the numerical validation proposal.
Supersedes only the numerical thresholds in ADR 0003.

## Context

Exploratory FloppyLM fixtures exposed ill-conditioning of the integrated first AdamW
step near epsilon. Gradients around -4e-8, differing by 8e-10 and passing their gate,
produced a 1.41759e-5 parameter difference. This compares different optimizer inputs.

## Decision

- Require exact quantized symbols and canonical serialized scale bytes.
- Finite floating-point outputs and gradients must satisfy, per element,
  `abs(actual-reference) <= 1e-5 + 1e-4 * abs(reference)`.
- Validate AdamW separately with identical weights, moments, gradients, clipping,
  learning rates and step numbers. Compare weights and both moments under that bound.
- Preserve integrated one-step parameter error as a diagnostic, requiring its
  forward/gradient gates and finite state. Do not use it as the optimizer gate.
- Require exact native checkpoint/resume state equality, all scientific gates and
  real hardware dispatch evidence. Generate held-out fixtures after acceptance.

## Consequences

Record the exploratory failures rather than changing them into successes. No
scientific E0 campaign runs before the independent and console gates pass.

## Alternatives

Relaxing an integrated update threshold to fit a single observed example would hide
the ill-conditioned test. Matching a particular CPU reduction order is not an
optimizer specification. Both alternatives are rejected.
