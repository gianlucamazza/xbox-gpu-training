# Claims policy

This repository reports only what it measured. This page is the single statement
of that rule; other pages link here instead of repeating it.

## Evidence tags

Every number or capability claim in the docs carries one of these tags, explicitly
or by linking to its source:

| Tag             | Meaning                                                                                                    |
| --------------- | ---------------------------------------------------------------------------------------------------------- |
| **Sourced**     | The value is in a committed artifact (`docs/evidence/`, `benchmarks/results/`) or a cited public document. |
| **UNMEASURED**  | No measurement exists. The cell stays empty or carries this word.                                          |
| **BLOCKED**     | The path did not run. The reason is written (for example `BLOCKED: no D3D12 device`).                      |
| **UNVALIDATED** | A planning budget or SKU figure exists, but this workload was not measured against it.                     |
| **Placeholder** | A caption for a figure that was not drawn.                                                                 |

A value without one of these tags is not written.

## Rules

- No invented throughput, latency, perplexity or quality numbers. Stubs say `status: stub`.
- Series S E0 throughput is **functional** evidence (synthetic corpus). It is not a
  quality, perplexity or peer-ranking number and is never copied into a peer table.
- Console results come only from a non-debug Release package on Dev Mode hardware.
  A debugger can mask out-of-memory; WARP / Basic Render Driver is never a console.
- Win32 diagnostic-lane results (Fase 0–5) certify nothing for the E0 trainer, and
  E0 results are never copied onto diagnostic-lane rows.
- Current-state numbers live in [status.md](status.md) and the evidence notes.
  Other pages link to them; `scripts/check_doc_claims.py` enforces this.
- Architecture changes need an ADR under [adr/](adr/README.md).

## What this repository does not claim

- **CUDA.** Xbox has no CUDA; there are no CUDA or cuDNN paths.
- **DirectML as trainer or optimizer.** DirectML on console is inference/forward-focused
  ([platform/directml-scope.md](platform/directml-scope.md)). Training runs in DirectX 12
  HLSL compute shaders.
- **GDKX or ID@Xbox access.** Only the public GDK / Windows SDK and retail Dev Mode are used
  ([platform/gdk-vs-gdkx.md](platform/gdk-vs-gdkx.md), [platform/dev-mode.md](platform/dev-mode.md)).
- **Game designation memory.** The package is a UWP App; planning budget ~1 GB
  ([platform/uwp-resources.md](platform/uwp-resources.md)).
- **Language-model quality** until the scientific E0 campaign publishes its gated result.
- **Series X, matched CPU or PIX measurements** — none exist ([status.md](status.md#unmeasured)).

## Repository boundaries

FloppyLM semantics, the FLP2 format and the `floppylm.*.v1` contracts are owned by the
[FloppyLM](https://github.com/gianlucamazza/floppylm) repository ([its ADR 0012](https://github.com/gianlucamazza/floppylm/blob/main/docs/adr/0012-repo-boundaries.md), [backend ADR 0005](adr/0005-repository-authority.md)). It is the
Python oracle and campaign runner. This repository is its only native training
backend (E0 on DirectX 12 / UWP) and does not define or duplicate the binary FLP2
envelope.

[xllama](https://github.com/gianlucamazza/xllama) contains no FloppyLM logic (its
`feat/floppylm-training` PR #301 was closed unmerged). Do not modify it from here and
do not describe it as a trainer.
