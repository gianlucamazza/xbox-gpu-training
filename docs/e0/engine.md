# E0 engine

Code-level map of the E0 trainer. Behaviour is fixed by ADR 0003/0004
([overview.md](overview.md)); this page explains where it lives.

## Source tree

| File | Role |
| --- | --- |
| `src/cpp/e0/tensor.h`, `tensor.cpp` | `Op` enum (14 primitives), `Command` (44-byte constant block mirrored by the HLSL cbuffer), `Storage`/`Tensor`, abstract `Kernel`, `CpuKernel` reference, reverse-mode autograd `Graph`. |
| `src/cpp/e0/dx12_kernel.cpp` | `GpuKernel` (`make_gpu`): DX12 device, root signature, buffer `Pool`, timestamps, `run`/`read`/`flush`. Rejects WARP. |
| `src/cpp/e0/model.h`, `model.cpp` | `Config`, `quantize` (row16 / row8log / tensor16), `Model` forward/step/AdamW/checkpoint/restore, fixture reports. |
| `src/cpp/e0/job.cpp` | SHA-256, `atomic_json`, asset verification and chunk assembly, batch reader, WSD learning rate, `run_job`. |
| `src/cpp/e0/main.cpp` | `xgpu_e0_train` CLI (Windows/Linux, reference or GPU). |
| `src/hlsl/e0_tensor.hlsl` | One `CSMain` multi-op compute shader; `op`/`mode` select the primitive and its forward (0) or input-gradient (1..3) variant. Compiled with `dxc -T cs_6_0 -E CSMain -Gis`. |
| `uwp/App.cpp` | Console worker: inbox polling, schema dispatch, suspension handling ([job-protocol.md](job-protocol.md)). |

CMake targets: `xgpu_e0` (static library), `xgpu_e0_train` (CLI), `xgpu_e0_cso`
(shader, when `dxc` is found). The UWP package is built by `uwp/XgpuE0.vcxproj`
through `scripts/build-e0-uwp.ps1`.

## Primitives

`Add, Linear, Norm, Rope, Activation, Heads, Unheads, Embed, Slice, Multiply,
CrossEntropy, Scores, Softmax, Weighted`. Attention is composed from `Scores`,
causal `Softmax` and `Weighted`; there is no fused attention kernel. Each dispatch
covers at most 65535 groups of 64 threads; larger tensors are rejected, not split.

## Training step

`Model::step` for one optimizer step:

1. Build effective (quantized / fp16 round-tripped) weights **once per step** and
   keep them on the GPU.
2. For each sample of the batch, record forward + `CrossEntropy` + backward on a
   fresh `Graph`. Nothing is read back while recording.
3. One `Kernel::read` per sample returns the loss and the master gradients
   (the only fence wait).
4. Accumulate gradients on the host in sample order, clip to global norm 1, and
   apply AdamW to the fp32 masters (`Model::apply_gradients`).

`run_job` drives the trunk and the three cooldown branches (copies of the model),
checkpoints every 64 steps and on cancel, and writes `branch-<end>.json` weights.

## Execution engine (E0.1)

The GPU-resident engine (PR #18) changed only how work is submitted:

- Host leaves are uploaded once and stay resident; GPU results never return to the
  host unless read.
- A single open command list records all dispatches with explicit state transitions
  and a UAV barrier per dispatch.
- Buffers are recycled through a size-keyed `Pool`; upload buffers still referenced
  by an unexecuted list are parked until the next flush.
- `transfer_bytes` counts real uploads and readbacks.

### Bit-identity rule

An execution-engine change must keep the shader bytecode, dispatch parameters,
reduction order and gradient accumulation order. Then outputs are bit-identical
and no scientific gate changes. Proof required before switching packages:

1. CPU `--reference` outputs byte-identical on the acceptance fixtures.
2. Same `e0_tensor.cso` SHA-256.
3. GPU acceptance outputs identical to the previous package
   ([bit-identity.json](../evidence/e0-20261001-resident/bit-identity.json)).

Changes that reorder reductions (tiled matmul, fusion, cross-sample batching) are
a new engine generation and need fresh gates and an ADR.

## Local use

```bash
cmake -S . -B build && cmake --build build
./build/xgpu_e0_train --help
./build/xgpu_e0_train --reference --fixture fixture.json --out actual.json
```

`--reference` uses `CpuKernel`. Without it the GPU kernel is required; on hosts
without a hardware D3D12 adapter it fails with `BLOCKED: …`.
