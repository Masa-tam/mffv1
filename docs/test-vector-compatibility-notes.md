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
and CRC correctly, but slice 0 reaches a Golomb-Rice payload end mismatch:

- `smptebars_inter_420p.mkv` and `smptebars_intra_420p.mkv` report
  non-zero Golomb-Rice alignment padding at slice-local bit offset 1207 with
  `plane_end_bits=0:411,1:807,2:1207` and
  `run_states=0:24/61,1:22/112,2:22/32`.
- `smptebars_intra_444p.mkv` reports trailing Golomb-Rice bytes at
  slice-local bit offset 1736 with
  `plane_end_bits=0:411,1:1110,2:1736` and
  `run_states=0:24/61,1:24/0,2:24/128`.
- `smptebars_intra_gray.mkv` reports non-zero Golomb-Rice alignment padding
  at slice-local bit offset 411 with `plane_end_bits=0:411` and
  `run_states=0:24/61`.
- `smptebars_intra_yuva.mkv` reports non-zero Golomb-Rice alignment padding
  at slice-local bit offset 1838 with
  `plane_end_bits=0:411,1:1110,2:1736,3:1838` and
  `run_states=0:24/61,1:24/0,2:24/128,3:24/156`.
- The second frame of `smptebars_inter_420p.mkv` depends on the first frame
  succeeding before reference slice state can be available.

The 10-bit vector currently fails during configuration parsing:

- `quant_table_set_count must be in the range 1..8: 0 byte=155`

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

## Rejected Hypotheses

The following experiments did not improve external-vector compatibility and
should not be repeated without new evidence:

- Treating v3 EC `slice_size` as including the footer.
- Removing the range-coded termination sentinel before v3 Golomb-Rice content.
- Sharing Golomb-Rice run state by quantization-table-set bank.
- Dropping full-run pending carry across row boundaries.
  Rechecking this with final run-state diagnostics lets the gray slice 0
  advance, but makes multi-plane vectors underflow early in plane 1 and is not
  a valid fix.
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

## Next Investigation Targets

The remaining 8-bit Golomb-Rice mismatch is most likely in one of these areas:

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
or inter-plane boundaries. The final run-state diagnostics show nonzero
`pending_count` at plane end for these vectors, so any next run-mode
experiment should preserve the behavior needed by multi-plane streams while
explaining why the last full-run segment leaves unread non-padding bits. Avoid
relaxing padding or trailing-byte validation as a fix; doing so only hides the
bitstream-position mismatch.
