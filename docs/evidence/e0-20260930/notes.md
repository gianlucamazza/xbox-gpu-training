# Series S E0 backend acceptance and throughput

Hardware: retail Xbox Series S, Dev Mode, OS
`26100.9438.amd64fre.xb_flt_2608ge.260902-1030`.
Release UWP package `GianlucaMazza.XgpuE0_0.1.0.11_x64__g0p5dcfz4t9z4`;
source `9744ff7dbe2b619a942e95f6209d8b5c6dd507a8`, CI run `36746732705`.
The installed executable, shader, manifest and resources match the CI entries
byte for byte. Packaging metadata and signature were regenerated with OpenAppx;
`package-lineage.json` records both hashes and payload identities.

- All 36 held-out numerical cases passed on adapter `SraKmd_arden`.
- Native AdamW passed three steps against PyTorch using identical gradients.
- Continuous and interrupted/resumed native runs reproduced every branch weight
  byte and final checkpoint step, stream position, master weights and both moments.
- The ctx=256/batch=32/d=96/layers=3/d_ff=391 synthetic-corpus trial completed
  18 training steps: 147,456 tokens / 153.38236103 seconds = 961.362 token/s.
  GPU timestamps total 13.143978 seconds; peak app memory 90,902,528 bytes.
  Host/device transfer counters total 86,828,875,776 bytes, including repeated
  buffer transfers. These counters are scoped to this job, not preceding fixtures.
- The same shape extrapolates to 32,193 seconds for the full three-branch recipe.
  This is a short-run estimate; corpus upload and Python evaluation are excluded.

These are functional correctness and throughput measurements on a synthetic
corpus. They are not quality scores or a completed scientific E0 campaign.
Fixtures contain PyTorch oracle inputs/outputs and exact hashes; raw outputs are
retained in the local path recorded in `acceptance.json`.

Earlier failures remain in local run directories: WARP under the Release debug
layer; AppContainer canonicalization outside LocalState; atomic publication conflict.
The final package corrected each layer and passed the repeated gates and benchmark.

Independent S9 diagnostics also found that tensor16 cannot preserve individual
zero rows in mixed tensors with an even-level grid. Matching the Python oracle
had concealed this protocol mismatch. Scientific submission now checks this
invariant separately. The correction proposal in the FloppyLM workspace (`docs/e0-zero-row-proposal.md`)
awaits owner acceptance; no scientific training or held-out final test has run.
