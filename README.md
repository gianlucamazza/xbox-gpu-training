# xbox-gpu-training

[![CI](https://github.com/gianlucamazza/xbox-gpu-training/actions/workflows/ci.yml/badge.svg)](https://github.com/gianlucamazza/xbox-gpu-training/actions/workflows/ci.yml)
[![Benchmark](https://github.com/gianlucamazza/xbox-gpu-training/actions/workflows/benchmark.yml/badge.svg)](https://github.com/gianlucamazza/xbox-gpu-training/actions/workflows/benchmark.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Milestones](https://img.shields.io/badge/milestones-Fase%200–7-blue.svg)](https://github.com/gianlucamazza/xbox-gpu-training/milestones)

**Closing the Xbox GPU training gap with DirectX 12 HLSL compute shaders.**

> **EN.** Xbox has no CUDA. DirectML on console is inference/forward-focused. A UWP App typically sees ~1 GB RAM. This repo researches quantized LLM training on Series S|X GPU via DirectX 12 compute shaders, host-resident fp32 master weights, and FLP2 streaming — measured honestly, or not claimed.
>
> **IT.** Su Xbox non c’è CUDA. DirectML in console è orientato a inference/forward. Una UWP App vede in genere ~1 GB di RAM. Questo repo ricerca il training quantizzato di LLM sulla GPU Series S|X con compute shader DirectX 12, pesi master fp32 in system RAM e streaming FLP2 — solo numeri misurati, altrimenti nessun claim.

Companion: FloppyLM **CPU** path lives in [gianlucamazza/xllama](https://github.com/gianlucamazza/xllama). This repository is the **GPU research track**. Do not claim that xllama trains on the GPU. Do not modify xllama from here.

---

## Italiano

### Perché esiste

Su Xbox Series S|X in **Dev Mode** manca CUDA; DirectML è utile per alcuni grafi di inference/forward e **non** è il trainer di questo progetto; una UWP **App** ha un budget RAM tipico di **~1 GB** (designazione **Game** ~**5 GB**); il bus memoria e l’**AppContainer** vincolano i pesi; la licenza Dev Mode serve a sviluppare e testare app (in documentazione pubblica tipicamente **≤3 console**), non a gestire una GPU farm.

### Soluzione architetturale

- Compute shader **HLSL** su **DirectX 12** per matmul, **RMSNorm**, **RoPE** e un forward ricostruito dal codec **FLP2**.
- Pesi master **fp32** in system RAM; **chunk streaming** e **double buffering** verso la GPU.
- Baseline numerica e di tempo: **CPU ggml** (Fase 1+).
- QAT ternario/2/4-bit, **straight-through estimator (STE)**, **AdamW**, schedule **WSD** con cooldown isolati (Fasi 3–5).
- GDK / Windows SDK pubblici. **GDKX** / **ID@Xbox** sono percorsi NDA/partner: **non** ne rivendichiamo l’accesso.

Dettaglio: [docs/architecture.md](docs/architecture.md), [docs/adr/0001-architecture.md](docs/adr/0001-architecture.md). Fatti piattaforma (Microsoft/Xbox pubblici, non banchi nostri): [Dev Mode](docs/platform/dev-mode.md), [risorse UWP](docs/platform/uwp-resources.md), [DirectX 12 / HLSL](docs/platform/dx12-hlsl-compute.md), [ambito DirectML](docs/platform/directml-scope.md), [GDK vs GDKX](docs/platform/gdk-vs-gdkx.md), [Series S vs X](docs/platform/series-s-vs-x.md), [blocchi Fase 6](docs/platform/blockers-fase6-validation.md).

### Roadmap (Fase 0–7)

Playbook Cursor: [docs/execution-plan.md](docs/execution-plan.md). Tabella completa: [ROADMAP.md](ROADMAP.md). Milestone GitHub: [elenco](https://github.com/gianlucamazza/xbox-gpu-training/milestones).

| Fase | Milestone | Label |
| --- | --- | --- |
| 0 Setup ambiente | [Fase 0 — Setup ambiente](https://github.com/gianlucamazza/xbox-gpu-training/milestone/1) | `phase-0` |
| 1 Kernel HLSL di base | [Fase 1 — Kernel HLSL di base](https://github.com/gianlucamazza/xbox-gpu-training/milestone/2) | `phase-1` |
| 2 Forward FLP2 su GPU | [Fase 2 — Forward FLP2 su GPU](https://github.com/gianlucamazza/xbox-gpu-training/milestone/3) | `phase-2` |
| 3 Backward + AdamW | [Fase 3 — Backward + AdamW](https://github.com/gianlucamazza/xbox-gpu-training/milestone/4) | `phase-3` |
| 4 Streaming memoria | [Fase 4 — Streaming memoria](https://github.com/gianlucamazza/xbox-gpu-training/milestone/5) | `phase-4` |
| 5 QAT completo WSD | [Fase 5 — QAT completo WSD](https://github.com/gianlucamazza/xbox-gpu-training/milestone/6) | `phase-5` |
| 6 Validazione Series S\|X | [Fase 6 — Validazione Series S\|X](https://github.com/gianlucamazza/xbox-gpu-training/milestone/7) | `phase-6` |
| 7 Pubblicazione | [Fase 7 — Pubblicazione](https://github.com/gianlucamazza/xbox-gpu-training/milestone/8) | `phase-7` |

### Stato attuale

Fase 6: **`BLOCKED: no console`** — nessun kit Dev Mode in questa lane; GDKX / ID@Xbox non rivendicati. Pagina: [docs/console.md](docs/console.md) (deploy / PIX / tabella vuota). I quattro blocker restano veri: [docs/platform/blockers-fase6-validation.md](docs/platform/blockers-fase6-validation.md). Fase 5 resta il massimo host (`--qat-smoke --steps 16`). Setup: [docs/setup.md](docs/setup.md). **Nessun tok/s o banco Series S|X inventato.** xllama **non** modificato. Il smoke `benchmarks/run_smoke.py` resta stub.

### Come contribuire

[CONTRIBUTING.md](CONTRIBUTING.md). Evidence-first. Inglese per codice e commit; italiano ok nelle discussioni.

---

## English

### Why it exists

On Xbox Series S|X **Dev Mode** there is **no CUDA**; **DirectML** on console is inference/forward-focused and is **not** the trainer here; a UWP **App** typically has **~1 GB** RAM ( **Game** designation ~**5 GB** ); the memory bus and **AppContainer** constrain weights; the Dev Mode purpose licence is to develop and test apps (public docs typically **≤3 consoles**), not to operate a GPU farm.

### Architectural solution

- **HLSL** compute shaders on **DirectX 12** for matmul, **RMSNorm**, **RoPE**, and a forward reconstructed from the **FLP2** codec.
- **fp32** master weights in system RAM; **chunk streaming** and **double buffering** to the GPU.
- Numerical and timing baseline: **CPU ggml** (Fase 1+).
- Ternary / 2-bit / 4-bit **QAT**, **straight-through estimator (STE)**, **AdamW**, **WSD** with isolated cooldowns (Fasi 3–5).
- Public GDK / Windows SDK. **GDKX** / **ID@Xbox** are NDA/partner paths: this repo does **not** claim access.

See [docs/architecture.md](docs/architecture.md) and [docs/adr/0001-architecture.md](docs/adr/0001-architecture.md). Public Microsoft/Xbox fact packs (not our benches): [Dev Mode](docs/platform/dev-mode.md), [UWP resources](docs/platform/uwp-resources.md), [DirectX 12 / HLSL](docs/platform/dx12-hlsl-compute.md), [DirectML scope](docs/platform/directml-scope.md), [GDK vs GDKX](docs/platform/gdk-vs-gdkx.md), [Series S vs X](docs/platform/series-s-vs-x.md), [Fase 6 blockers](docs/platform/blockers-fase6-validation.md).

### Roadmap (phases 0–7)

Cursor playbook: [docs/execution-plan.md](docs/execution-plan.md). Full table: [ROADMAP.md](ROADMAP.md). GitHub milestones: [index](https://github.com/gianlucamazza/xbox-gpu-training/milestones).

| Phase | Milestone | Label |
| --- | --- | --- |
| 0 Environment setup | [Fase 0 — Setup ambiente](https://github.com/gianlucamazza/xbox-gpu-training/milestone/1) | `phase-0` |
| 1 Base HLSL kernels | [Fase 1 — Kernel HLSL di base](https://github.com/gianlucamazza/xbox-gpu-training/milestone/2) | `phase-1` |
| 2 FLP2 forward on GPU | [Fase 2 — Forward FLP2 su GPU](https://github.com/gianlucamazza/xbox-gpu-training/milestone/3) | `phase-2` |
| 3 Backward + AdamW | [Fase 3 — Backward + AdamW](https://github.com/gianlucamazza/xbox-gpu-training/milestone/4) | `phase-3` |
| 4 Memory streaming | [Fase 4 — Streaming memoria](https://github.com/gianlucamazza/xbox-gpu-training/milestone/5) | `phase-4` |
| 5 Full QAT + WSD | [Fase 5 — QAT completo WSD](https://github.com/gianlucamazza/xbox-gpu-training/milestone/6) | `phase-5` |
| 6 Series S\|X validation | [Fase 6 — Validazione Series S\|X](https://github.com/gianlucamazza/xbox-gpu-training/milestone/7) | `phase-6` |
| 7 Publish results | [Fase 7 — Pubblicazione](https://github.com/gianlucamazza/xbox-gpu-training/milestone/8) | `phase-7` |

### Current status

Fase 6: **`BLOCKED: no console`** — no Dev Mode kit in this lane; GDKX / ID@Xbox not claimed. Page: [docs/console.md](docs/console.md) (deploy / PIX / empty table). The four blockers stay accurate: [docs/platform/blockers-fase6-validation.md](docs/platform/blockers-fase6-validation.md). Fase 5 remains the host ceiling (`--qat-smoke --steps 16`). Setup: [docs/setup.md](docs/setup.md). **No invented tok/s or Series S|X benches.** xllama **not** modified. `benchmarks/run_smoke.py` remains a stub.

### How to contribute

[CONTRIBUTING.md](CONTRIBUTING.md). Evidence-first. English for code and commits; Italian is fine in discussions.

---

## Layout

| Path | Role |
| --- | --- |
| `src/hlsl/` | HLSL compute shaders (`hello_compute` Fase 0; `matmul` Fase 1; RMSNorm / RoPE / FLP2 Fase 2; FakeQuant / grad / STE Fase 3). Fase 4–5 add none. |
| `src/cpp/` | C++ host (`xbox_gpu_host`) — DX12 hello + matmul + `--forward-fixture` + `--grad-check` / `--train-step` + `--stream-stress` + `--qat-smoke` |
| `docs/setup.md` | Toolchain + run notes |
| `docs/ggml-baseline.md` | How the CPU GEMM compares; ggml is not vendored |
| `docs/flp2-forward.md` | Fase 2 decode contract, tolerances, envelope non-goals |
| `docs/ste-adamw.md` | Fase 3 FakeQuant / STE / AdamW contract + grad-check tolerances |
| `docs/memory-budget.md` | Fase 4 App ~1 GB / Game ~5 GB streaming contract |
| `docs/qat-wsd.md` | Fase 5 QAT bit-widths + WSD + isolated cooldowns (N=16) |
| `docs/console.md` | Fase 6 Dev Mode deploy / PIX / `BLOCKED: no console` table |
| `docs/`, `docs/adr/` | Architecture + ADRs (`0001`, `0002`) |
| `docs/platform/` | Public Xbox / Dev Mode / GDK / UWP fact packs |
| `docs/execution-plan.md` | Cursor phase playbook |
| `benchmarks/` | Smoke stub + Fase 1 CSV + Fase 2 `fixtures/tiny_flp2.json` + Fase 4 `fixtures/stream_stress.json` |
| `examples/hello-compute/` | Hello compute host (`hello_compute`) |
| `examples/qat-wsd-smoke.json` | Fase 5 QAT/WSD schedule (isolated cooldown overlay) |
| `.github/workflows/ci.yml` | **CI** — jobs `lint-docs`, `build-windows`, `notify-failure` |
| `.github/workflows/benchmark.yml` | **Benchmark** — job `benchmark` |

## License

[MIT](LICENSE).
