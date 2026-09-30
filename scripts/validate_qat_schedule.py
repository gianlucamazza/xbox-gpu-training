#!/usr/bin/env python3
"""Validate xbox-gpu-training.qat.wsd.v1 and dry-run the LR table.

Hand-rolled (no jsonschema dependency). Isolated cooldowns must not overlap.
Does not invent loss, tok/s, or quality.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCHEMA_ID = "xbox-gpu-training.qat.wsd.v1"
ADR_BASE_LR = 1.0e-3
ADR_BETA1 = 0.9
ADR_BETA2 = 0.999
ADR_EPS = 1.0e-8
ADR_WD = 0.01
HYPER_TOL = 1.0e-8


def _close(a: float, b: float, tol: float = HYPER_TOL) -> bool:
    return abs(float(a) - float(b)) <= tol


def lerp(a: float, b: float, i: int, n: int) -> float:
    if n <= 1:
        return float(b)
    return float(a) + (float(b) - float(a)) * (i / (n - 1))


def wsd_length(cfg: dict) -> int:
    w = cfg["wsd"]
    return int(w["warmup_steps"]) + int(w["stable_steps"]) + int(w["decay_steps"])


def wsd_lr(cfg: dict, step: int) -> float:
    w = cfg["wsd"]
    opt = cfg["optimizer"]
    warmup = int(w["warmup_steps"])
    stable = int(w["stable_steps"])
    decay = int(w["decay_steps"])
    base = float(opt["base_lr"])
    min_lr = float(w["min_lr"])
    if step < warmup:
        return lerp(0.0, base, step, warmup)
    if step < warmup + stable:
        return base
    if step < warmup + stable + decay:
        return lerp(base, min_lr, step - warmup - stable, decay)
    return min_lr


def cooldown_at(cfg: dict, step: int) -> dict | None:
    for cd in cfg["cooldowns"]:
        start = int(cd["start_step"])
        n = int(cd["steps"])
        if start <= step < start + n:
            return cd
    return None


def lr_at(cfg: dict, step: int) -> float:
    cd = cooldown_at(cfg, step)
    if cd is None:
        return wsd_lr(cfg, step)
    start = int(cd["start_step"])
    n = int(cd["steps"])
    start_lr = wsd_lr(cfg, start)
    return lerp(start_lr, float(cd["end_lr"]), step - start, n)


def phase_at(cfg: dict, step: int) -> str:
    cd = cooldown_at(cfg, step)
    if cd is not None:
        return f"cooldown:{cd.get('id', '')}"
    w = cfg["wsd"]
    warmup = int(w["warmup_steps"])
    stable = int(w["stable_steps"])
    decay = int(w["decay_steps"])
    if step < warmup:
        return "warmup"
    if step < warmup + stable:
        return "stable"
    if step < warmup + stable + decay:
        return "decay"
    return "after-wsd"


def validate(cfg: dict) -> list[str]:
    err: list[str] = []
    if not isinstance(cfg, dict):
        return ["root must be an object"]
    if cfg.get("schema") != SCHEMA_ID:
        err.append(f"schema must be {SCHEMA_ID}")
    if not isinstance(cfg.get("name"), str) or not cfg["name"]:
        err.append("name must be a non-empty string")

    qat = cfg.get("qat")
    if not isinstance(qat, dict):
        err.append("qat must be an object")
    else:
        if qat.get("master") != "fp32":
            err.append("qat.master must be fp32")
        bw = qat.get("bit_width")
        if bw not in {"ternary", "2", "4", "2bit", "4bit"}:
            err.append("qat.bit_width must be ternary|2|4|2bit|4bit")
        if "scale_grad" in qat and qat["scale_grad"] is not False:
            err.append("qat.scale_grad must be false (s is a constant in the backward)")

    opt = cfg.get("optimizer")
    if not isinstance(opt, dict):
        err.append("optimizer must be an object")
    else:
        if opt.get("type") != "adamw":
            err.append("optimizer.type must be adamw")
        if opt.get("device", "host") != "host":
            err.append("optimizer.device must be host")
        try:
            base = float(opt["base_lr"])
            if base <= 0:
                err.append("optimizer.base_lr must be > 0")
            if not _close(base, ADR_BASE_LR):
                err.append("optimizer.base_lr must stay ADR 0002 1e-3")
            if not _close(float(opt["beta1"]), ADR_BETA1):
                err.append("optimizer.beta1 must stay ADR 0002 0.9")
            if not _close(float(opt["beta2"]), ADR_BETA2):
                err.append("optimizer.beta2 must stay ADR 0002 0.999")
            if not _close(float(opt["eps"]), ADR_EPS):
                err.append("optimizer.eps must stay ADR 0002 1e-8")
            if not _close(float(opt["weight_decay"]), ADR_WD):
                err.append("optimizer.weight_decay must stay ADR 0002 0.01")
        except (KeyError, TypeError, ValueError):
            err.append("optimizer is missing numeric ADR 0002 fields")

    wsd = cfg.get("wsd")
    if not isinstance(wsd, dict):
        err.append("wsd must be an object")
        return err
    for key in ("warmup_steps", "stable_steps", "decay_steps"):
        try:
            if int(wsd[key]) < 1:
                err.append(f"wsd.{key} must be >= 1")
        except (KeyError, TypeError, ValueError):
            err.append(f"wsd.{key} missing or invalid")
    if wsd.get("decay") != "linear":
        err.append("wsd.decay must be 'linear' (cosine is not implemented)")
    try:
        min_lr = float(wsd["min_lr"])
        if min_lr < 0:
            err.append("wsd.min_lr must be >= 0")
        if min_lr > float(opt.get("base_lr", ADR_BASE_LR)):
            err.append("wsd.min_lr must be <= optimizer.base_lr")
    except (KeyError, TypeError, ValueError):
        err.append("wsd.min_lr missing or invalid")

    cds = cfg.get("cooldowns")
    if not isinstance(cds, list) or len(cds) < 1:
        err.append("cooldowns must be a non-empty array (isolation is mandatory)")
        return err

    length = wsd_length(cfg)
    windows: list[tuple[int, int, str]] = []
    for i, cd in enumerate(cds):
        if not isinstance(cd, dict):
            err.append(f"cooldowns[{i}] must be an object")
            continue
        try:
            start = int(cd["start_step"])
            steps = int(cd["steps"])
            end_lr = float(cd["end_lr"])
        except (KeyError, TypeError, ValueError):
            err.append(f"cooldowns[{i}] needs start_step, steps, end_lr")
            continue
        ident = str(cd.get("id") or f"cooldown-{i}")
        if start < 0 or steps < 1:
            err.append(f"{ident}: start_step>=0 and steps>=1")
        if end_lr < 0:
            err.append(f"{ident}: end_lr must be >= 0")
        if start + steps > length:
            err.append(f"{ident}: window [{start},{start + steps}) exceeds WsdLength {length}")
        windows.append((start, start + steps, ident))

    windows.sort()
    for a, b in zip(windows, windows[1:]):
        if a[1] > b[0]:
            err.append(f"cooldown overlap: {a[2]} [{a[0]},{a[1]}) vs {b[2]} [{b[0]},{b[1]})")

    smoke = cfg.get("smoke")
    if not isinstance(smoke, dict):
        err.append("smoke must be an object")
    else:
        try:
            n = int(smoke["steps"])
            if n < 1:
                err.append("smoke.steps must be >= 1")
            entered = any(int(cd["start_step"]) < n for cd in cds if isinstance(cd, dict) and "start_step" in cd)
            if not entered:
                err.append("smoke.steps must cross at least one cooldown start_step")
            if n > length:
                err.append(f"smoke.steps {n} exceeds WsdLength {length}")
        except (KeyError, TypeError, ValueError):
            err.append("smoke.steps missing or invalid")
    return err


def format_table(cfg: dict, steps: int) -> str:
    lines = [
        f"schema={cfg.get('schema')} name={cfg.get('name')}",
        f"N={steps} WsdLength={wsd_length(cfg)} decay=linear min_lr={cfg['wsd']['min_lr']}",
        f"bit_width={cfg['qat'].get('bit_width')} master=fp32",
        "step  phase                    lr",
    ]
    for s in range(steps):
        lines.append(f"{s:4d}  {phase_at(cfg, s):<22}  {lr_at(cfg, s):.8e}")
    lines.append("Not a quality curve. Not a tok/s result. Not a console result.")
    return "\n".join(lines)


def self_check_overlap() -> int:
    """Inline bad config must be rejected (no extra fixture file)."""
    bad = {
        "schema": SCHEMA_ID,
        "name": "overlap-reject",
        "qat": {"master": "fp32", "bit_width": "ternary", "scale_grad": False},
        "optimizer": {
            "type": "adamw",
            "base_lr": ADR_BASE_LR,
            "beta1": ADR_BETA1,
            "beta2": ADR_BETA2,
            "eps": ADR_EPS,
            "weight_decay": ADR_WD,
            "device": "host",
        },
        "wsd": {
            "warmup_steps": 4,
            "stable_steps": 8,
            "decay_steps": 4,
            "decay": "linear",
            "min_lr": 1.0e-4,
        },
        "cooldowns": [
            {"id": "a", "start_step": 6, "steps": 4, "end_lr": 1.0e-5},
            {"id": "b", "start_step": 8, "steps": 4, "end_lr": 1.0e-5},
        ],
        "smoke": {"steps": 16},
    }
    errs = validate(bad)
    if not any("overlap" in e for e in errs):
        print("FAILED: overlap self-check did not reject overlapping cooldowns")
        for e in errs:
            print(f"  - {e}")
        return 1
    print("overlap self-check: rejected overlapping cooldowns")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "config",
        nargs="?",
        default=str(ROOT / "examples" / "qat-wsd-smoke.json"),
        help="path to qat-wsd JSON (default: examples/qat-wsd-smoke.json)",
    )
    parser.add_argument("--dry-run", action="store_true", help="print the LR table after validation")
    parser.add_argument("--steps", type=int, default=0, help="override smoke.steps for the table")
    args = parser.parse_args()

    path = Path(args.config)
    if not path.is_file():
        print(f"FAILED: missing config {path}")
        return 1
    try:
        cfg = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        print(f"FAILED: JSON parse: {exc}")
        return 1

    errs = validate(cfg)
    if errs:
        print("FAILED: qat-schedule validation")
        for e in errs:
            print(f"  - {e}")
        return 1

    steps = args.steps if args.steps > 0 else int(cfg["smoke"]["steps"])
    print(f"STATUS: qat-schedule validated ({path.as_posix()})")
    if args.dry_run:
        print(format_table(cfg, steps))
        print("STATUS: qat-schedule dry-run ok")
    if self_check_overlap() != 0:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
