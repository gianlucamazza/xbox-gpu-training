# Contributing

Evidence-first research repository. Before opening a pull request read
[README.md](README.md), [docs/claims-policy.md](docs/claims-policy.md) and the doc
for the area you touch ([docs/README.md](docs/README.md)).

## Rules

- Follow the [claims policy](docs/claims-policy.md): no invented numbers; every metric
  is Sourced, UNMEASURED, BLOCKED, UNVALIDATED or a Placeholder.
- Current-state values (package, throughput, memory, gate counts) are edited only in
  [docs/status.md](docs/status.md) and evidence directories; other pages link to them.
- Architecture changes need an ADR ([docs/adr/](docs/adr/README.md)) and the `adr` label.
- E0 engine changes follow the bit-identity rule or bring fresh gates
  ([docs/e0/engine.md](docs/e0/engine.md#bit-identity-rule)).
- A new console package needs its own acceptance and an evidence directory
  ([docs/e0/acceptance.md](docs/e0/acceptance.md)).
- Do not modify [xllama](https://github.com/gianlucamazza/xllama) from this project.

- floppylm's `floppylm.*.v1` schemas and golden fixtures are vendored in `contracts/floppylm/`
  at the commit in `contracts/floppylm/PIN.json`. Never edit them here: change floppylm, then
  `python3 scripts/sync_floppylm_contracts.py --source <floppylm checkout> --commit <sha>`.
  CI (`scripts/check_floppylm_contracts.py`) checks the copy, the fixtures and every native
  report in `docs/evidence/`.

## Language and style

- Code, comments, docs, commit messages and PR titles: **English**.
- Conventional commits (`feat:`, `fix:`, `docs:`, `chore:`, `test:`, `refactor:`, `perf:`).
- Terms and spelling: [docs/glossary.md](docs/glossary.md).
- State each fact once and link to it; do not copy disclaimers between pages.

## Branches and pull requests

- Branch `<type>/<short-slug>` from the latest `main`; one PR per change.
- Labels: topic (`research`, `kernel`, `memory`, `benchmark`, `adr`).
- PR body: objective, commands run, results (paste), what was **not** done
  ([template](.github/PULL_REQUEST_TEMPLATE.md)).
- Do not merge with red CI (`lint-docs`, `build-windows`, and `E0 UWP` when E0 paths change).

## Local checks

```bash
python3 scripts/check_required_docs.py
python3 scripts/check_glossary.py
python3 scripts/check_relative_links.py
python3 scripts/check_doc_claims.py
python3 -m unittest discover -s scripts/tests
python3 scripts/validate_qat_schedule.py examples/qat-wsd-smoke.json --dry-run
cmake -S . -B build && cmake --build build
./build/xgpu_e0_train --help
```

On Windows, let CMake pick the Visual Studio generator (`cmake -S . -B build -A x64`);
do not pin `-G "Visual Studio 17 2022"`. Diagnostic-host commands:
[docs/diagnostic/setup.md](docs/diagnostic/setup.md). E0 package build and deploy:
[docs/e0/runbook.md](docs/e0/runbook.md).

## Code owners

`@gianlucamazza` owns `/*` (see `.github/CODEOWNERS`).
