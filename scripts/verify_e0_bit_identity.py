#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path

IGNORED = {
    "dispatches",
    "gpu_seconds",
    "transfer_bytes",
    "peak_memory_bytes",
    "adapter",
    "wall_seconds",
    "loss_series",
}


def clean(value):
    if isinstance(value, dict):
        return {k: clean(v) for k, v in value.items() if k not in IGNORED}
    if isinstance(value, list):
        return [clean(v) for v in value]
    return value


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical(value):
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), allow_nan=False
    ).encode()


def main():
    ap = argparse.ArgumentParser(
        description="Compare accepted E0 numerical payloads, branch bytes and checkpoint state."
    )
    ap.add_argument("--reference", type=Path, required=True)
    ap.add_argument("--reference-benchmark", type=Path, required=True)
    ap.add_argument("--reference-lineage", type=Path, required=True)
    ap.add_argument("--candidate", type=Path, required=True)
    ap.add_argument("--benchmark", type=Path, required=True)
    ap.add_argument("--lineage", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    a = ap.parse_args()
    ref = a.reference
    reference_lineage = a.reference_lineage
    accepted = json.loads((ref / "acceptance.json").read_text())
    candidate = json.loads((a.candidate / "acceptance.json").read_text())
    old_line = json.loads(reference_lineage.read_text())
    new_line = json.loads(a.lineage.read_text())
    for report, lineage in ((accepted, old_line), (candidate, new_line)):
        if not report["ok"] or any(
            report[k] != lineage[k if k == "package" else "source_commit"]
            for k in ("package", "commit")
        ):
            raise RuntimeError("acceptance does not bind the package lineage")
    if len(accepted["fixtures"]) != len(candidate["fixtures"]):
        raise RuntimeError("fixture matrix changed")
    cases = {}
    case_hashes = {}
    for name in ["kernels", "optimizer"] + [
        f"fixture-{i:02d}" for i in range(len(accepted["fixtures"]))
    ]:
        left = list((ref / name).glob("*actual*.json"))
        right = list((a.candidate / name).glob("*actual*.json"))
        if len(left) != 1 or len(right) != 1:
            raise RuntimeError("missing actual case " + name)
        left_payload = canonical(clean(json.loads(left[0].read_text())))
        right_payload = canonical(clean(json.loads(right[0].read_text())))
        cases[name] = left_payload == right_payload
        case_hashes[name] = {
            "reference": hashlib.sha256(left_payload).hexdigest(),
            "candidate": hashlib.sha256(right_payload).hexdigest(),
        }
    branches = {}
    checkpoints = {}
    for mode in ("full", "resumed"):
        for p in sorted((ref / mode).glob("branch-*.json")):
            q = a.candidate / mode / p.name
            branches[mode + "/" + p.stem] = p.read_bytes() == q.read_bytes()
        # Checkpoint JSON binds different job ids; numerical state is compared independently.
        p = ref / mode / "checkpoint.json"
        q = a.candidate / mode / "checkpoint.json"
        if not p.exists() or not q.exists():
            raise RuntimeError("missing checkpoint " + mode)
        left_state = json.loads(p.read_text())
        right_state = json.loads(q.read_text())
        keys = ("step", "stream_position", "tensors", "moments")
        checkpoints[mode] = canonical({k: left_state[k] for k in keys}) == canonical(
            {k: right_state[k] for k in keys}
        )
    if len(branches) != 6 or len(checkpoints) != 2:
        raise RuntimeError("incomplete numerical proof")
    old = json.loads((a.reference_benchmark / "result.json").read_text())
    new = json.loads((a.benchmark / "result.json").read_text())
    benchmark = {
        str(left["end_step"]): left["end_step"] == right["end_step"]
        and left["artifact"]["sha256"] == right["artifact"]["sha256"]
        for left, right in zip(old["branches"], new["branches"], strict=True)
    }
    reference_job = json.loads((a.reference_benchmark / "job/job.json").read_text())
    candidate_job = json.loads((a.benchmark / "job/job.json").read_text())
    if {k: v for k, v in reference_job.items() if k != "job_id"} != {
        k: v for k, v in candidate_job.items() if k != "job_id"
    }:
        raise RuntimeError("benchmark recipe or assets changed")
    if len(benchmark) != 3:
        raise RuntimeError("incomplete benchmark artifact proof")
    shader = (
        old_line["payloads"]["Assets/e0_tensor.cso"]
        == new_line["payloads"]["Assets/e0_tensor.cso"]
    )
    ok = (
        all(cases.values())
        and all(branches.values())
        and all(checkpoints.values())
        and all(benchmark.values())
        and shader
        and canonical(old["last_loss"]) == canonical(new["last_loss"])
    )
    report = {
        "schema": "xbox-gpu-training.e0.bit-identity.v1",
        "purpose": "functional",
        "reference_package": old_line["package"],
        "candidate_package": new_line["package"],
        "ignored_fields": sorted(IGNORED),
        "shader_cso_sha256_both": new_line["payloads"]["Assets/e0_tensor.cso"]
        if shader
        else None,
        "acceptance_cases": {
            "compared": len(cases),
            "identical": sum(cases.values()),
            "differences": {k: v for k, v in cases.items() if not v},
        },
        "acceptance_canonical_payload_sha256": case_hashes,
        "canonicalization": "sorted JSON after removing declared runtime metadata only; preserves negative zero and numeric types",
        "acceptance_branch_weights_identical": branches,
        "checkpoint_numeric_state_identical": checkpoints,
        "benchmark_branch_artifact_sha256_identical": benchmark,
        "benchmark_last_loss": [old["last_loss"], new["last_loss"]],
        "ok": ok,
    }
    a.out.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))
    if not ok:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
