# RFC 9043 Implementation Notes

This note collects mffv1 findings that may be useful when reading or
implementing RFC 9043. It is not a replacement for the RFC and does not define
the public mffv1 API. It records black-box compatibility observations, internal
tests, and clean-room implementation decisions.

mffv1 must remain independent from FFmpeg source code. The observations below
come from RFC text, generated bitstreams, generated expected planes, and mffv1
unit tests.

## Confirmed Implementation Interpretations

### Version 3 Single-Slice Frames Still Use Slice Syntax

Version 3 frames use the version 3 Slice Header, Slice Content, and Slice
Footer structure even when the slice raster is 1x1. Treating a version 3 1x1
frame as a legacy whole-frame payload skips the Slice Header and misplaces the
content boundary.

### Slice Size Is Payload Before Footer

For version 3 slices, `slice_size` is interpreted as the number of bytes before
the Slice Footer. The footer itself is located after those bytes. This
interpretation gives coherent multi-slice payload walking and CRC verification
for the local generated vectors.

### Range-Coded Slice Header Has Fresh Scalar Contexts

The first-slice `keyframe` symbol and the Slice Header use separate initial
range-state scopes. After reading or writing `keyframe`, mffv1 reconfigures
scalar contexts to one fresh default-initialized Slice Header context. Slice
Content then continues the arithmetic coder state from the Slice Header, while
reconfiguring scalar contexts according to the selected quantization-table
indexes.

### Range Slice Content Uses Quant-Table Index Slots

Version 3 range-coded Slice Content maps scalar context banks by Slice Header
quantization-table index slot, not by coded plane and not by the literal
quantization-table-set value. For YCbCr, luma uses slot 0, Cb and Cr share slot
1, and an optional extra plane uses slot 2. For RGB/RGBA, coded RCT Y uses slot
0, coded Cb and Cr share slot 1, and alpha uses slot 2.

### Golomb-Rice Adaptive State Is Slot-Local

Version 3 Golomb-Rice adaptive VLC context state follows the same slot-local
model as range-coded Slice Content. RGB/RGBA decoding additionally requires the
Golomb-Rice run state to progress in coded line-plane order, while VLC context
state remains split by quant-table index slot.

### No Range Termination Sentinel Between Header And Content

The version 3 Slice pseudocode places `SliceContent()` immediately after
`SliceHeader()`. mffv1 does not read or write an additional range-coded
termination sentinel between a range-coded Slice Header and Golomb-Rice Slice
Content. The range coder's normal byte-position behavior still determines the
content byte boundary.

### Custom State Transition Applies After Parameters

For `coder_type == 2`, `state_transition_delta` is decoded using the current
Parameter-section range state. The decoded custom transition table is applied
only after the full Parameter section has been read successfully. Applying it
immediately after the 255 deltas causes later Parameter fields to diverge on
local generated vectors.

### Quantization Tables Use Per-Table Context Scope

Each individual `QuantizationTable` is read with its own independent scalar
context scope. Sharing one scalar context scope across all five tables in a
`QuantizationTableSet` produces invalid context counts on generated vectors.

### Legacy Version 0 Embedded Parameters Carry Quant Tables

For version 0/1 legacy keyframes, embedded `Parameters()` may be present in the
frame payload. Version 0 embedded Parameters carry a quantization-table set in
the same structural position as version 1. Treating version 0 as an implicit
zero-table stream creates false early content boundaries and breaks local
legacy vectors.

## Compatibility Boundaries

### Legacy Multi-Slice

Stable, widely used multi-slice behavior is associated with version 3. Version
0/1 range-coded multi-slice parsing exists in mffv1 as defensive parser
coverage. Version 0/1 Golomb-Rice multi-slice frames are reported as
`UnsupportedFeature` unless real compatibility evidence justifies a dedicated
path.

### Legacy Bootstrap Is A Container-Adapter Contract

Legacy AVI-style payloads may have empty Codec Private data. mffv1 therefore
provides `bootstrap_legacy_frame()` so a caller can initialize from
keyframe-embedded Parameters after supplying external frame dimensions.
Parameter changes discovered in later legacy keyframes are reported as stream
events, not as failed `Status` values.

## Open Clarification Candidates

These are not proposed changes yet. They are useful topics to keep isolated if
future public compatibility work or RFC feedback becomes appropriate.

- Clarify in implementation guidance that version 3 1x1 slice grids still use
  the version 3 slice header/footer path.
- Clarify whether `slice_size` should be described explicitly as bytes before
  the Slice Footer.
- Clarify the intended range-state and scalar-context reset boundary between
  the first-slice `keyframe` symbol and the version 3 Slice Header.
- Clarify that Slice Content context banks are selected by Slice Header
  quant-table index slot.
- Clarify legacy version 0 keyframe-embedded Parameter handling, especially
  the presence of quantization tables.

## Evidence Policy

Before turning a note into an external specification proposal, require:

- a minimal generated vector or hand-built bitstream that isolates the issue;
- an mffv1 unit test that pins the behavior without relying on FFmpeg source;
- confirmation that the behavior is not merely an mffv1 internal artifact;
- license/provenance notes for any external media or generated data used.
