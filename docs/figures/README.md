# Figure placeholders (Fase 7)

**No plots live here.** This directory is a caption list for figures that would be drawn only after a real measurement. Do **not** add PNGs, SVGs, or notebooks that invent tok/s, loss curves, Series benches, or peer rankings.

Results prose: [docs/results.md](../results.md).

## Catalog

### F1 — Matmul parity vs tile {#f1}

- **Would show:** max-abs / max-rel of HLSL matmul vs CPU GEMM for the Fase 1 tiles.
- **Have now:** [benchmarks/results/matmul.csv](../../benchmarks/results/matmul.csv) with `status=blocked` (CPU self-check). No GPU series in the committed file.
- **Status:** placeholder. Not a plot.

```
[ F1 PLACEHOLDER — no fabricated matmul chart ]
```

### F2 — FLP2 forward vs CPU fixture {#f2}

- **Would show:** per-op error (decode / RMSNorm / RoPE / tiny forward) on `tiny_flp2`.
- **Have now:** a pass/fail tolerance (max-abs `1e-5` / max-rel `1e-4`) in [flp2-forward.md](../flp2-forward.md). No committed error series.
- **Status:** placeholder.

```
[ F2 PLACEHOLDER — no fabricated forward-error chart ]
```

### F3 — Grad-check {#f3}

- **Would show:** analytic vs central finite-diff points for the tiny STE-identity net.
- **Have now:** gates in [ste-adamw.md](../ste-adamw.md). No committed scatter CSV.
- **Status:** placeholder.

```
[ F3 PLACEHOLDER — no fabricated grad-check scatter ]
```

### F4 — Streaming working-set {#f4}

- **Would show:** peak working-set vs App 1024 MiB (and Game 5120 MiB as a reference line).
- **Have now:** one Linux desktop point (**36.08 MiB** `VmHWM`) from Fase 4. Console AppContainer **UNVALIDATED**.
- **Status:** placeholder. Do not draw a Series trace.

```
[ F4 PLACEHOLDER — no fabricated memory trace ]
```

### F5 — WSD + isolated cooldown LR {#f5}

- **Would show:** `lr(step)` for N=16 with the `stable-mid` overlay.
- **Have now:** the closed-form schedule in [qat-wsd.md](../qat-wsd.md) / [examples/qat-wsd-smoke.json](../../examples/qat-wsd-smoke.json). No plotted artifact.
- **Status:** placeholder. A formula is not a quality curve.

```
[ F5 PLACEHOLDER — no fabricated LR / loss plot ]
```

### F6 — Series S|X benches {#f6}

- **Would show:** tok/s / quality / PIX on Dev Mode hardware.
- **Have now:** [console.md](../console.md) **`BLOCKED: no console`** on `main` @ `66224e05`. Empty metric cells. Kit may reopen this as a **follow-up measured PR**.
- **Status:** placeholder. **UNMEASURED.**

```
[ F6 PLACEHOLDER — BLOCKED: no console — no fabricated Series chart ]
```

### F7 — BitNet / peer overlay {#f7}

- **Would show:** this repo vs BitNet b1.58 only after **both** sides are measured on a declared host.
- **Have now:** paper-sourced BitNet numbers in [results.md](../results.md); our columns **UNMEASURED**.
- **Status:** placeholder. **Not a ranking.**

```
[ F7 PLACEHOLDER — no fabricated peer-ranking chart ]
```

## Rules

- No CUDA plots. No DirectML-as-trainer charts.
- Do not commit a figure that implies Series S|X measurement until Fase 6 (or a follow-up) fills real cells.
- Prefer replacing a placeholder in a later PR over inventing an image in this one.
