# Xbox Series S vs Series X (public product specs only)

**Honest product differences as publicly documented.** These are **not** benches of this repository’s kernels, not throughput, and not console results.

## Public hardware (not our measurements)

| Console | GPU (public TFLOPS) | Memory (public) |
| --- | --- | --- |
| **Xbox Series X** | **12 TFLOPS** | **16 GB GDDR6** |
| **Xbox Series S** | **4 TFLOPS** | **10 GB GDDR6** |

Sources:

- [Xbox Series X](https://www.xbox.com/en-US/consoles/xbox-series-x) — GPU 12 TFLOPS; memory 16 GB GDDR6
- [Xbox Series S](https://www.xbox.com/en-US/consoles/xbox-series-s) — GPU 4 TFLOPS; memory 10 GB GDDR6
- [Xbox Series X: a closer look at the technology](https://news.xbox.com/en-us/2020/03/16/xbox-series-x-tech/) — Series X table: 12 TFLOPS custom RDNA 2 GPU; 16 GB GDDR6

Retail product pages also list other SKU differences (resolution targets, disc drive vs all-digital, SSD sizes). Those are store/product facts, **not** performance of HLSL compute shaders in this repo.

## What we do not invent

- No fabricated memory or performance benches for **our** kernels on Series S vs Series X.
- No throughput, latency or quality figures except those measured on Dev Mode hardware and listed in [status.md](../status.md) (Series S only; Series X is UNMEASURED).
- Marketing TFLOPS / TOPS (including Xbox Wire ML/DirectML capability figures) are **hardware capability copy**, not a training product claim and not a result table. See [directml-scope.md](directml-scope.md).

UWP App / Creators Game **usable RAM** is **not** the same as the 10 GB / 16 GB GDDR6 product figures. Planning budgets remain **1 GB** (Apps) and **5 GB** (Creators games). See [uwp-resources.md](uwp-resources.md).

## Related

- [dx12-hlsl-compute.md](dx12-hlsl-compute.md) — FL 11.0, App vs Game GPU share
- [console-constraints.md](console-constraints.md)
