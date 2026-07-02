# Test Vector Compatibility Notes

This note captures compatibility findings from local generated FFmpeg FFV1
test vectors. The generated vector header is intentionally ignored by git, so
these observations should be treated as implementation guidance rather than
repository test data.

## Current Local Vector Status

The local `testvectors/test_vector_data.hpp` set currently contains:

- `smptebars_inter_420p.mkv`
- `smptebars_intra_420p.mkv`
- `smptebars_intra_420p10.mkv`
- `smptebars_intra_444p.mkv`
- `smptebars_intra_gray.mkv`
- `smptebars_intra_yuva.mkv`

With the current decoder, 8-bit Golomb-Rice vectors parse their slice footers
and CRC correctly, but slice 0 reaches a Golomb-Rice run-state mismatch:

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

The 10-bit vector currently fails during configuration parsing:

- `quant_table_set_count must be in the range 1..8: 0 (version=3.4
  entropy=range colorspace=0 bits=8 chroma=1 subsample=28,0 extra=0
  slices=2x61) byte=155`

The parser has already decoded the 10-bit vector incorrectly before the
quantization-table-set count. A 420p10 stream should not report 8-bit samples,
horizontal subsampling 28, or a 2x61 slice grid. The remaining 10-bit issue is
therefore earlier than quantization-table parsing, most likely in range-coded
nonbinary symbol state progression for the Parameter section.

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

The decoder now rejects a nonzero Golomb-Rice run `pending_count` at plane end
before byte-alignment validation. This converts the earlier padding or trailing
byte failures into a direct plane-0 run overrun at bit 411. The pending-run
trace is identical across the 8-bit vectors: a full run of 256 samples starts
at plane 0, row 118, x 125, encoded by bit range 410..411 with run index 24.
The changed diagnostic does not fix compatibility; it narrows the next
investigation to why the current plane-0 decode enters run mode at that
position and accepts the one-bit full-run segment.

The richer pending-run trace shows context 0, no difference inversion, and a
flat reconstructed neighborhood at the failing coordinate. This makes an
unexpected prediction-context decision less likely than a slice content
boundary mismatch. In the gray vector, the failing GR reader byte is absolute
byte 54 while the located footer starts at byte 60, exactly six bytes later.
Keep `slice_size` as excluding the footer; the current open question is whether
the v3 range-coded slice header termination/finalization leaves additional
bytes before the Golomb-Rice payload begins.

Two direct content-offset probes were tried and rejected:

- Adding six bytes to the parsed content offset moves the 8-bit vectors to a
  later plane-0 overrun with all-zero neighborhoods and does not decode. This
  is not a valid interpretation of the six-byte gap observed in the gray
  vector.
- Subtracting one byte from the parsed content offset to model Sentinel mode's
  "one byte beyond" wording makes the 8-bit vectors fail much earlier with
  bitstream underflow. With the current `RangeCoder::byte_position()` contract,
  the value returned after reading the termination sentinel remains the best
  local content offset.

## Rejected Hypotheses

The following experiments did not improve external-vector compatibility and
should not be repeated without new evidence:

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
- Treating a whole quantization table set as sharing one range context while
  parsing the configuration record.
- Decoding `state_transition_delta` with per-state or 256 independent
  parameter contexts.
- Accepting `quant_table_set_count == 0` as if it were 1.
- Incrementally applying `state_transition_delta` while reading the custom
  state transition table.
- Feeding the full Configuration Record, including the CRC parity bytes, to the
  range decoder instead of only the pre-CRC parameter payload.

## Next Investigation Targets

The remaining 8-bit Golomb-Rice mismatch is most likely in one of these areas:

- The v3 range-coded slice header to Golomb-Rice payload boundary, especially
  termination and range-coder finalization byte accounting.
- The exact update order of Golomb-Rice context state during run interruption.
- The transition between run mode and scalar mode after a derived context
  changes near a row boundary.
- A context-model state scope that differs between the RFC description and
  FFmpeg's v3 Golomb-Rice encoder output.
- A slice-content bit consumption issue after samples decode but before
  byte-alignment padding is checked.

When investigating, prefer small experiments that report the byte/bit position
at the end of each plane in slice 0. The decoder now includes the slice-local
bit offset and plane end offsets in Golomb-Rice padding and trailing-byte
errors. The single-plane gray vector reaches the same first-plane endpoint
(`0:411`) and then sees non-zero padding, so the next target should be
Golomb-Rice symbol or run-code consumption rather than only chroma plane order
or inter-plane boundaries. The final run-state diagnostics show
`run_states=0:24/61` at plane 0 end for these vectors, so any next run-mode
experiment should explain why the last full-run segment leaves 61 samples of
pending run after the plane's last row. The pending-run origin is now known,
including the flat neighborhood and prediction value. Prefer investigating the
range-coded header/content boundary before changing generic run-count carry
behavior. Avoid relaxing padding or trailing-byte validation as a fix; doing so
only hides the bitstream-position mismatch.

The 10-bit range-coded configuration mismatch should be investigated by
tracing the Parameter section one scalar at a time. The first obviously wrong
decoded field is `bits_per_raw_sample`, which currently decodes as the
compatibility value 0 and becomes 8 instead of decoding as 10.
