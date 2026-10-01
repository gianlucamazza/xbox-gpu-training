# Glossary

Terms and spelling used across the docs. `scripts/check_glossary.py` rejects the
claim phrasings listed in [claims-policy.md](claims-policy.md).

## Spelling

| Term                                 | Rule                                                      |
| ------------------------------------ | --------------------------------------------------------- |
| GPU, HLSL                            | Uppercase in prose; folders stay lowercase (`src/hlsl/`). |
| DirectX 12                           | First mention per page; `DX12` afterwards.                |
| compute shader                       | Two words.                                                |
| straight-through estimator (STE)     | Spell out once per page, then STE.                        |
| FLP2, QAT, WSD, RMSNorm, RoPE, AdamW | As written.                                               |
| Dev Mode, UWP, GDK                   | Public paths used here.                                   |
| GDKX / ID@Xbox                       | Only when noting the partner path we do not claim.        |
| token/s                              | Throughput unit in prose; JSON key `tokens_per_second`.   |

## Project terms

| Term                | Meaning                                                                                                                                                                                            |
| ------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **E0**              | The FloppyLM experiment-0 protocol executed natively on the console: full causal transformer forward/backward in DX12, AdamW on fp32 masters, three-branch WSD ([e0/overview.md](e0/overview.md)). |
| **E0.1**            | The GPU-resident execution engine (package `0.1.0.28`, PR #18); same kernels, bit-identical outputs.                                                                                               |
| **E0 lane**         | `src/cpp/e0/`, `src/hlsl/e0_tensor.hlsl`, `uwp/` — the active trainer.                                                                                                                             |
| **Diagnostic lane** | The Fase 0–5 Win32 host (`xbox_gpu_host`) and its tiny fixtures ([diagnostic/](diagnostic/README.md)). Historical; certifies nothing for E0.                                                       |
| **Companion**       | The FloppyLM repository (`floppy_4mb`): Python oracle, fixtures, campaign runner, FLP2 owner.                                                                                                      |
| **Acceptance**      | The hardware gate run before a package may train: 52 operation cases, 36 model fixtures, optimizer, resume ([e0/acceptance.md](e0/acceptance.md)).                                                 |
| **Lineage**         | Proof that installed payloads match the CI build byte for byte (`package-lineage.json`).                                                                                                           |
| **Trunk / branch**  | WSD stable trunk and the isolated cooldown branches ending at T, 2T, 4T.                                                                                                                           |
| **Fase N**          | Original project phases 0–7; still the GitHub milestone titles ([history.md](history.md)).                                                                                                         |

## Labels and milestones

Labels: `research`, `kernel`, `memory`, `benchmark`, `adr`, `phase-0` … `phase-7`.
Milestones keep their Italian titles **Fase 0–7** on GitHub.
