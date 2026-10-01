# Architecture

## Problem

A retail Xbox Series S|X in Dev Mode exposes an RDNA 2 GPU through DirectX 12, but
there is no CUDA, DirectML on console is inference-focused, a UWP **App** has a
planning budget of about 1 GB, and only the public GDK / Dev Mode path is available
([platform/](platform/README.md)). The goal is to train quantized transformers on
that GPU and verify every result against an independent oracle.

Base decisions: [ADR 0001](adr/0001-architecture.md) — DirectX 12 HLSL compute
shaders, fp32 master weights, no CUDA, no DirectML trainer, no GDKX claim.

## Two lanes

|           | E0 trainer                                                                                       | Diagnostic host                                                     |
| --------- | ------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------- |
| Purpose   | Execute the FloppyLM E0 protocol on the console                                                  | Bring up DX12 compute step by step on a desktop                     |
| State     | Active; accepted on Series S                                                                     | Historical (Fase 0–5)                                               |
| Code      | `src/cpp/e0/`, `src/hlsl/e0_tensor.hlsl`, `uwp/`                                                 | `src/cpp/` (`xbox_gpu_host`), other `src/hlsl/` kernels             |
| Runs on   | Series S UWP App (and CPU reference mode anywhere)                                               | Windows DX12 desktop; CPU paths on Linux                            |
| Semantics | FloppyLM exactly ([authority](adr/0005-repository-authority.md), [FloppyLM protocol](https://github.com/gianlucamazza/floppylm/blob/main/docs/adr/0008-e0-numeric-protocol.md)) | Own clipped STE / absmean ([ADR 0002](adr/0002-ste-qat-mapping.md)) |
| Docs      | [e0/](e0/overview.md)                                                                            | [diagnostic/](diagnostic/README.md)                                 |

Results never cross lanes ([claims-policy.md](claims-policy.md)).

## E0 system

```
 FloppyLM (Linux host)                       Series S — XgpuE0 UWP App
 ┌──────────────────────────┐               ┌────────────────────────────────────┐
 │ Python oracle, fixtures  │  Device Portal│ worker thread (uwp/App.cpp)        │
 │ corpus + index plan      │──── files ───▶│   LocalState/inbox → run_job       │
 │ campaign runner          │               │                                    │
 │ FLP2 packing, evaluation │◀─── files ────│ Model (fp32 masters, AdamW, WSD)   │
 └──────────────────────────┘               │   │ effective weights, per step    │
                                            │   ▼                                │
                                            │ GpuKernel ── DX12 ── e0_tensor.cso │
                                            │   resident buffers, 1 read/sample  │
                                            └────────────────────────────────────┘
```

- **Host side of the console process** keeps fp32 master weights and AdamW moments,
  accumulates gradients, and drives WSD trunk and branches.
- **GPU** holds effective weights and activations for the whole step and executes
  every forward and backward primitive.
- **Companion** prepares data, compares results with the oracle and turns branch
  weights into FLP2 models.

Detail: [e0/overview.md](e0/overview.md) (responsibilities and data flow),
[e0/engine.md](e0/engine.md) (code), [e0/job-protocol.md](e0/job-protocol.md) (files),
[e0/runbook.md](e0/runbook.md) (operations).

## Memory

The App planning budget is about 1 GB; the debugger can mask out-of-memory, so only
non-debug Release measurements count ([platform/uwp-resources.md](platform/uwp-resources.md)).
E0 models are small enough to keep the whole training state resident; measured peak
app memory is in [status.md](status.md). The diagnostic lane's chunk streaming and
double buffering ([diagnostic/memory-budget.md](diagnostic/memory-budget.md)) were
designed for larger master copies and are not used by E0.
