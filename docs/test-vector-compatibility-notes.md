# Test Vector Compatibility Notes

This note captures compatibility findings from local generated FFmpeg FFV1
test vectors. The generated vector header is intentionally ignored by git, so
these observations should be treated as implementation guidance rather than
repository test data.

## Current Local Vector Status

The local `testvectors/test_vector_data.hpp` set currently contains:

- `range_intra_gray10_1slice.mkv`
- `range_intra_420p10_1slice.mkv`
- `range_intra_420p10_2x2.mkv`
- `range_intra_420p8_1slice.mkv`
- `gr_intra_gray8_1slice_flat.mkv`
- `gr_intra_gray8_2x2_flat.mkv`

The previous `smptebars_*` local set showed that 8-bit Golomb-Rice vectors
parse their slice footers and CRC correctly, but slice 0 reaches a
Golomb-Rice run-state mismatch:

- `smptebars_inter_420p.mkv` and `smptebars_intra_420p.mkv` report
  `Golomb-Rice run extends beyond plane end at bit offset 411 plane=0` with
  `plane_end_bits=0:411,1:0,2:0`, `run_states=0:24/61,1:0/0,2:0/0`, and
  `pending_runs=0:y118x125c0i0n(180/180/180/180/0/180)p180+256b410-411r24>24q221`.
- `smptebars_intra_444p.mkv` reports the same plane-0 overrun at bit offset
  411 with
  `pending_runs=0:y118x125c0i0n(180/180/180/180/0/180)p180+256b410-411r24>24q221`.
- `smptebars_intra_gray.mkv` reports the same plane-0 overrun at bit offset
  411 with `plane_end_bits=0:411`, `run_states=0:24/61`, and
  `pending_runs=0:y118x125c0i0n(191/191/191/191/0/191)p191+256b410-411r24>24q221`.
- `smptebars_intra_yuva.mkv` reports the same plane-0 overrun at bit offset
  411 with
  `pending_runs=0:y118x125c0i0n(180/180/180/180/0/180)p180+256b410-411r24>24q221`.
- The second frame of `smptebars_inter_420p.mkv` depends on the first frame
  succeeding before reference slice state can be available.

The current requested compatibility vectors now all pass configuration parsing
and reach frame decode. Version 3 frames use the range-coded slice
header/footer path even when the stream has a 1x1 slice grid; treating a v3
single-slice payload as legacy whole-slice content skips the slice header and
misplaces the content boundary.

After resetting the range scalar context scope between the first-slice
`keyframe` symbol and the version 3 Slice Header, the focused vectors moved
from content offset 2 to content offset 3 and the single-plane range and flat
Golomb-Rice vectors pass the generated-vector decode test.

The range-coded 4:2:0 vectors then required one additional correction: v3
range-coded Slice Content uses scalar context banks by Slice Header
quantization-table index slot, not by coded plane. This means Cb and Cr share
the chroma slot's range context bank. With that behavior, all vectors in the
current local set decode through the public API and match the generated output
planes.

The generated-vector comparison now ignores output stride padding and checks
only active row bytes. Padding bytes are caller-owned output storage, not a
codec reconstruction mismatch.

The focused vectors previously failed before or during quantization-table
parsing. Deferring application of custom `state_transition_delta` until after
the full Parameter section and using independent scalar states per individual
QuantizationTable moved all focused vectors past configuration parsing. The
subsequent Slice Header context reset and Slice Content slot-bank fixes moved
the current range-coded focused vectors to full frame reconstruction.

## Confirmed Behaviors

### Slice Size Semantics

The FFmpeg-generated v3 EC slice footers in the local vectors use
`slice_size` as the number of bytes before the footer, not the number of bytes
including the footer. Walking `payload_0_0` backward with that interpretation
finds four coherent slices and each full slice has CRC remainder zero.

Changing `slice_size` handling to include the footer makes external vector
inspection fail immediately with CRC mismatch and is not compatible with these
vectors.

### Golomb-Rice QSet Context Sharing

Golomb-Rice context state sharing by quantization-table-set bank improved the
external vector failure mode from early underflow to slice-end mismatch. Keep
this as the current baseline unless later evidence proves otherwise.

### Slice-End Validation Probe

Temporarily bypassing Golomb-Rice padding and trailing-byte validation allows
slice 0 to continue, but later top-row slices still fail with bitstream
underflow near rows 3 to 5. This suggests the current mismatch is not only an
overly strict end-padding check; sample or run-code consumption is already
diverging before the end of at least some slices.

The decoder carries a Golomb-Rice full-run remainder across row boundaries but
now discards any remaining pending run at the end of a plane. This matches the
useful behavior exposed by the FFmpeg gray ramp vector without treating a final
plane-edge run as malformed.

The v3 Golomb-Rice Slice Header no longer reads or writes an extra range-coded
termination sentinel before Slice Content. The RFC Slice pseudocode has no
symbol between `SliceHeader()` and `SliceContent()`, and removing the sentinel
lets the generated single-plane gray ramp vector decode successfully. The
parsed content byte offset still includes the range coder's lookahead byte, so
mffv1's own encoder/decoder round trips continue to agree on the located GR
content boundary.

Two direct content-offset probes were tried and rejected:

- Adding six bytes to the parsed content offset moves the 8-bit vectors to a
  later plane-0 overrun with all-zero neighborhoods and does not decode. This
  is not a valid interpretation of the six-byte gap observed in the gray
  vector.
- Subtracting one byte from the parsed content offset to model Sentinel mode's
  "one byte beyond" wording is no longer the selected model. Removing the
  extra sentinel read is the useful boundary correction; byte-position
  subtraction by itself either hides the range coder lookahead or misaligns
  multi-plane vectors.

### Parameter Custom Transition Scope

FFmpeg-generated coder type 2 configuration records encode
`state_transition_delta` with the default transition table and continue using
that default transition table for the rest of the Parameter section. Applying
the decoded custom transition table immediately after the 255 deltas makes
later Parameter fields diverge, for example by decoding 10-bit 4:2:0 vectors
as 8-bit streams with impossible subsampling and slice-grid values.

The parser now applies the custom transition table only after the Parameter
section has been read successfully. This also avoids mutating the caller's
range reader when a later Parameter field is malformed.

### QuantizationTable Context Scope

The focused FFmpeg vectors require a fresh independent scalar context scope
for each individual `QuantizationTable`. Reading all five tables under one
shared `QuantizationTableSet` scope leaves impossible context counts such as
`113699822`; per-table scopes produce valid context counts such as `365` and
move every focused vector past configuration parsing.

### Version 3 Slice Header Context Scope

RFC 9043 gives the Frame `keyframe` symbol and Slice Header their own initial
range states. The real range-coded parse/decode/encode paths now write or read
the first-slice `keyframe` symbol, then reconfigure scalar contexts to a fresh
single default-initialized Slice Header context before processing the Slice
Header. Slice Content still carries the arithmetic coder state forward from
the Slice Header, but reconfigures scalar contexts from the selected
quantization-table-set indexes.

This change is required by the current FFmpeg-generated vectors: without it,
the first Slice Header consumes only two bytes and later sample decoding
diverges earlier. With it, the content offset for the focused v3 range vectors
is three bytes and single-plane focused vectors pass.

### Version 3 Range Slice Content Context Banks

Version 3 range-coded Slice Content maps scalar context banks by Slice Header
quantization-table index slot. For YCbCr with chroma planes, luma uses slot 0
and both chroma planes use slot 1. Extra planes use their own slot according to
`plane_quant_table_set_index_slot()`. The implementation now allocates range
context banks from `quant_table_set_index_count(stream)` for v3 and maps each
plane to the appropriate slot when reading or writing symbols.

This is distinct from mapping banks by the quantization-table-set value itself.
The focused vectors use `qidx=0,0`; sharing by qset value would make luma and
chroma share one bank and fails during range scalar decoding. Sharing by slot
keeps luma separate while sharing Cb/Cr, which matches the current vectors.

## Rejected Hypotheses

The following experiments did not improve external-vector compatibility and
should not be repeated without new evidence:

- Treating a version 3 1x1 slice grid as a legacy single-slice payload with no
  range-coded slice header/footer.
- Treating v3 EC `slice_size` as including the footer.
- Removing the range-coded termination sentinel before v3 Golomb-Rice content.
- Sharing Golomb-Rice run state by quantization-table-set bank.
- Dropping full-run pending carry across row boundaries.
  Rechecking this with final run-state diagnostics lets the gray slice 0
  advance, but makes multi-plane vectors underflow early in plane 1 and is not
  a valid fix. RFC 9043's run-mode pseudo-code also supports carrying a
  remaining `run_count` across the row when a full run reaches the row end.
- Resetting Golomb-Rice run state on every `prepare_golomb_rice` call; this
  breaks existing legacy non-keyframe reference-state tests.
- For the 10-bit range-coded configuration, forcing the alternative state
  transition table after reading deltas.
- Applying the custom state transition table immediately after
  `state_transition_delta` within the Parameter section.
- Decoding `state_transition_delta` with per-state or 256 independent
  parameter contexts.
- Accepting `quant_table_set_count == 0` as if it were 1.
- Incrementally applying `state_transition_delta` while reading the custom
  state transition table.
- Feeding the full Configuration Record, including the CRC parity bytes, to the
  range decoder instead of only the pre-CRC parameter payload.
- Separating the range coder's binary `br` state from scalar state 0. This
  worsens the focused vector parse and contradicts the unit tests that pin
  binary symbols and scalar context 0 to the same range state.
- Sharing one independent scalar context scope across all five
  QuantizationTables in a QuantizationTableSet.
- Sharing range-coded Slice Content scalar context banks by
  quantization-table-set value instead of Slice Header slot. This makes the
  current 4:2:0 vectors fail during range scalar decoding because their luma
  and chroma slots both reference qset 0 but must keep separate context banks.
- Switching v3 slice header/content decoding back to the default state
  transition table. The external vectors then fail while parsing the slice
  header, for example with `slice header rectangle is outside the slice raster
  byte=2`.
- Switching only range-coded Slice Content back to the default state transition
  after parsing the v3 Slice Header. Internal v3 tests still pass, but external
  vectors move to earlier or more numerous range scalar failures, so this is
  not compatible with the current FFmpeg vectors.
- Reading a range termination sentinel between a v3 Slice Header and
  range-coded Slice Content. This breaks the internal multi-slice range decode
  test and worsens external vectors.

## Next Investigation Targets

The remaining 8-bit Golomb-Rice mismatch is now isolated to the generated
4:2:0 ramp vector. The single-plane gray ramp vector decodes successfully.
The 4:2:0 vector still underflows in Y plane scalar decoding around row 29, so
the next investigation should focus on multi-plane GR bit consumption rather
than the generic v3 Slice Header boundary.

Additional probing on `gr_intra_420p8_1slice_ramp.mkv` showed that the first
observable Y-plane mismatch happens before the eventual underflow: row 1,
x=87 reconstructs as 107 while the generated plane expects 106. At that point
the decoder is reading a Golomb-Rice run interruption with prediction 145 and
context-0 state `drift=0,error_sum=19,bias=1,count=2`; the decoded value is
therefore biased from -39 to -38. The preceding context-0 update is the first
row's initial +16 run interruption. This makes context-0 VLC bias handling
during run interruption the current highest-value investigation target.

Two direct probes were rejected during that investigation:

- Resetting Golomb-Rice run state at every line breaks the passing gray ramp
  vector and is not compatible with current generated vectors.
- Removing the output effect of context bias from run-interruption decoding
  fixes the first x=87 mismatch but introduces a later row-1 mismatch around
  x=233 and does not restore full vector compatibility.

- The exact update order of Golomb-Rice context state during run interruption.
- The transition between run mode and scalar mode after a derived context
  changes near a row boundary.
- A context-model state scope that differs between the RFC description and
  FFmpeg's v3 Golomb-Rice encoder output.
- A YCbCr 4:2:0-specific slice-content bit consumption issue before chroma
  decoding begins.

When investigating, prefer small experiments that report the byte/bit position
at the end of each plane in slice 0. The decoder now includes the slice-local
bit offset and plane end offsets in Golomb-Rice padding and trailing-byte
errors. Avoid relaxing padding or trailing-byte validation as a fix; doing so
only hides the bitstream-position mismatch or sample-consumption mismatch.

The current focused range-coded vectors now pass. Future range-coded
compatibility work should add less uniform vectors with nonzero chroma
gradients, explicit extra planes, and nonzero quantization-table-set indexes
before changing generic range nonbinary decoding again.

The binary `ConfigurationRecordParser` now initializes the Parameter range
reader with all 32 scalar contexts so `states_coded == 1` can decode
`initial_state_delta[i][j][k]` with `k` as the context index. This improves v3
range configuration coverage and is part of the current passing baseline.

The generated-vector test now reports the first byte and sample mismatch per
plane instead of dumping whole buffers, and includes stream transition,
initial-state summaries, and slice qidx values in mismatch diagnostics. Keep
these diagnostics available for future vector additions even though the current
focused range-coded vectors now pass.

One additional slice-content boundary probe was tried and rejected: after
validating a version 3 range-coded Slice Header, resetting the range decoder on
`content_payload` instead of carrying the arithmetic state forward breaks the
internal v3 decoder tests and moves external vectors to earlier range scalar
failures. Keep the current model where Slice Header and range-coded Slice
Content share the arithmetic state while scalar contexts are reconfigured by
Slice Header quantization-table index slot.
