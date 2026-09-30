# Contributing

Evidence-first research repository. Read [README.md](README.md), [ROADMAP.md](ROADMAP.md), [docs/execution-plan.md](docs/execution-plan.md), [docs/adr/0001-architecture.md](docs/adr/0001-architecture.md), [docs/adr/0002-ste-qat-mapping.md](docs/adr/0002-ste-qat-mapping.md), and the public platform packs under [docs/platform/](docs/platform/dev-mode.md) before opening a pull request.

## Companion repo

The FloppyLM **CPU** path lives in [gianlucamazza/xllama](https://github.com/gianlucamazza/xllama). **Do not modify xllama from this project.** This repository is the GPU research track only. Do not claim that xllama already trains on the GPU.

## Language

- Code, comments, commit messages, and PR titles: **English**.
- Conventional commits (`feat:`, `fix:`, `docs:`, `chore:`, `test:`, `refactor:`).
- Italian is welcome in GitHub Discussions and issue conversation.
- README keeps complete **Italiano** and **English** sections.

## Evidence-first rules

- Do **not** invent tok/s, latency, perplexity, or quality scores.
- Stubs must say `not implemented` / `status: stub`.
- CPU ggml is the numerical and timing baseline once Fase 1 exists. ggml is **not vendored**; the portable GEMM in `src/cpp/cpu_matmul.*` is the current interface ([docs/ggml-baseline.md](docs/ggml-baseline.md)).
- Xbox Series S|X numbers are valid only after Fase 6 Dev Mode measurement (or an explicit `BLOCKED: no console` note).
- Architecture changes need a new or updated file under `docs/adr/` and the `adr` label.

## What this repo is not

- Xbox has **no CUDA**. Do not add CUDA/cuDNN paths or assume NVIDIA tooling.
- DirectML on console is **inference/forward-focused**. Do not describe DirectML as the trainer.
- Public **GDK** / Windows SDK / DirectX 12 is the documented host path.
- **GDKX** / **ID@Xbox** are NDA/partner programmes. This repo does **not** claim access to them.
- Dev Mode purpose licence: develop and test apps, not run a GPU farm. Dev Mode is limited (≤3 consoles per account, typical public documentation). Detail: [docs/platform/dev-mode.md](docs/platform/dev-mode.md), [docs/platform/gdk-vs-gdkx.md](docs/platform/gdk-vs-gdkx.md), [docs/platform/directml-scope.md](docs/platform/directml-scope.md).

## Branch and PR protocol (Cursor and humans)

Follow [docs/execution-plan.md](docs/execution-plan.md):

- One phase at a time. Branch `phase-N/<short-slug>` from latest `main`.
- One PR per phase → `main`.
- Labels: matching `phase-0` … `phase-7` plus topic (`kernel`, `memory`, `benchmark`, `research`, `adr`).
- Milestone: matching **Fase N** (Italian titles already on GitHub).
- PR body: objective, commands run, acceptance results (paste), what was **not** done.
- Do not merge if **CI** jobs `lint-docs` or `build-windows` are red.

## Local checks

```bash
python3 scripts/check_required_docs.py
python3 scripts/check_glossary.py
python3 scripts/check_relative_links.py
python3 benchmarks/run_smoke.py
python3 benchmarks/run_matmul.py
cmake -S . -B build && cmake --build build
./build/xbox_gpu_host --cpu-ref
./build/xbox_gpu_host --bench matmul --out benchmarks/results/matmul.csv
./build/xbox_gpu_host --forward-fixture benchmarks/fixtures/tiny_flp2.json
./build/xbox_gpu_host --grad-check
./build/xbox_gpu_host --train-step 1
```

On Windows with the Windows SDK, compile HLSL with `dxc` and run the host (see [docs/setup.md](docs/setup.md) and `src/hlsl/README.md`). CMake must auto-detect the Visual Studio generator (`cmake -S . -B build -A x64`); do not pin `-G "Visual Studio 17 2022"`.

## Canonical glossary

Use these terms consistently (IT and EN prose):

| Term | Rule |
| --- | --- |
| GPU | Uppercase in prose. Folders stay lowercase (`src/`, `src/hlsl/`). |
| DirectX 12 | First mention; `DX12` is OK afterwards. |
| HLSL | Uppercase. Folder: `src/hlsl/`. |
| compute shader | Two words. |
| DirectML | Inference/forward-focused on console. Never as the trainer. |
| FLP2, codec, QAT, WSD | As written. |
| RMSNorm, RoPE, AdamW | As written. |
| straight-through estimator (STE) | Spell out once, then STE. |
| Dev Mode, UWP, GDK | Public path. |
| GDKX / ID@Xbox | Only when noting the NDA/partner path we do **not** claim. |
| No CUDA on Xbox | Phrase as absence, not as a stack we use. |
| Labels | `research`, `kernel`, `memory`, `benchmark`, `adr`, `phase-0` … `phase-7`. |
| Milestones | **Fase 0–7** (Italian titles). |

## Code owners

`@gianlucamazza` owns `/*` (see `.github/CODEOWNERS`).
