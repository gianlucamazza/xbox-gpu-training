# UWP resource allocation on Xbox

Planning budgets from Microsoft Learn. These are **not** measured working-set numbers from this repo.

Primary source:

- [System resources for UWP apps and games on Xbox](https://learn.microsoft.com/en-us/windows/uwp/xbox-apps/system-resource-allocation)

The page covers **Xbox One and Xbox Series X|S**.

## Foreground memory

| Classification | Maximum available memory (foreground) |
| --- | --- |
| UWP **Apps** | **1 GB** |
| Xbox Live **Creators Program** games | **5 GB** |

Background apps: **≤128 MB**. Background mode applies to concurrent applications (for example background music). Games are suspended and terminated in the background.

In this repo, “**~1 GB**” means the **App** class. **5 GB** is the Creators **game** class. Whether an unpublished research package can use the Game / Creators classification is **uncertain** — do not assume Game designation for an App package.

## Debugger can mask OOM

Microsoft: *“When running your app or game from the Visual Studio debugger, these memory constraints do not apply. This limit is only applicable when not running in debugging mode.”*

The **non-debug** package is the gate. A debug session that does not OOM is **not** evidence that the App 1 GB or Creators 5 GB cap holds.

Exceeding the cap causes memory allocation failures. Only non-debug Release measurements count; E0 peak app memory is in [status.md](../status.md).

## Architecture requirement

All apps and games must target **x64** to be developed or submitted to the store for Xbox (same Learn page).

## CPU / GPU share (same page)

This is the single copy of the table in this repository:

- **Apps:** share of **2–4 CPU cores** (depends on what else is running); **~45%** shared GPU.
- **Games:** **4 exclusive + 2 shared** CPU cores; **full access to available GPU** cycles.

App Mode is **not** full title GPU. See [dx12-hlsl-compute.md](dx12-hlsl-compute.md).

## Related

- [dev-mode.md](dev-mode.md) — Dev Mode purpose and limits
- [console-constraints.md](console-constraints.md) — memory constraint in context
- [diagnostic/memory-budget.md](../diagnostic/memory-budget.md) — diagnostic-lane streaming against the App budget
