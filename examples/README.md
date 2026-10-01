# `examples/`

Small, runnable samples. Do not treat example output as a benchmark. No tok/s claims.

| Path | Status |
| --- | --- |
| `hello-compute/` | Fase 0 host (`hello_compute`) that dispatches `src/hlsl/hello_compute.hlsl` on Windows DX12, or prints `BLOCKED: no D3D12 device`. |
| `qat-wsd-smoke.json` | Fase 5 QAT/WSD schedule (`xbox-gpu-training.qat.wsd.v1`). Isolated cooldown overlay. Not a quality curve. |
| `qat-wsd.schema.json` | Documentation schema for that config. Validator: `scripts/validate_qat_schedule.py`. |

Fase 2 tiny fixture lives under [`benchmarks/fixtures/tiny_flp2.json`](../benchmarks/fixtures/tiny_flp2.json), not here. Fase 4 stream fixture: [`benchmarks/fixtures/stream_stress.json`](../benchmarks/fixtures/stream_stress.json). Fase 5 contract: [`docs/diagnostic/qat-wsd.md`](../docs/diagnostic/qat-wsd.md).

See [docs/diagnostic/setup.md](../docs/diagnostic/setup.md).
