# Measured results

The active separate E0 UWP backend has passed functional validation on retail
Series S. [Current evidence](evidence/e0-20261001/notes.md) owns the exact package
lineage, independent numerical checks, worker rejection/reuse, real suspension,
runner recovery and representative throughput measurement. Earlier evidence
directories retain historical packages and baseline results.

Representative synthetic E0: 147456 training tokens in 153.030679 seconds,
963.571 token/s and 91418624 bytes peak app memory. This is a short functional
benchmark; its extrapolation excludes corpus transfer and Python evaluation.

No scientific quality or selected scalar frontier is available yet. The companion
campaign must complete accepted row16/row8log selection, tuning, solver grids,
five paired seeds, actual-byte/saturation gates and the single reserved final test.
Only then can generated paired statistics, costs and exclusions be published.

No Series X measurement, matched CPU comparison, peer/BitNet comparison or PIX
capture is available. The original Win32 host smokes remain diagnostics; xllama's
CPU trainer is a separate project. [Console status](console.md),
[completion roadmap](../ROADMAP.md).
