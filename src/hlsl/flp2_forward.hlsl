// Tiny FLP2 reconstructed forward — Fase 2.
// One decoder block + final RMSNorm + tied embedding logits.
// MLP is relu2 (unambiguous). No QK-norm. Causal attention.
// Weights are decoded in-shader from symbols + row16 scales.
//
// Compile (Windows SDK dxc, Shader Model 6.0):
//   dxc -T cs_6_0 -E CSMain -Fo flp2_forward.cso src\hlsl\flp2_forward.hlsl
//
// Limits (tiny fixture class): Seq<=8, D<=16, Heads<=4, Dff<=32, Vocab<=16.
// One thread writes one logit (seq, vocab). Hidden state is recomputed
// per thread — acceptable only because the fixture is tiny.
//
// DirectX 12 compute shader path. No CUDA. DirectML is not the trainer.
// Not a tok/s result. Not a console result. English comments only.

cbuffer Flp2Fwd : register(b0)
{
    uint Seq;
    uint D;
    uint Heads;
    uint Dff;
    uint Vocab;
    uint Levels;
    uint EpsBits;
    uint ThetaBits;
};

StructuredBuffer<uint> Tokens : register(t0);
StructuredBuffer<uint> Symbols : register(t1);
StructuredBuffer<uint> Scales : register(t2);
StructuredBuffer<uint> Norms : register(t3);
RWStructuredBuffer<uint> Logits : register(u0);

float DecodeAt(uint sym_off, uint scale_off, uint rows, uint cols, uint r, uint c)
{
    const float halfv = ((float)Levels - 1.0f) * 0.5f;
    const float scale = asfloat(Scales[scale_off + r]);
    const float sym = (float)Symbols[sym_off + r * cols + c];
    return (sym - halfv) * scale;
}

void RmsNorm(float x[16], float g[16], uint dim, float eps, out float y[16])
{
    float acc = 0.0f;
    [loop]
    for (uint i = 0; i < dim; ++i)
    {
        acc += x[i] * x[i];
    }
    const float inv = 1.0f / sqrt(acc / (float)dim + eps);
    [loop]
    for (uint j = 0; j < dim; ++j)
    {
        y[j] = x[j] * inv * g[j];
    }
}

void RopeVec(inout float v[16], uint t, uint dh, float theta)
{
    [loop]
    for (uint p = 0; p < dh / 2; ++p)
    {
        const float freq = pow(theta, -((float)(2 * p) / (float)dh));
        const float ang = (float)t * freq;
        const float c = cos(ang);
        const float s = sin(ang);
        const float x0 = v[2 * p];
        const float x1 = v[2 * p + 1];
        v[2 * p] = x0 * c - x1 * s;
        v[2 * p + 1] = x0 * s + x1 * c;
    }
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    const uint vocab_i = dtid.x;
    const uint tok_i = dtid.y;
    if (tok_i >= Seq || vocab_i >= Vocab)
    {
        return;
    }

    const float eps = asfloat(EpsBits);
    const float theta = asfloat(ThetaBits);
    const uint dh = D / Heads;

    // Symbol / scale / norm offsets (serialization order of the tiny fixture).
    const uint emb_sym = 0;
    const uint qkv_sym = emb_sym + Vocab * D;
    const uint proj_sym = qkv_sym + (3 * D) * D;
    const uint fc_sym = proj_sym + D * D;
    const uint fc2_sym = fc_sym + Dff * D;

    const uint emb_sc = 0;
    const uint qkv_sc = emb_sc + Vocab;
    const uint proj_sc = qkv_sc + 3 * D;
    const uint fc_sc = proj_sc + D;
    const uint fc2_sc = fc_sc + Dff;

    const uint n1_off = 0;
    const uint n2_off = D;
    const uint nf_off = 2 * D;

    float hidden[8][16];
    [loop]
    for (uint t = 0; t < Seq; ++t)
    {
        const uint id = Tokens[t];
        [loop]
        for (uint d = 0; d < D; ++d)
        {
            hidden[t][d] = DecodeAt(emb_sym, emb_sc, Vocab, D, id, d);
        }
    }

    float gamma1[16];
    float gamma2[16];
    float gammaf[16];
    [loop]
    for (uint gi = 0; gi < D; ++gi)
    {
        gamma1[gi] = asfloat(Norms[n1_off + gi]);
        gamma2[gi] = asfloat(Norms[n2_off + gi]);
        gammaf[gi] = asfloat(Norms[nf_off + gi]);
    }

    float q[8][16];
    float k[8][16];
    float v[8][16];
    [loop]
    for (uint t = 0; t < Seq; ++t)
    {
        float xn[16];
        RmsNorm(hidden[t], gamma1, D, eps, xn);
        [loop]
        for (uint o = 0; o < 3 * D; ++o)
        {
            float acc = 0.0f;
            [loop]
            for (uint i = 0; i < D; ++i)
            {
                acc += xn[i] * DecodeAt(qkv_sym, qkv_sc, 3 * D, D, o, i);
            }
            if (o < D)
            {
                q[t][o] = acc;
            }
            else if (o < 2 * D)
            {
                k[t][o - D] = acc;
            }
            else
            {
                v[t][o - 2 * D] = acc;
            }
        }
    }

    float attn[8][16];
    [loop]
    for (uint t = 0; t < Seq; ++t)
    {
        [loop]
        for (uint d = 0; d < D; ++d)
        {
            attn[t][d] = 0.0f;
        }
        [loop]
        for (uint h = 0; h < Heads; ++h)
        {
            float qh[16];
            [loop]
            for (uint i = 0; i < dh; ++i)
            {
                qh[i] = q[t][h * dh + i];
            }
            RopeVec(qh, t, dh, theta);

            float scores[8];
            float m = -1.0e30f;
            [loop]
            for (uint s = 0; s <= t; ++s)
            {
                float kh[16];
                [loop]
                for (uint i = 0; i < dh; ++i)
                {
                    kh[i] = k[s][h * dh + i];
                }
                RopeVec(kh, s, dh, theta);
                float dotp = 0.0f;
                [loop]
                for (uint i = 0; i < dh; ++i)
                {
                    dotp += qh[i] * kh[i];
                }
                scores[s] = dotp / sqrt((float)dh);
                if (scores[s] > m)
                {
                    m = scores[s];
                }
            }
            float sum_e = 0.0f;
            [loop]
            for (uint s = 0; s <= t; ++s)
            {
                scores[s] = exp(scores[s] - m);
                sum_e += scores[s];
            }
            [loop]
            for (uint s = 0; s <= t; ++s)
            {
                const float a = scores[s] / sum_e;
                [loop]
                for (uint i = 0; i < dh; ++i)
                {
                    attn[t][h * dh + i] += a * v[s][h * dh + i];
                }
            }
        }
    }

    [loop]
    for (uint t = 0; t < Seq; ++t)
    {
        float residual[16];
        [loop]
        for (uint d = 0; d < D; ++d)
        {
            residual[d] = hidden[t][d];
        }
        [loop]
        for (uint o = 0; o < D; ++o)
        {
            float acc = 0.0f;
            [loop]
            for (uint i = 0; i < D; ++i)
            {
                acc += attn[t][i] * DecodeAt(proj_sym, proj_sc, D, D, o, i);
            }
            hidden[t][o] = residual[o] + acc;
        }

        float xn[16];
        RmsNorm(hidden[t], gamma2, D, eps, xn);
        float hff[32];
        [loop]
        for (uint o = 0; o < Dff; ++o)
        {
            float acc = 0.0f;
            [loop]
            for (uint i = 0; i < D; ++i)
            {
                acc += xn[i] * DecodeAt(fc_sym, fc_sc, Dff, D, o, i);
            }
            const float r = acc > 0.0f ? acc : 0.0f;
            hff[o] = r * r;
        }
        [loop]
        for (uint o = 0; o < D; ++o)
        {
            float acc = 0.0f;
            [loop]
            for (uint i = 0; i < Dff; ++i)
            {
                acc += hff[i] * DecodeAt(fc2_sym, fc2_sc, D, Dff, o, i);
            }
            hidden[t][o] = hidden[t][o] + acc;
        }
    }

    float yn[16];
    RmsNorm(hidden[tok_i], gammaf, D, eps, yn);
    float acc = 0.0f;
    [loop]
    for (uint i = 0; i < D; ++i)
    {
        acc += yn[i] * DecodeAt(emb_sym, emb_sc, Vocab, D, vocab_i, i);
    }
    Logits[tok_i * Vocab + vocab_i] = asuint(acc);
}
