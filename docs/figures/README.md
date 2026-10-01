# Figure placeholders

**No plots live here.** This directory is a caption list for figures that would be drawn only after a real measurement. Do **not** add PNGs, SVGs, or notebooks that invent tok/s, loss curves, Series benches, or peer rankings.

Results prose: [docs/results.md](../results.md).

## Catalog

### F1 — Matmul parity vs tile

- **Would show:** max-abs / max-rel of HLSL matmul vs CPU GEMM for the Fase 1 tiles.
- **Have now:** [benchmarks/results/matmul.csv](../../benchmarks/results/matmul.csv) with `status=blocked` (CPU self-check). No GPU series in the committed file.
- **Status:** placeholder. Not a plot.

```
[ F1 PLACEHOLDER — no fabricated matmul chart ]
```

### F2 — FLP2 forward vs CPU fixture

- **Would show:** per-op error (decode / RMSNorm / RoPE / tiny forward) on `tiny_flp2`.
- **Have now:** a pass/fail tolerance (max-abs `1e-5` / max-rel `1e-4`) in [flp2-forward.md](../diagnostic/flp2-forward.md). No committed error series.
- **Status:** placeholder.

```
[ F2 PLACEHOLDER — no fabricated forward-error chart ]
```

### F3 — Grad-check

- **Would show:** analytic vs central finite-diff points for the tiny STE-identity net.
- **Have now:** gates in [ste-adamw.md](../diagnostic/ste-adamw.md). No committed scatter CSV.
- **Status:** placeholder.

```
[ F3 PLACEHOLDER — no fabricated grad-check scatter ]
```

### F4 — Streaming working-set

- **Would show:** peak working-set vs App 1024 MiB (and Game 5120 MiB as a reference line).
- **Have now:** one Linux desktop point (**36.08 MiB** `VmHWM`) from Fase 4. Console AppContainer **UNVALIDATED**.
- **Status:** placeholder. Do not draw a Series trace.

```
[ F4 PLACEHOLDER — no fabricated memory trace ]
```

### F5 — WSD + isolated cooldown LR

- **Would show:** `lr(step)` for N=16 with the `stable-mid` overlay.
- **Have now:** the closed-form schedule in [qat-wsd.md](../diagnostic/qat-wsd.md) / [examples/qat-wsd-smoke.json](../../examples/qat-wsd-smoke.json). No plotted artifact.
- **Status:** placeholder. A formula is not a quality curve.

```
[ F5 PLACEHOLDER — no fabricated LR / loss plot ]
```

### F6 — Series S|X benches

- **Would show:** scientific quality / PPL, Series X, matched CPU, PIX on Dev Mode hardware.
- **Have now:** Series S E0 functional evidence in [status.md](../status.md) (operation and fixture gates, synthetic throughput). Quality, Series X, matched CPU and PIX are UNMEASURED.
- **Status:** placeholder. Functional evidence is cited in text — **not** a quality chart.

```
[ F6 PLACEHOLDER — no fabricated Series quality / PIX / Series X chart ]
```

### F7 — BitNet / peer overlay

- **Would show:** this repo vs BitNet b1.58 only after **both** sides are measured on a declared host.
- **Have now:** paper-sourced BitNet numbers in [results.md](../results.md); our columns **UNMEASURED**.
- **Status:** placeholder. **Not a ranking.**

```
[ F7 PLACEHOLDER — no fabricated peer-ranking chart ]
```

## Rules

- Figures follow the [claims policy](../claims-policy.md).
- Do not commit a figure that implies scientific quality, Series X, matched CPU, or PIX until those cells are measured. Do not draw a peer-ranking chart from E0 functional tok/s.
- Prefer replacing a placeholder in a later PR over inventing an image in this one.
