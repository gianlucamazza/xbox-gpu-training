# `src/cpp/e0/` — E0 trainer

Native FloppyLM E0 trainer: tensor runtime and autograd, DX12 kernel, model, job
runner and the `xgpu_e0_train` CLI. The UWP worker in [`uwp/`](../../../uwp/README.md)
links the same library.

- Code map and execution engine: [docs/e0/engine.md](../../../docs/e0/engine.md)
- Job files and schemas: [docs/e0/job-protocol.md](../../../docs/e0/job-protocol.md)
- Gates: [docs/e0/acceptance.md](../../../docs/e0/acceptance.md)

```bash
./build/xgpu_e0_train --reference --fixture fixture.json --out actual.json   # CPU reference
./build/xgpu_e0_train --kernel-fixture kernels.json --out actual.json         # hardware GPU
```
