# Diagnostic lane (Fase 0–5)

The original Win32 desktop host (`xbox_gpu_host`, `examples/hello-compute/`) and its
tiny fixtures. It was built phase by phase to bring up DirectX 12 compute, matmul,
a scalar FLP2 forward, a clipped STE with AdamW, streaming and a WSD schedule.

**Status: historical.** Its quantizer, STE and schedule differ from FloppyLM; its
results certify nothing for the E0 trainer and none of its workloads were run on a
console. The lane still builds in CI (`build-windows`) and its CPU paths run on
Linux. The active trainer is documented in [../e0/overview.md](../e0/overview.md).

| Page | Phase | Contract |
| --- | --- | --- |
| [setup.md](setup.md) | 0–5 | Toolchain (Windows SDK, `dxc`, PIX, CMake) and host commands |
| [ggml-baseline.md](ggml-baseline.md) | 1 | Portable CPU GEMM baseline and matmul tolerances |
| [flp2-forward.md](flp2-forward.md) | 2 | Scalar FLP2 decode, RMSNorm, RoPE, tiny forward |
| [ste-adamw.md](ste-adamw.md) | 3 | FakeQuant, clipped STE, host AdamW, grad-check ([ADR 0002](../adr/0002-ste-qat-mapping.md)) |
| [memory-budget.md](memory-budget.md) | 4 | Chunk streaming and double buffer against the App ~1 GB planning budget |
| [qat-wsd.md](qat-wsd.md) | 5 | QAT bit-widths, WSD with isolated cooldown overlays, N=16 smoke |

Outcomes per phase: [../history.md](../history.md). The original agent playbook is
archived at [../archive/execution-plan.md](../archive/execution-plan.md).
