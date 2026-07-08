# Test Vector Compatibility Notes

This note captures compatibility findings from local generated FFmpeg FFV1
test vectors. The generated vector header is intentionally ignored by git, so
these observations should be treated as implementation guidance rather than
repository test data.

## Current Local Vector Status

The local `testvectors/test_vector_data.hpp` set currently contains the latest
version 3.4 controls plus MKV-derived legacy Golomb-Rice probes:

- Passing Golomb-Rice vertical bars:
  `gr_rgb_bars_128x96_1slice.mkv`,
  `gr_rgb_bars_128x96_2x2.mkv`,
  `gr_rgb_bars_320x240_1slice.mkv`, and
  `gr_rgb_bars_320x240_2x2.mkv`.
- Passing Golomb-Rice RGB testsrc probes:
  `gr_rgb_testsrc_64x48_1slice.mkv`,
  `gr_rgb_testsrc_64x48_2x2.mkv`,
  `gr_rgb_testsrc_128x96_1slice.mkv`,
  `gr_rgb_testsrc_128x96_2x2.mkv`,
  `gr_rgb_testsrc_320x240_1slice.mkv`, and
  `gr_rgb_testsrc_320x240_2x2.mkv`.
- Passing Golomb-Rice YUV420p testsrc controls:
  `gr_yuv420p_testsrc_64x48_1slice.mkv`,
  `gr_yuv420p_testsrc_64x48_2x2.mkv`,
  `gr_yuv420p_testsrc_128x96_1slice.mkv`,
  `gr_yuv420p_testsrc_128x96_2x2.mkv`,
  `gr_yuv420p_testsrc_320x240_1slice.mkv`, and
  `gr_yuv420p_testsrc_320x240_2x2.mkv`.
- Passing range-coded testsrc controls:
  `range_rgb_testsrc_128x96_1slice.mkv`,
  `range_rgb_testsrc_128x96_2x2.mkv`,
  `range_rgb_testsrc_320x240_1slice.mkv`, and
  `range_rgb_testsrc_320x240_2x2.mkv`.
- Passing high-bit, alpha, CRC, and MKV legacy additions:
  `gr_rgba_testsrc2_2x2.mkv`,
  `gr_yuva_testsrc2_1slice.mkv`,
  `gr_rgb10_mandelbrot_2x2.mkv`,
  `gr_yuv420p10le_mandelbrot_1slice.mkv`,
  `range_rgb10_testsrc2_2x2.mkv`,
  `range_rgba10_testsrc2_1slice.mkv`,
  `range_yuv420p_inter_64x48_1slice_2frames.mkv`,
  `gr_yuv420p_inter_64x48_1slice_2frames.mkv`,
  `range_rgb_inter_64x48_1slice_2frames.mkv`,
  `gr_rgb_inter_64x48_1slice_2frames.mkv`,
  `range_rgb_mandelbrot_inter_64x48_2frames.mkv`,
  `range_gray_v1_legacy_1slice.mkv`,
  `range_yuv420p_v1_legacy_1slice.mkv`,
  `gr_gray_v1_legacy_1slice.mkv`,
  `gr_yuv420p_v1_legacy_1slice.mkv`,
  `range_gray_v0_legacy_1slice_1x1.mkv`,
  `range_gray_v0_legacy_1slice_4x1.mkv`,
  `range_gray_v0_legacy_1slice_32x16.mkv`,
  `range_gray_v0_legacy_1slice_16x1_nonzero.mkv`,
  `gr_gray_v0_legacy_1slice_1x1.mkv`,
  `gr_gray_v0_legacy_1slice_4x1.mkv`,
  `gr_gray_v0_legacy_1slice_16x1.mkv`,
  `gr_rgb10_testsrc_1slice.mkv`,
  `gr_rgb10_testsrc_2x2.mkv`,
  `gr_rgba10_testsrc_1slice.mkv`,
  `gr_yuva10le_testsrc_1slice.mkv`,
  `gr_yuv420p10_testsrc_1slice.mkv`, and
  `gr_yuv420p10_testsrc_2x2.mkv`.

This set now passes through the public generated-vector test. The earlier RGB
Golomb-Rice testsrc failure was not plain size growth: RGB bars passed at
320x240 and 2x2 slices, matching range-coded RGB testsrc controls passed, and
YUV420p Golomb-Rice testsrc controls also passed. The active issue was
therefore isolated to RGB/RCT Golomb-Rice run interruption context selection
inside more complex residual patterns.

Before the fix, the 64x48 one-slice testsrc vector reported a first traced
coded-chroma divergence at plane 1, `x=17,y=1`: expected internal chroma
`511`, reconstructed `393`, around a magenta RGB region. The run started in
context 0, but the interruption sample's current neighbors derived context
242. Decoding the interruption with context 242's adaptive VLC state produced
the expected small residual; reusing the old context 0 state drove `k` too
high and produced a large chroma residual.

Three local probes were rejected while checking the larger RGB GR vectors:

- Making RGB Golomb-Rice run state independent per coded plane worsened the
  one-slice vector to an immediate coded-chroma mismatch at `x=0,y=0`.
- Resetting the shared RGB Golomb-Rice run state once per RGB output row moved
  the failure but still diverged before completing the frame; it is not a
  sufficient compatibility rule.
- Using a neutral `1 << bits_per_raw_sample` border for RGB/RCT chroma worsened
  the first coded-chroma sample. This reinforces the current zero-border model
  for RGB/RGBA RCT planes.

Encoder and decoder now mirror the derived-interruption-context rule,
including context inversion for the derived interruption context. Future RGB
Golomb-Rice vectors should stress new shapes instead of repeating the same
testsrc failure: alpha, high bit depth, non-keyframes, and nontrivial slice
grids are now more valuable than additional RGB 8-bit testsrc sizes.

The MKV-derived legacy v0/v1 Golomb-Rice RGB and YUV444 vectors confirm that
the legacy bootstrap path also handles containers that omit Codec Private data
for those versions. The generated header exposes those entries with empty
configuration records and keyframe payloads containing embedded parameters,
which matches the intended public API flow through `bootstrap_legacy_frame()`.

The current generated `*_inter*.mkv` entries now expose two frame payloads and
two expected-plane sets per vector, so they exercise the public decoder's
reference-state continuity across one non-keyframe.

Several previously rejected probes remain useful guardrails:

- Clearing pending Golomb-Rice runs at row boundaries breaks the gray ramp and
  is not compatible with the passing controls. Full-run remainders may carry
  across rows, but a run segment that reaches the end of a plane is clipped
  there instead of creating a pending run beyond the plane.
- Golomb-Rice borders are zero, including YCbCr chroma and RGB/RGBA RCT
  chroma. Flat chroma therefore starts as a residual from predictor zero, not
  from a neutral 128 baseline.
- Version 3 Golomb-Rice adaptive VLC contexts are slot-local: coded luma/RCT Y
  uses slot 0, coded Cb and Cr share slot 1, and an optional extra/alpha plane
  uses slot 2. Sharing by literal quantization-table-set value or by coded
  plane breaks the current external-vector baseline.

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

### Golomb-Rice Slot-Local Context State

For version 3 Golomb-Rice content, adaptive VLC context state is local to the
Slice Header quantization-table index slot rather than to the literal
quantization-table-set value or every coded plane. For YCbCr, luma uses slot 0,
Cb and Cr share slot 1, and an optional extra plane uses slot 2. For RGB/RGBA,
the coded RCT Y plane uses slot 0, coded Cb and Cr share slot 1, and alpha uses
slot 2. This is the current passing baseline for generated 4:2:0 and RGB
Golomb-Rice vectors.

The RGB Golomb-Rice vectors exposed this because black vectors need the run
index to continue in line-plane order across the three coded RCT planes, while
colored vectors need the adaptive VLC context state split by quantization-table
index slot. Sharing all RGB VLC state by the literal qset value decodes the
first RCT Y row but shifts the first coded chroma sample; making every RGB
plane fully independent fixes one chroma plane but breaks mffv1's internal
round trips. The compatible model is therefore shared run state in RGB line
order plus slot-local VLC context state.

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
- Suppressing the `run_index` increment when a full run ends exactly at the
  row boundary. This does not move the refreshed flat vector's first mismatch
  (`y=4,x=0`, +1 luma), so the RFC `x + run_count <= w` condition remains the
  implemented behavior.
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

## Historical Investigation Notes

The following notes are retained to document rejected hypotheses and the path
to the current passing local-vector baseline. At the time of this investigation,
the remaining 8-bit Golomb-Rice mismatch was isolated to the generated 4:2:0
ramp vector. The single-plane gray ramp vector decoded successfully. The 4:2:0
vector still underflowed in Y plane scalar decoding around row 29, so the
investigation focused on multi-plane GR bit consumption rather than the generic
v3 Slice Header boundary.

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

The refreshed local Golomb-Rice 4:2:0 set contains:

- `gr_intra_420p8_1slice_flat.mkv`
- `gr_intra_420p8_2x2_flat.mkv`
- `gr_intra_420p8_1slice_smptebars.mkv`
- `gr_intra_420p8_2x2_smptebars.mkv`
- `gr_intra_420p8_1slice_ygrad_uvflat.mkv`
- `gr_intra_420p8_1slice_yflat_uvgrad.mkv`

These vectors use `qidx=1,1`. With the current internal slice-header boundary
model, they parse as `content=4` and fail from the first Y sample. Temporarily
subtracting one byte from the v3 Golomb-Rice content offset changes them to
`content=3`, which aligns the first samples but then exposes the existing
run-interruption/bias divergence around row 4 and later underflow. That
byte-position subtraction also breaks mffv1's internal Golomb-Rice
encoder/decoder round-trip tests, so it is not a valid standalone fix.

The flat vectors also exposed that a Golomb-Rice VLC context can legitimately
rescale `error_sum` to zero after long zero-residual runs. This is now accepted
by the context validator; negative `error_sum` remains invalid.

The refreshed flat and SMPTE vectors also confirm that treating the range
read-ahead byte as Golomb-Rice content with bit offsets other than zero makes
the 4:2:0 failures occur earlier. The alternate boundary therefore remains
byte-aligned. Diagnostic partial-output runs show the first stable luma
mismatch at the start of row 4 (`y=4,x=0`, actual one sample above or below
expected depending on the vector), before the later underflow.

Additional diagnostics on the flat vector show that the first stable mismatch
is not a run-interruption symbol. It is a scalar symbol with neighbors
`l=t=tl=tr=T=126` and `L=0`, which is the RFC border model's additional left
column. With the selected qset 1, `Q3[L-l]` is `Q3[-126] = -1210`, producing
`context=1210` with sign inversion. qset 0 has `Q3[-126] = 0`, but forcing GR
content to qset 0 makes the refreshed vectors diverge later and more broadly,
so the selected qset 1 remains the correct interpretation. Updating VLC
context state from the biased decoded difference instead of the pre-bias `v`
also worsens the vectors, leaving the RFC state update order intact.
Resetting Golomb-Rice VLC context state at every decoded line, even as a coarse
experiment combined with run-state reset, also worsens the refreshed vectors;
the remaining issue is not explained by a simple per-line VLC scope.
Flipping the Q3 gradient from the RFC's `L-l` direction to `l-L` also worsens
the refreshed vectors and is not compatible with the pinned border-context
unit test.
Changing the leftmost sample's `L` neighbor from the RFC additional-zero
border column to the shifted left-border sample also worsens the refreshed
vectors. It removes the later `x=0,y=4` scalar-context symptom, but causes an
earlier flat/SMPTE mismatch at `x=1,y=2` after a run interruption, so the
RFC border model remains the current baseline.

The RFC 9043 Slice syntax places `SliceContent()` immediately after
`SliceHeader()` and defines Golomb-Rice padding only after the content. The
observed FFmpeg vectors behave as if the byte already read ahead by the range
decoder is the first Golomb-Rice content byte. Moving the parser boundary from
`header_reader.byte_position()` to `header_reader.byte_position() - 1` changes
the refreshed vectors to `content=3` and removes the first-sample mismatch, but
it is not safe as a standalone change: mffv1's current encoder finalizes the
range-coded header with an additional stabilizing byte and appends Golomb-Rice
content after that byte. Simply popping or overwriting this byte before
appending content breaks internal Golomb-Rice round trips because the byte is
part of the range header's decodability, not disposable padding.

The next implementation step should therefore treat this as a range-header
finalization problem rather than a parser-only off-by-one. A conforming mixed
range-header/Golomb-Rice encoder needs to finalize the range-coded header so
that the following content byte can serve as the range decoder read-ahead byte,
or the decoder needs a safe alternate-boundary path that does not expose partial
output/state when the first boundary attempt fails.

The decoder now implements that safe alternate-boundary path in the slice
executor. For v3 Golomb-Rice slices, the normal parsed boundary is tried first
against a temporary output frame; if that fails, `content_byte_offset - 1` is
tried from the same input state. Only a successful candidate is copied to the
caller output and committed to the slice reference state. This preserves
mffv1-generated streams while allowing FFmpeg-style read-ahead boundaries to
reach the deeper Golomb-Rice run/context mismatch. With the refreshed vectors,
the first-sample mismatch disappears and the remaining failures are underflow
or alignment errors after several decoded rows.
The TestVector diagnostics now annotate failed fallback attempts with the
actual candidate content byte offset; the refreshed vectors confirm that the
remaining failures happen while decoding the `content_byte_offset=3` alternate
candidate, even though the parsed Slice Header still reports `content=4`.
The external-vector failure path also performs a low-level partial decode into
test-owned storage so the first reconstructed-sample mismatch is visible even
when the public API correctly withholds failed fallback output. With untouched
slice areas ignored, both the 1-slice and 2x2 flat/SMPTE vectors report their
first luma mismatch at `x=0,y=4`; the y-gradient control still differs earlier
at `x=0,y=1`.
The partial mismatch diagnostic now also reports reconstructed neighbor samples
from both the partial output and expected plane. The refreshed GR vectors show
matching neighbor values at the first mismatch, so the next useful probes
should focus on Golomb-Rice scalar/VLC symbol consumption or context state,
not on a decoded-sample neighbor history mismatch.
The diagnostic also reports the median predictor and residual at the first
partial mismatch. The flat vectors now show `pred=126`, expected `diff=0`, and
actual `diff=1`; the SMPTE vectors show `pred=180`, expected `diff=0`, and
actual `diff=-1`. This confirms the first visible mismatch is a one-step
residual decode difference under matching predictor input.
Golomb-Rice scalar and run-interruption failures now include the adaptive VLC
context as `gr_state=drift/error_sum/bias/count`. The refreshed vectors show
that later underflow can occur with different adaptive states, for example
`-2/8/-1/3` in the 1-slice flat vector and `0/4/0/1` in the 1-slice SMPTE
vector, so future probes should compare the first divergent symbol against the
state that led to it rather than only the final underflow location.
The internal GR sample observer now records the first expected-output mismatch
while decoding. The flat, SMPTE, and y-flat/u-v-gradient refreshed vectors all
first diverge on a scalar symbol at `x=0,y=4`, `context=1210`, with
`state_before=0/4/0/3` and `state_after=-1/5/0/4`; the bit positions differ by
slice size, but the adaptive state and context are identical. The y-gradient
control diverges earlier on a run fill at `x=0,y=1`, which points to a
separate run-length interpretation issue for that vector.
The traced common mismatch now also reports the Rice parameter and consumed
bit string. Those scalar mismatches consume `11` with `k=1`, which is folded
value 1 under the current Golomb-Rice signed mapping. Since the expected
residual is zero, the remaining issue is unlikely to be the median predictor
itself; the next probes should test whether this border position should have
remained in run mode, whether a preceding run segment consumed the wrong
number of bits, or whether the signed mapping differs for this scalar context.
Consuming a carried pending run before deriving the next context removes the
pending run from the common flat/SMPTE mismatch (`run_before` becomes `*/0`),
but the first scalar mismatch still consumes `11` with `k=1`. This makes the
pending-run carry a correctness cleanup rather than the root cause of the
shared border mismatch.
The run reader now also carries `prefix=0` remainder counts across row
boundaries when the decoded remainder is larger than the remaining samples in
the current row. This carries only the zero-run count; it does not force the
run-interruption symbol itself to cross the row boundary. Carrying the
interruption flag as well was tested and rejected because it made mffv1's own
Golomb-Rice encoder output fail and moved the refreshed vectors to an earlier
false run-interruption mismatch.
The traced mismatch now also reports context gradients and quantized terms.
The common flat/SMPTE scalar mismatch has gradients such as
`0/0/0/-126/0`, `0/0/0/-128/0`, or `0/0/0/-180/0`, and the only nonzero term
is the RFC border `Q3[L-l]` contribution (`-1210` for flat controls and
`+1210` for SMPTE controls). This confirms that the `context=1210` decision
is produced entirely by the additional left border column while predictor
inputs and expected residual remain zero. The next useful probe should
therefore compare preceding bit consumption and run/scalar transition timing
around that border context, not the quantization-table index selection itself.
With interrupted remainder carry limited to the zero-run count, the refreshed
flat vectors advance through luma and first diverge at the start of chroma:
plane 1 predicts zero while the generated vector expects 128 for flat chroma.
The SMPTE vectors also move past the earlier left-border scalar mismatch and
now diverge later in luma. The next investigation should therefore focus on
per-plane Golomb-Rice state/baseline handling and chroma plane initialization,
not the previous `x=0,y=4` luma border context.

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

### Legacy Version 0 Golomb-Rice Probe

The following legacy v0 notes are retained as investigation history. They were
superseded by the later finding that version 0 embedded `Parameters()` include
a quant table set; see the final paragraphs of this section for the current
compatibility status.

The AVI-derived legacy v0 Golomb-Rice vectors were initially skipped. A focused
probe on `gr_gray_v0_legacy_1slice.avi` showed that treating the incompletely
parsed bootstrap result as embedded `Parameters()` gives `content=3`, but every
byte/bit candidate around bytes 0 through 3 diverges within the first few
samples. The best candidate observed was byte 1, bit 0, which matches eight
initial zero samples before reading a run interruption where the generated
reference remains zero.

At that point, the parser appeared to falsely accept early Golomb-Rice payload
bits as version 0 embedded parameters, or legacy v0 Golomb-Rice appeared to use
an additional boundary convention not yet modeled. Later vectors resolved this
as a missing quant-table parse rather than a separate payload-boundary rule.

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

The AVI-derived legacy v0 range vectors were also temporarily treated as
unsupported by the generated-vector public API test. Temporarily allowing
`range_gray_v0_legacy_1slice.avi` through the same path as the passing v1 range
vector reaches frame parsing and slice decoding, but the decoded plane first
diverges at `x=3,y=0`: expected zero, decoded sample `253`. The parsed stream
is gray 8-bit range-coded v0, single slice, with a computed content byte offset
of 152. This makes v0 closer than an unsupported syntax failure, but not yet a
passing compatibility profile under the then-current parser.

One v0 hypothesis was tested and rejected: reading keyframe-embedded
`Parameters()` without reconfiguring the range scalar contexts after
`keyframe` causes both real v0 and v1 AVI-derived range vectors to fail during
bootstrap parsing (`unsupported range coder type` for v0 and an invalid custom
state-transition delta for v1). The existing Parameters context
reconfiguration is therefore part of the known-good v1 path and should not be
removed to chase v0.

The next v0 range investigation should compare the exact range context state
after embedded `Parameters()` between the v1 passing vector and the v0 failing
vector, then trace the first four decoded sample differences. The current
evidence points away from a simple byte-boundary error and toward a v0-specific
range state, fixed-table, or historical encoder compatibility detail.

The generated-vector skip diagnostics now include a legacy range symbol probe.
For `range_gray_v0_legacy_1slice.avi`, replaying the bootstrap header and then
reading the first eight range-coded signed differences from context 0 yields
`0,0,0,-3,-1,-1,-12,-1`. This matches the first visible decoded mismatch at
`x=3,y=0` (`-3` reconstructs as 253 in 8-bit wraparound). The next useful
experiment is therefore to determine why FFmpeg's legacy v0 all-zero frame
does not code a zero residual at the fourth scalar under the RFC-style v0
fixed one-context table model currently used by mffv1.

The v0 skip diagnostic now also probes the matching v1 sibling vector when it
is present. For the gray and nominal yuv420p legacy range pairs, the v1 sibling
probe reads eight zero differences and reaches the end of the payload, while
the v0 probe diverges at the fourth difference. The diagnostic includes the v1
sibling's stream summary, content byte offset, and Parameters-after arithmetic
state so the passing v1 path can be compared directly against the failing v0
bootstrap state. This confirms that the probe itself follows the passing v1
decode path and that the v0 issue is not merely a test-vector output comparison
artifact.

The range probe also compares the normal content-context reset against carrying
the Parameters scalar context directly into Slice Content. Carrying the context
is worse for the v0 vectors (`0,9,1,1,-4,17,-4,-1` for the gray control), so
the current content-context reconfiguration is still the better model. The
remaining v0 mismatch is therefore not explained by accidentally resetting the
content scalar context after `Parameters()`.

A separate byte-boundary reset probe tries independent range-coder resets near
the computed content byte offset. For the v0 gray and nominal yuv420p controls,
offsets 149, 152, and 156 all decode the first four differences as zero when
the arithmetic state is discarded and the coder is reset from that byte. This
is useful as a boundary-smoke diagnostic, but it should not be treated as a
candidate fix by itself: the legacy range path is expected to continue the
arithmetic state from the keyframe and Parameters header, and the passing v1
sibling follows that carry-state path.

The shifted-state probe restores the Parameters-after arithmetic state and then
varies only the next byte position around the computed content boundary. For
the same v0 controls, every tested byte position from 150 through 154 still
decodes `0,0,0,-3`. That makes a simple off-by-one refill position unlikely:
with the current arithmetic `range/low`, the fourth scalar diverges regardless
of the nearby byte position.

The legacy range diagnostic also includes a `param_trace` field-by-field
checkpoint for keyframe-embedded `Parameters()`. The current controls use range
coder type 2, so both v0 and v1 read the 255 custom state-transition deltas
before the remaining stream fields. For the gray v0/v1 pair, the trace reaches
the same `range=0x1005` after the state-transition table but with different
`low` values (`0xdd0` for v0 and `0xd6d` for v1). The v0 stream then ends
`Parameters()` immediately after `extra_plane`, while the v1 sibling reads
`bits_per_raw_sample` and its quantization table before slice content. The
remaining investigation should therefore keep separating syntax-length effects
from the v0-specific one-context content model.

An `initial_state_probe` now scans a uniform scalar context initial state while
preserving the Parameters-after arithmetic state. For the gray and nominal
yuv420p v0 controls, the default state 128 is not among the states that decode
the first 16 sample differences as zero, while higher states such as 184, 200,
and 210 do. The scan still leaves many candidates, so it is not a fix by
itself. It does show that the v0 mismatch is sensitive to Slice Content
context-state initialization rather than only to the byte boundary or arithmetic
`range/low` carry state.

The same probe now ranks candidate initial states by how far they can decode
the first expected output plane before the first mismatch. For the gray and
nominal yuv420p v0 controls, state 255 is currently the best uniform-state
candidate but still matches only 408 of 512 luma samples before diverging at
`x=24,y=12`. This rules out a simple "use a different fixed initial state"
explanation for the v0 range mismatch. Any eventual compatibility fix likely
needs either the historical v0 context evolution behavior or another
v0-specific content model detail, not just a replacement for the default 128
state.

The ranked initial-state probe now compares Slice Content decoding under the
parsed custom state-transition table and the default transition table. For the
gray and nominal yuv420p v0 controls, the best default-transition candidate
matches only 51 of 512 luma samples, while the custom-transition candidate
matches 408. This keeps the custom transition table in the surviving model and
rules out "decode v0 Slice Content with the default transition table" as a
useful compatibility direction.

The probe also checks a swapped zero/one state-update transition derived from
the parsed custom table. That variant matches only eight luma samples before
diverging on the same v0 controls. A simple zero/one transition-update swap is
therefore not the missing historical behavior.

The initial-state ranking now separates the 32 scalar state slots into
`zero_only`, `exponent_only`, `sign_only`, and `magnitude_only` groups. On the
gray and nominal yuv420p v0 controls, changing only `states[0]` gives the same
best result as changing all 32 states (`state255=408/512`), while changing only
the exponent, sign, or magnitude groups still diverges at the fourth sample.
This narrows the useful part of the hypothesis to the first zero/non-zero
decision bit used by range-coded signed symbols.

Freezing `states[0]` after every decoded symbol does not improve the result:
the best frozen-zero candidate still matches 408 of 512 luma samples, the same
as the best mutable `zero_only` and uniform candidates. The useful signal is
therefore not "keep the zero/non-zero state fixed"; it is that the initial and
early evolution of the zero/non-zero decision state differs from the current
model.

A `zero_state_trace` for the best `state255` candidate shows `states[0]`
remaining at 255 through sample 407 and dropping to 246 exactly at the first
mismatch (`sample=408`, `x=24,y=12`). This means the candidate path is not
gradually drifting through the zero/non-zero context state. It stays maximally
biased toward zero until the arithmetic state first decodes a non-zero symbol
where the generated reference plane still expects zero.

The same trace records the arithmetic state around that transition. Sample 407
decodes zero from `range=0xd47 low=0x10` to `range=0xd39 low=0x2`; sample 408
then starts from `range=0xd39 low=0x2` and decodes `diff=1`, ending at
`range=0x380 low=0x2ed byte=153`. The next useful probe should focus on the
range split and first-bit interpretation at that exact zero/non-zero decision,
rather than on long-term scalar context drift.

The pivot split confirms why `state255` cannot keep sample 408 on the zero
path with the current arithmetic rule. In mffv1's range symbol model, the
first bit's false path means "non-zero" and its true path means "zero". At
sample 408, `range=0xd39` and `state=255` produce a non-zero/zero split of
`14/3371`; `low=0x2` falls inside the non-zero side. Ceiling or midpoint
rounding for the split would still leave a non-zero span of 13, so those simple
rounding variants do not explain the mismatch. The diagnostic reports
`need_state=256`, so no 8-bit scalar state can make the current split decode
zero at that point. This points the next investigation toward arithmetic-state
alignment or a more substantial historical split semantic, not merely a
stronger context state.

The refreshed tiny v0/v1 legacy set confirms that version 0 range-coded
all-zero gray payloads now pass through 32x16. The remaining range-coded v0
failure is isolated by `range_gray_v0_legacy_1slice_16x1_nonzero.avi`: the
generated reference is `128,0,0,128,0,...,128`, but mffv1 reconstructs the
first sample as zero. The matching v1 sibling decodes successfully, so the
generator and public decode path are usable as a black-box comparison. This
rules out a broad v0 range bootstrap failure and narrows the next range-coded
v0 work to nonzero symbol reconstruction or the v0-specific scalar context
mapping used for nonzero residuals.

The skip diagnostic now includes an `expected_residual_probe` that follows the
actual decoder's v0 arithmetic path. For the same nonzero vector, the expected
coded residuals at samples 0, 3, and 15 are `-128`, while the v0 decoder reads
`0` at each position and reconstructs zero. The contexts are all context 0
with no inversion, so the mismatch occurs before sign or prediction can matter:
the first zero/nonzero decision for a required nonzero residual takes the zero
path under the current v0 range model.

The diagnostic also probes whether the same arithmetic position can be read as
a legacy slice header. For the nonzero vector it parses as a plausible
single-slice header (`x=0 y=0 raster=1x1 qidx=0,0`) without advancing the byte
offset, only the arithmetic state. Replaying that header was tested as an
implementation experiment, but it still reconstructs the first nonzero sample
as zero. This rules out a missing same-byte legacy slice header as a complete
fix for the current v0 range-coded nonzero mismatch.
The probe now records the first residual after that candidate header too:
`exp_diff=-128`, `act_diff=0`, and `act_sample=0`. The mismatch therefore
survives both the no-header and header-replayed arithmetic positions.
Preserving the default scalar state instead of forcing v0 `state[0]=255` was
also tested and rejected: the all-zero v0 range vectors begin producing
nonzero samples at sample 3. The zero-bias override is therefore still needed
for the verified all-zero legacy v0 cases, even though it is not sufficient for
the nonzero sample vector.
The range variant probe also tried omitting the scalar zero/nonzero flag and
reading exponent/magnitude/sign directly. That `nozero_flag_probe` still
mismatches at the first sample, so the remaining difference is not explained by
a scalar coding order that simply lacks the leading zero flag.
The v1 sibling probe now emits the same expected-residual trace. It decodes the
first sample as `act_diff=-128` and `act_sample=128`, and it also reconstructs
the later nonzero samples at positions 3 and 15. This confirms that the
expected-plane data and predictor path are valid; the remaining v0 difference
is localized to the v0 range scalar/context/arithmetic-state path before the
first residual decision.
The expected-residual trace now also records arithmetic state and key scalar
states around nonzero expected samples. In the v0 failing path, the zero flag
state remains pinned as `s[0]=255->255` and the exponent, sign, and magnitude
states do not advance, proving that the residual never leaves the zero-symbol
branch. The v1 sibling starts the first sample with `s[0]=128->91`, advances
the exponent and magnitude states, and reconstructs `-128`. This further
narrows the v0 issue to the historical initial zero-symbol state or its
interaction with the v0 arithmetic interval after embedded parameters, rather
than to the signed residual body.
The first-sample probe also exhaustively varied only the v0 zero-symbol state
from 0 through 255 while preserving the v0 arithmetic split. No candidate
reconstructed the expected sample 128; the closest observed candidate was
`s0=48`, which reconstructed 107. This rules out a single zero-state override
as the compatibility fix and points back to the arithmetic position, interval
semantics, or another historical v0 scalar-body convention.
Keeping the current `s0=255` and sweeping the restored arithmetic `low` value
across the entire current range also found no exact first-sample match. The
closest low candidate reconstructed 193, not 128. This makes a simple local
low adjustment unlikely; the remaining explanation likely involves how v0
defines the interval before the first residual or how the scalar body consumes
the following bits after the zero/nonzero decision.
Sweeping the nearby restored byte positions together with every `low` value
also produced no exact match with `s0=255`; the closest candidate was byte
position 150 with `low=66`, reconstructing 151. This weakens the hypothesis
that the v0 nonzero failure is only a small byte-position restore error.
Sweeping a uniform scalar-body initial state together with `s0` also produced
no exact first-sample match under the current scalar body syntax, although the
best candidates reached 127 or 129. This suggests that the remaining v0 range
nonzero issue is not merely an initial-state selection problem; an interval
rounding boundary or legacy scalar-body coding detail is likely still missing.
Repeating that `s0`/body-state sweep with the normal arithmetic split rather
than the v0 split also produced no exact match and again stopped at 127/129.
This weakens the hypothesis that the residual is one arithmetic split variant
away from compatibility; the missing rule is more likely in the scalar value
mapping or in a v0-specific signed residual boundary convention.

Applying an away-from-zero adjustment to the decoded nonzero scalar before
sample reconstruction produces exact first-sample matches for many `s0` and
scalar-body state candidates: 173 candidates under the legacy v0 split and 158
candidates under the normal split. This does not prove that mffv1 should apply
such an adjustment globally; it is a diagnostic-only result. It does, however,
make the remaining v0 range-coded nonzero gap look much more like a historical
signed-scalar boundary or value-mapping convention than a byte restore,
initial-state, or simple arithmetic split issue.
Extending that probe from the first sample to the full 16-sample nonzero gray
control found no exact `s0`/body-state pair. Without the away-from-zero
adjustment every candidate still fails at the first sample; with the adjustment
the best candidate matches only the first three samples before failing at the
second expected nonzero sample. This rules out a simple fix made only from a
uniform scalar-body initial state plus nonzero magnitude offset.
A compact v0/v1 sibling sequence trace makes the split clearer. The v0 path
keeps all first 16 symbols on context 0 with `s0=255->255`, never advancing
past content byte 152 while expected nonzero samples at positions 0, 3, and 15
all decode as zero. The v1 sibling consumes bytes immediately on the first
nonzero residual, updates `s0`, changes context for the next predicted sample,
and reconstructs all three nonzero samples. This reinforces that the v0
problem is the zero/nonzero gate or its pre-content arithmetic state, not the
later predictor or signed reconstruction step.
Tracing the passing v0 all-zero controls (`4x1` and `32x16`) shows the same
`s0=255->255` and content-byte-152 zero-branch behavior. Those vectors pass
because their expected residuals are all zero, not because the current v0
range model has proven it can leave the zero branch. The v1 all-zero siblings
advance and adapt state normally, so v0 all-zero success should be treated as
a narrow zero-run compatibility result rather than broad validation of v0
range-coded scalar decoding.
A focused mode probe compared `legacy_s255`, `legacy_s128`, `normal_s128`,
and `normal_s255` at the same v0 content arithmetic state. The nonzero control
still fails on the first sample in all four modes, while the 32x16 all-zero
control is fully matched only by `legacy_s255`; `legacy_s128` and
`normal_s128` fail at sample 3, and `normal_s255` fails after 408 samples.
This rules out a fix based only on choosing a different zero-state initial
value or toggling the v0 arithmetic split at Slice Content entry.
The content-byte trace adds an important constraint: the v0 nonzero control
and the passing v0 all-zero controls share the same after-Parameters arithmetic
state and the same content byte prefix from offset 152 through offset 166; the
payloads first differ around offset 167. With the current content=152
carry-state model, a deterministic range decoder must therefore produce the
same first samples for those files. Extending the independent reset-boundary
probe through offset 175 did not produce the expected `-128` first residual,
so the missing rule is not just "start decoding at the first differing byte"
either.
A pre-content symbol probe also tried consuming one to four unsigned or signed
range-coded symbols before the first sample; all of them decoded as zero and
left the first sample at zero. Extending that idea to an unsigned skip sweep
from 0 through 512 pre-symbols still found no exact first-sample match for the
nonzero control, while all-zero controls continue to match from skip 0 onward.
This rules out a simple hidden run of zero-valued pre-content symbols before
Slice Content.
A parameter-entry probe compared decoding the first sample directly from
`after_keyframe` against the current `after_parameters` entry point, with both
default and parsed custom transitions. The nonzero control still decodes the
first sample as zero in every entry mode. This weakens the hypothesis that the
current v0 parser merely reads too much embedded Parameters data before Slice
Content.
The generated-vector test suite now treats unsupported matched external
vectors as failures by default. This keeps future external-vector regressions
from silently joining a compatibility exception set now that the local
single-slice legacy v0/v1 set has no known unsupported entries. For active
compatibility diagnosis, `MFFV1_TEST_VECTOR_TRY_UNSUPPORTED=1` still asks the
harness to run vectors that fail the bootstrap-support precheck so their public
decode errors can be inspected directly.

The refreshed tiny v0 Golomb-Rice vectors were then still intentionally skipped.
Their boundary probe found only short zero-prefix matches with trailing-data or
first-sample mismatch diagnostics, which supported the earlier conclusion that
the missing piece was a legacy v0 Golomb-Rice payload boundary or
embedded-parameter convention rather than ordinary sample reconstruction.
Running the same v0 Golomb-Rice set with
`MFFV1_TEST_VECTOR_TRY_UNSUPPORTED=1` confirmed that the public decode path
reached the same class of failure: `1x1`, `2x1`, `4x1`, `8x1`, and `16x1`
all decoded up to a candidate plane end and then reported trailing bytes. The
matching v1 Golomb-Rice legacy set passed in strict mode, so this was still
best treated as a v0-specific payload-boundary or embedded-parameter placement
gap rather than a shared Golomb-Rice sample decoder problem.
The compact boundary summary now reports the best byte/bit candidate for those
v0 Golomb-Rice vectors, distinguishes traced sample matches from full
output-plane matches, and reports the first output mismatch. A diagnostic slice
width bug previously made the `2x1` and wider flat controls appear to stop
after one sample. After changing the candidate slice to cover the full frame,
`1x1`, `2x1`, `4x1`, and `8x1` can all reach output matches before trailing-byte
failure. The `16x1` control still diverges at `x=8` after matching the first
eight flat samples, with `byte=1 bit=0` as the unique best candidate.
This refocuses the remaining v0 Golomb-Rice work on run growth or termination
around longer flat runs, not on a one-sample output-window failure.
The run-segment trace shows the `16x1` best candidate matching eight flat
samples as four one-sample full runs followed by two two-sample full runs
(`seg1, seg1, seg1, seg1, seg2, seg2`). The next symbol is decoded as a run
interruption at `x=8` with difference `-7`. This suggests the remaining v0
Golomb-Rice difference is around long-run continuation or termination encoding,
not around the predictor or the early run-index growth.
Comparing the current encoder-side flat-run prefix against the same best
candidate makes the gap concrete: for a 16-sample flat run, the current
Golomb-Rice run writer emits `111111111`, while the v0 legacy payload starts
with `11111100000101101...`. The first six one bits explain why the decoder
matches through the two 2-sample runs; the following zero terminates the run
too early under the current run table. This narrows the next investigation to
the legacy v0 long-run code table or continuation threshold rather than slice
boundary placement.
An exhaustive expected-output scan corrected that conclusion: the valid
`16x1` content begins at `byte=18 bit=0`, where the payload suffix is
`111111111...` and the current run table decodes all 16 flat samples. The same
`byte=18 bit=0` boundary also fully matches the `1x1`, `2x1`, `4x1`, and
`8x1` flat v0 Golomb-Rice vectors. The parser-reported boundary at byte 3 is
therefore too early for these keyframe-embedded Parameters. However, status-only
candidate scanning is ambiguous: the `16x1` vector has six syntactically valid
content candidates from `15:5` through `18:0`, and only the expected output
selects the right one. Production decoding must not adopt a generic
"first/last candidate that parses" heuristic without another deterministic
legacy boundary rule or more constraining vectors.
The expanded v0/v1 Golomb-Rice vector set resolved the ambiguity. Version 0
embedded Parameters carry a quant table set just like the matching version 1
vectors; treating v0 as an implicit zero-quant-table stream left every
gradient in context 0, which made nonzero samples followed by flat samples
look like a run continuation. Parsing the v0 quant table set moves the
bootstrap content boundary to the actual Golomb-Rice payload and lets the
public decoder pass the v0 flat, single-nonzero, checker, and gradient legacy
Golomb-Rice vectors. The same correction also removes the generated-vector
skip for v0 range-coded nonzero controls, so the current local single-slice
legacy v0/v1 vector set has no known unsupported entries.

The refreshed RGB external set separates a Golomb-Rice-specific RGB gap from
general RGB reconstruction. `gr_rgb_bars_*` vectors pass, and
`range_rgb_testsrc_*` vectors pass, but `gr_rgb_testsrc_*` fails from the
small `64x48` controls upward. The first stable failure in
`gr_rgb_testsrc_64x48_1slice` occurs in the coded chroma line at
plane 1, `y=1`, `x=17`: the run mode reaches an interruption after a 17-sample
run, the current context-0 VLC state derives `k=8`, and the bit sequence
decodes to a large negative residual where the expected RGB output implies a
small `-2` coded-chroma residual. This rules out simple size dependence and
points at RGB Golomb-Rice context/run-state evolution before that sample.
The follow-up `gr_yuv420p_testsrc_*` controls all pass for `64x48`,
`128x96`, and `320x240` in both 1-slice and 2x2-slice layouts. That result
rules out a general Golomb-Rice `testsrc` complexity failure and makes the
remaining issue specific to the RGB/RCT Golomb-Rice path, especially the
shared coded-chroma context-0 state before an in-row run interruption.

Several local experiments were deliberately not kept. Making RGB Golomb-Rice
VLC banks plane-local broke the already-passing bars vectors, so FFmpeg-style
RGB Golomb-Rice streams appear to share at least the chroma VLC bank. Making
RGB run state plane-local also broke the passing bars vectors, which supports
the current shared RGB run-state behavior despite the generic RFC wording about
planes. Clearing only pending run counts at RGB coded-line boundaries did not
move the primary `testsrc` mismatch. Suppressing VLC-state updates for run
interruptions broke bars and the local Golomb-Rice context contract tests.
Carrying an explicit "pending interruption" bit across row boundaries exposed
interesting writer/decoder ambiguity but did not affect the `testsrc` failure
site, which is an in-row interruption, not a pending-row-boundary case.

The later refreshed vector set reintroduced targeted known gaps under the
generated-vector harness instead of treating every local probe as a release
blocker. At that point, the known generated-vector gap list included:

- `gr_rgba_testsrc2_2x2.mkv`
- compact legacy version 1 YUV420p no-Codec-Private controls
- compact legacy version 0 no-Codec-Private controls

For `gr_rgba_testsrc2_2x2.mkv`, forcing decode with
`MFFV1_TEST_VECTOR_TRY_UNSUPPORTED=1` shows that the read-ahead content
candidate still reaches the same RGB/RCT region before failing. The first
stable mismatch appears immediately after the coded row-0 RGB/RCT data, either
as the first alpha sample under the normal row-interleaved interpretation or
as row-1 coded Y when alpha is experimentally moved to a trailing planar pass.
This means the failure is not explained by a simple packed/planar expected
output mismatch.

Temporary experiments deliberately not kept:

- Decoding RGBA Golomb-Rice alpha after all RGB/RCT rows moved the first
  mismatch to coded plane 0, row 1, so the stream is not simply RGB rows
  followed by a trailing alpha plane.
- Giving alpha its own Golomb-Rice run-state bank changed the first alpha
  sample but did not make the vector decode.
- Mapping alpha to the RGB luma or chroma VLC bank did not make the vector
  decode.
- Making all RGBA Golomb-Rice run state plane-local for streams with an extra
  plane did not make the vector decode.
- Resetting the shared RGB/RGBA Golomb-Rice run state at each row did not make
  the vector decode.

The remaining `gr_rgba_testsrc2_2x2` ambiguity needs smaller RGBA-specific
black-box controls before another implementation change is justified. The most
useful next probes are tiny 8-bit GR RGBA vectors with constant opaque alpha,
constant non-opaque alpha, and simple RGB bars in both 1-slice and 2x2 layouts.

The refreshed compact vector set added those RGBA probes and also added
multi-frame controls. The 1-slice range and Golomb-Rice YUV420p/RGB
three-frame controls pass, as does the range-coded 3x2 RGB inter-frame
control. The compact range-coded YUVA10/RGBA10 controls also pass.

Subsequent fixes removed the Golomb-Rice YUV420p inter-frame and Golomb-Rice
RGBA known gaps:

- Golomb-Rice v3 slice content offsets now account for the RangeCoder low-byte
  holdover at the boundary between the range-coded slice header and
  Golomb-Rice body.
- Golomb-Rice RGBA now reconstructs the alpha plane in the same `bits+1`
  internal domain used by the RGB/RCT planes and truncates the stored alpha
  output back to the raw sample width.

The remaining known generated-vector gap is now focused on compact
range-coded legacy no-Codec-Private controls:

- `range_rgb_v1_legacy_1slice.mkv` and `range_yuv444p_v1_legacy_1slice.mkv`
  still fail under `MFFV1_TEST_VECTOR_TRY_UNSUPPORTED=1`, so the generated
  harness now treats compact version 1 no-Codec-Private legacy probes as a
  named gap rather than only the earlier YUV420p variants.
- `range_rgb_v0_legacy_1slice.mkv` and `range_yuv444p_v0_legacy_1slice.mkv`
  are the matching version 0 siblings and remain grouped with the same compact
  range-state boundary investigation.

With `MFFV1_TEST_VECTOR_TRACE_BOOTSTRAP=1`, known-gap legacy entries now emit
the same bootstrap/range-state probes used for unsupported-entry diagnostics.
For the compact v1 RGB probe, the first range residual can be decoded from the
post-parameter arithmetic state, but public output diverges immediately after
RGB reconstruction. For the compact v1 YUV444p probe, the luma plane begins
correctly and the first visible mismatch appears on the second chroma plane.
That points to a compact legacy range-state or plane-boundary rule rather than
a failed bootstrap parse.
