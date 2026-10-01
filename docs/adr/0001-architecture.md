# ADR 0001 — Architecture for quantized LLM training research on Xbox GPU

- Status: Accepted
- Date: 2026-09-30
- Amended: 2026-10-01 — §3 FLP2 ownership moved to FloppyLM (`floppy_4mb` ADR 0012); xllama holds no FloppyLM documentation.
- Labels: `adr`, `research`, `phase-0`
- Milestone: [Fase 0 — Setup ambiente](https://github.com/gianlucamazza/xbox-gpu-training/milestone/1)

## Context

Xbox Series S|X in Dev Mode expose an RDNA 2 GPU through DirectX 12. There is no CUDA on Xbox. DirectML, as used on console in the companion app [xllama](https://github.com/gianlucamazza/xllama), is inference/forward-focused and has already shown operator-level pitfalls (for example RMSNorm on some graphs). UWP AppContainer memory is tight (~1 GB App designation vs ~5 GB Game designation, as planning figures). Dev Mode is licensed to develop and test apps, not to run a GPU farm, and is limited (≤3 consoles in typical public documentation).

We need a research path for quantized training that can be measured honestly against a CPU ggml baseline, without claiming ID@Xbox or GDKX access.

## Decision

1. **DirectX 12 compute shaders (HLSL), not CUDA.** Host code uses the public GDK / Windows SDK / DirectX 12 path. GDKX and ID@Xbox are NDA/partner programmes; this repo does not claim them.
2. **Host-resident fp32 master weights** in system RAM, with chunk streaming and double buffering to the GPU. Do not assume the full master copy fits in the UWP App ~1 GB budget.
3. **FLP2 codec** as the quantized weight representation for a reconstructed GPU forward (Fase 2). Implementation lives here; FloppyLM CPU documentation in xllama is reference-only. Do not modify xllama.
4. **CPU ggml is the baseline** for numerical parity and for timing until Fase 6 Dev Mode numbers exist. ggml may be vendored or interfaced later; Fase 1 must document the comparison even if the vendor step is still a stub.
5. **Dev Mode UWP constraints are first-class.** Fase 4 documents App ~1 GB and Game ~5 GB budgets, AppContainer rules, and streaming. Fase 6 is the only phase that may publish console speed/quality tables — or must say `BLOCKED: no console`.
6. **No quality or tok/s claims until measured.** Stubs report `status: stub`. DirectML may be mentioned as an inference/forward stack; it is not the trainer.

Public Microsoft/Xbox fact packs (not a change of decision): [docs/platform/](../platform/dev-mode.md).

## Consequences

- CI on `windows-latest` can compile the C++ host and, when `dxc` exists, HLSL. It cannot replace Xbox hardware.
- Real GPU benches need Dev Mode hardware or a self-hosted runner (`self-hosted`, `windows`, `xbox-gpu`). That runner is optional and disabled by default.
- Contributors must open an ADR when they change any decision above.
- Public BitNet/peer comparison waits for Fase 7.

## Alternatives considered

| Alternative | Why not (now) |
| --- | --- |
| CUDA / cuDNN kernels | Xbox has no CUDA. |
| DirectML as the training engine | Inference/forward-focused on console; not the research trainer. |
| Keep all master weights in GPU-local memory | Conflicts with UWP App RAM / VRAM budgets. |
| Claim GDKX / ID@Xbox | Access is not claimed; public DirectX 12 is enough to start. |
