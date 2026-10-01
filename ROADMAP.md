# Roadmap

Forward-looking work only. Current state: [docs/status.md](docs/status.md). Completed
phases and their outcomes: [docs/history.md](docs/history.md).

## Now — scientific E0 campaign

- [ ] Complete the sequential row16/row8log campaign on the active package with frozen
      artifact hashes (companion ADR 0011).
- [ ] Diagnose any failed native job; recover bound interrupted trials explicitly.
- [ ] Run the single reserved final test.
- [ ] Publish selection, paired statistics, exclusions and costs in
      [docs/results.md](docs/results.md).

Gate: every number from a committed evidence directory; thresholds never relaxed
after a failure ([ADR 0004](docs/adr/0004-independent-e0-gates.md)).

## Next — engine and coverage

- [ ] **E0.2 kernels** (tiled matmul, fusion, cross-sample batching). Reorders
      reductions, so it needs fresh gates and an ADR, not the bit-identity shortcut
      ([engine.md](docs/e0/engine.md#bit-identity-rule)).
- [ ] Compile `e0_tensor.hlsl` in the main CI `build-windows` job, not only in the E0 UWP workflow.
- [ ] Series X measurement (no hardware today).
- [ ] Matched CPU versus console throughput comparison (both sides measured).
- [ ] PIX capture of one E0 dispatch, if tooling allows on Dev Mode.

## Later — publication

- [ ] Peer comparison only through a preregistered matched protocol; until then peer
      cells stay literature citations ([results.md](docs/results.md#bitnet--peer-comparison)).
- [ ] Figures from committed data only ([docs/figures/](docs/figures/README.md)).

## Conventions

Branches `<type>/<slug>` from `main`; one PR per change; labels `research`, `kernel`,
`memory`, `benchmark`, `adr` (phase labels `phase-0` … `phase-7` remain for history).
Details: [CONTRIBUTING.md](CONTRIBUTING.md).
