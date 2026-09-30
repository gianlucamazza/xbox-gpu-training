#!/usr/bin/env python3
"""Generate benchmarks/fixtures/tiny_flp2.json from the documented Fase 2 contract.

This is a host-side fixture builder. It is not a tok/s result and does not
modify gianlucamazza/xllama. Math matches src/cpp/cpu_flp2.cpp.
"""

from __future__ import annotations

import json
import math
from pathlib import Path

SEQ = 4
D = 8
HEADS = 2
D_FF = 16
VOCAB = 8
LEVELS = 3
EPS = 1.0e-6
THETA = 10000.0
DH = D // HEADS


def decode(symbols: list[int], scales: list[float], rows: int, cols: int) -> list[list[float]]:
    half = (LEVELS - 1) / 2.0
    out = []
    for r in range(rows):
        row = []
        for c in range(cols):
            row.append((symbols[r * cols + c] - half) * scales[r])
        out.append(row)
    return out


def pattern_symbols(rows: int, cols: int, seed: int) -> list[int]:
    return [((r * cols + c) * 7 + seed) % LEVELS for r in range(rows) for c in range(cols)]


def rmsnorm(x: list[float], gamma: list[float]) -> list[float]:
    acc = sum(v * v for v in x) / len(x)
    inv = 1.0 / math.sqrt(acc + EPS)
    return [v * inv * g for v, g in zip(x, gamma)]


def rope_vec(v: list[float], t: int) -> list[float]:
    y = v[:]
    for p in range(DH // 2):
        freq = THETA ** (-(2 * p) / DH)
        ang = t * freq
        c, s = math.cos(ang), math.sin(ang)
        x0, x1 = v[2 * p], v[2 * p + 1]
        y[2 * p] = x0 * c - x1 * s
        y[2 * p + 1] = x0 * s + x1 * c
    return y


def linear(x: list[float], w: list[list[float]]) -> list[float]:
    out = []
    for row in w:
        out.append(sum(a * b for a, b in zip(x, row)))
    return out


def softmax(xs: list[float]) -> list[float]:
    m = max(xs)
    e = [math.exp(v - m) for v in xs]
    s = sum(e)
    return [v / s for v in e]


def main() -> None:
    tokens = [1, 0, 3, 2]
    emb_s = pattern_symbols(VOCAB, D, 3)
    qkv_s = pattern_symbols(3 * D, D, 5)
    proj_s = pattern_symbols(D, D, 11)
    fc_s = pattern_symbols(D_FF, D, 13)
    fc2_s = pattern_symbols(D, D_FF, 17)
    emb_sc = [0.5] * VOCAB
    qkv_sc = [0.5] * (3 * D)
    proj_sc = [0.5] * D
    fc_sc = [0.25] * D_FF
    fc2_sc = [0.25] * D
    gamma1 = [1.0] * D
    gamma2 = [1.0] * D
    gammaf = [1.0] * D

    w_emb = decode(emb_s, emb_sc, VOCAB, D)
    w_qkv = decode(qkv_s, qkv_sc, 3 * D, D)
    w_proj = decode(proj_s, proj_sc, D, D)
    w_fc = decode(fc_s, fc_sc, D_FF, D)
    w_fc2 = decode(fc2_s, fc2_sc, D, D_FF)

    hidden = [w_emb[t][:] for t in tokens]
    xn = [rmsnorm(h, gamma1) for h in hidden]
    qkv = [linear(x, w_qkv) for x in xn]
    q = [row[:D] for row in qkv]
    k = [row[D : 2 * D] for row in qkv]
    v = [row[2 * D :] for row in qkv]

    attn = [[0.0] * D for _ in range(SEQ)]
    scale = 1.0 / math.sqrt(DH)
    for t in range(SEQ):
        for h in range(HEADS):
            sl = slice(h * DH, (h + 1) * DH)
            qh = rope_vec(q[t][sl], t)
            scores = []
            for s in range(t + 1):
                kh = rope_vec(k[s][sl], s)
                scores.append(sum(a * b for a, b in zip(qh, kh)) * scale)
            a = softmax(scores)
            for i in range(DH):
                attn[t][h * DH + i] = sum(a[s] * v[s][h * DH + i] for s in range(t + 1))

    hidden = [[h + p for h, p in zip(hrow, linear(arow, w_proj))] for hrow, arow in zip(hidden, attn)]
    xn = [rmsnorm(h, gamma2) for h in hidden]
    ff = []
    for x in xn:
        h = linear(x, w_fc)
        ff.append([(v if v > 0.0 else 0.0) ** 2 for v in h])
    hidden = [[h + p for h, p in zip(hrow, linear(frow, w_fc2))] for hrow, frow in zip(hidden, ff)]
    yn = [rmsnorm(h, gammaf) for h in hidden]
    logits = [linear(y, w_emb) for y in yn]
    flat = [v for row in logits for v in row]

    fixture = {
        "schema": "xbox-gpu-training.fixture.flp2.v1",
        "name": "tiny_flp2",
        "note": (
            "Tiny CPU-reference fixture for Fase 2. Scalar FLP2 decode "
            "(ternary + row16) + RMSNorm + RoPE + 1-layer relu2 forward. "
            "Not a binary FLP2 envelope. Not a tok/s result. xllama not modified."
        ),
        "config": {
            "seq": SEQ,
            "d": D,
            "n_heads": HEADS,
            "d_ff": D_FF,
            "vocab": VOCAB,
            "n_layers": 1,
            "mlp": "relu2",
            "core_fmt": "ternary",
            "emb_fmt": "ternary",
            "scale_policy": "row16",
            "qk_norm": False,
            "levels": LEVELS,
            "rope_theta": THETA,
            "rms_eps": EPS,
        },
        "tolerance": {"max_abs": 1.0e-5, "max_rel": 1.0e-4},
        "tokens": tokens,
        "emb": {
            "rows": VOCAB,
            "cols": D,
            "levels": LEVELS,
            "symbols": emb_s,
            "scales": emb_sc,
        },
        "blocks": [
            {
                "norm1": gamma1,
                "qkv": {
                    "rows": 3 * D,
                    "cols": D,
                    "levels": LEVELS,
                    "symbols": qkv_s,
                    "scales": qkv_sc,
                },
                "proj": {
                    "rows": D,
                    "cols": D,
                    "levels": LEVELS,
                    "symbols": proj_s,
                    "scales": proj_sc,
                },
                "norm2": gamma2,
                "fc": {
                    "rows": D_FF,
                    "cols": D,
                    "levels": LEVELS,
                    "symbols": fc_s,
                    "scales": fc_sc,
                },
                "fc2": {
                    "rows": D,
                    "cols": D_FF,
                    "levels": LEVELS,
                    "symbols": fc2_s,
                    "scales": fc2_sc,
                },
            }
        ],
        "norm": gammaf,
        "expected": {"logits": flat},
    }

    out = Path(__file__).resolve().parent / "fixtures" / "tiny_flp2.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(fixture, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {out} logits={len(flat)}")


if __name__ == "__main__":
    main()
