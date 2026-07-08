# Test Vector Registry

This registry records external FFV1 test vectors that are committed to the
mffv1 repository. It also defines the provenance fields required before adding
new vectors.

The optional local workspace under `testvectors/` is not itself a registry.
Local FFmpeg binaries, FFmpeg headers, MKV samples, generated C++ headers, and
generator build products are local test artifacts by default. They may be used
to validate the library locally without being recorded here. They must not be
committed unless the entries below are completed and reviewed.

## Current Committed External Vectors

No external FFV1 test vectors are currently committed.

The repository contains only:

- `testvectors/README.md`: local drop-point instructions and generated header
  contract.
- `testvectors/.gitignore`: keeps local vector artifacts out of commits.
- CMake-generated fallback `test_vector_data.hpp` in the build tree, defining
  `NO_DEFINE_TEST_VECTOR_DATA` when no local source-tree vector header exists.
- `tests/unit/test_vector_tests.cpp`: skip-aware test hook.

Generator projects should live outside this repository unless they are added in
a dedicated reviewed change. The mffv1 tree only requires the generated header
contract documented in `testvectors/README.md`.

## Required Record For Each Added Vector

Each committed external vector must have an entry with:

- Stable vector name.
- Repository path.
- Input media origin.
- Input media license or permission basis.
- Generator name and version or commit.
- FFmpeg binary source URL, version, and build license notes.
- FFmpeg command line or generator command line used.
- SHA-256 of the original media.
- SHA-256 of generated vector files.
- Date generated.
- Person or agent who generated it.
- Notes on whether the vector contains codec private data, frame payloads, or
  decoded expected samples.

Generated C++ headers used by the unit tests must follow the local test-only
schema documented in `testvectors/README.md`.

## Local-Only Vector Use

Developers and users may provide `testvectors/test_vector_data.hpp` locally,
run the skip-aware tests, and then remove the local header. If the header is
absent, CMake generates a build-tree placeholder that skips external-vector
checks. This local workflow is intentionally outside the registry and does not
make the mffv1 library depend on FFmpeg, any generator, or generated vector
data.

Only generated vector data that is committed to the repository needs a registry
entry with full provenance and license review.

Do not commit vectors derived from private, personal, unreleased, or otherwise
sensitive media unless the content owner has explicitly approved publication
and the license review is complete. When possible, reproduce issues with
synthetic or minimized media that preserves the codec behavior without exposing
the original content.

## Current Local Compatibility Coverage

The current optional local vector set validates v3 range and Golomb-Rice
decoding across the compatibility cases that most recently drove fixes:

- Range-coded 8-bit 4:2:0, one slice, with nonzero chroma gradients. This
  probes Cb/Cr prediction and shared chroma-slot range contexts beyond neutral
  flat chroma.
- Range-coded 10-bit 4:2:0, 2x2 slices, with nonzero chroma gradients. This
  combines subsampling, multi-slice payload location, and chroma context
  sharing under a higher bit depth.
- Range-coded 8-bit YUVA, one slice. This verifies the extra-plane quant-table
  slot and range context bank allocation separately from chroma.
- Range-coded planar RGBA, one slice, when the generator can supply unpacked
  planes. Packed FFmpeg output should still be rejected by the generator and
  kept out of the local header.
- Range-coded 8-bit 4:2:0 with nonzero Slice Header quantization-table-set
  indexes. This validates that range context banks are shared by slot, not by
  the literal qset value.
- Golomb-Rice 8-bit gray vertical and horizontal ramps. These isolate
  row-boundary zero-run carry and scalar/run-interruption transitions without
  chroma state.
- Golomb-Rice 8-bit 4:2:0 with flat Y plus flat or stepped chroma. These are
  compact controls for zero chroma borders, chroma-plane alignment, and
  slot-local adaptive VLC state.
- Golomb-Rice 8-bit RGB black and primary-color controls, in 1-slice and 2x2
  grids where available. These validate RGB line-plane run-state continuity,
  RCT output reconstruction, and slot-local adaptive VLC state for coded RCT
  Y versus coded Cb/Cr.
- AVI-derived legacy version 0/1 range-coded and Golomb-Rice 8-bit single-slice
  payloads with empty Codec Private data. These validate explicit
  `bootstrap_legacy_frame()` setup and decode of the same keyframe payload.
- MKV-derived legacy version 0/1 Golomb-Rice RGB and YUV444 single-slice
  payloads with no Codec Private data. These validate legacy bootstrap from
  keyframe-embedded parameters in a container where Codec Private SHOULD NOT be
  written for those versions.
- Golomb-Rice 8-bit RGBA and YUVA `testsrc2` controls, including non-flat
  alpha and multi-slice coverage across the pair. These exercise the
  extra-plane quant-table slot together with RGB/RCT or chroma run state.
- Golomb-Rice 10-bit RGB and YUV420p Mandelbrot controls. These exercise
  high-bit reconstruction and adaptive context evolution on non-flat material.
- Range-coded 10-bit RGB and RGBA `testsrc2` controls. These provide high-bit
  range-coded checks for RGB compatibility transform and optional alpha output.
- Error-correction multi-slice RGB and YUV controls for both range and
  Golomb-Rice. These exercise CRC/footer discovery with non-square slice grids.
- True multi-frame version 3.4 range and Golomb-Rice controls for RGB and
  YUV420p. These expose multiple `frame_payloads` and expected-plane sets in
  one `DecodeVector`, so reference-state continuity is exercised by the public
  generated-vector test.
- Complementary slice-grid siblings for the high-bit and alpha controls,
  including one-slice and 2x2 variants across the current local set.
- Golomb-Rice 10-bit RGBA/YUVA controls where the generator can provide planar
  expected data. These combine high-bit reconstruction, alpha, and adaptive
  Golomb-Rice context evolution.

Legacy version 0 AVI-derived vectors may be kept in a local generated header as
investigation material. The current test harness recognizes legacy version 0
no-Codec-Private vectors as known compatibility gaps by default; set
`MFFV1_TEST_VECTOR_TRY_UNSUPPORTED=1` with a narrow
`MFFV1_TEST_VECTOR_FILTER` when diagnosing those streams directly.

The same known-gap handling currently applies to the compact legacy version 1
YUV420p no-Codec-Private controls and the 8-bit Golomb-Rice RGBA `testsrc2`
control. These vectors are useful local probes, but they should not make the
normal generated-vector run fail while the implementation work is still in
progress.

If version 0 compatibility needs more evidence, prefer tiny diagnostic vectors
over broad coverage expansion:

- Range-coded 8-bit gray v0, 1 slice, all-zero frames at 1x1, 2x1, 3x1, 4x1,
  8x1, 16x1, and 32x16. These separate first-symbol behavior from later scalar
  context evolution.
- Range-coded 8-bit gray v0, 1 slice, exactly one nonzero luma sample at the
  first, fourth, and last position. These isolate the first zero/non-zero range
  decision and reconstruction path.
- Golomb-Rice 8-bit gray v0, 1 slice, all-zero frames at 1x1, 2x1, 4x1, 8x1,
  and 16x1. These distinguish keyframe-bit-only payloads from keyframe plus
  embedded-parameter payloads.
- Matching v1 siblings for each legacy v0 diagnostic vector whenever FFmpeg can
  generate them. The test harness already compares v0 failures against passing
  v1 siblings when the names match.

Keep the existing local set available while working on entropy, prediction,
slice, or frame-state changes. Ask for new vectors only when a new unsupported
profile or ambiguous mismatch needs black-box confirmation.

## High-Value Local Vector Requests

The current local vectors cover the most recent 8-bit RGB/YUV420p `testsrc`,
RGB bar, high-bit, alpha, CRC, MKV legacy Golomb-Rice, and true multi-frame
reference-state cases. The next most useful local-only additions are:

- Three-frame inter variants for range and Golomb-Rice in YUV420p and RGB,
  especially with one keyframe followed by two non-keyframes. These extend the
  current two-payload reference-state coverage.
- 2x2 or 3x2 multi-frame variants, if FFmpeg and the generator can produce
  compact files. These would combine slice state, reference state, CRC/footer
  location, and non-square grids in one black-box check.
- Legacy version 0/1 range-coded RGB or YUV444 MKV single-slice vectors, if
  FFmpeg can produce them without Codec Private data. These would complement
  the current legacy Golomb-Rice MKV controls.
- Range-coded 10-bit YUVA/RGBA controls if FFmpeg can generate planar expected
  data for both alpha layouts. The current set already has range RGBA10 and
  Golomb-Rice RGBA/YUVA10 coverage, but a YUVA10 range sibling would round out
  the matrix.
- Minimal Golomb-Rice 8-bit RGBA controls, ideally 16x16 or 32x16, with:
  constant opaque alpha, constant non-opaque alpha, and simple RGB bars. Generate
  both 1-slice and 2x2 variants if possible. These isolate the current
  `gr_rgba_testsrc2_2x2` gap from `testsrc2` complexity, alpha-plane
  prediction, and multi-slice state.

Keep these vectors local unless a separate provenance review promotes a
specific minimized case into the repository.

Recommended naming pattern for local generated headers:

- `range_intra_420p8_1slice_chroma_grad`
- `range_intra_420p10_2x2_chroma_grad`
- `range_intra_yuva8_1slice`
- `range_intra_rgba8_1slice`, only if planar data is generated
- `range_intra_420p8_1slice_qidx`
- `range_rgb10_testsrc_1slice`
- `range_rgb10_testsrc_2x2`
- `range_rgba10_testsrc_1slice`, only if planar data is generated
- `range_rgba10_testsrc_2x2`, only if planar data is generated
- `rgb_black_1slice`
- `rgb_red_1slice`
- `rgb_green_1slice`
- `rgb_blue_1slice`
- `gr_rgba8_testsrc_1slice`, only if planar data is generated
- `gr_rgba8_testsrc_2x2`, only if planar data is generated
- `gr_rgba8_flat_opaque_alpha_1slice`
- `gr_rgba8_flat_opaque_alpha_2x2`
- `gr_rgba8_flat_mid_alpha_1slice`
- `gr_rgba8_flat_mid_alpha_2x2`
- `gr_rgba8_bars_opaque_alpha_1slice`
- `gr_rgba8_bars_opaque_alpha_2x2`
- `gr_yuva8_testsrc_1slice`
- `gr_yuva8_testsrc_2x2`
- `gr_rgb10_testsrc_1slice`
- `gr_rgb10_testsrc_2x2`
- `gr_yuv420p10_testsrc_1slice`
- `gr_yuv420p10_testsrc_2x2`
- `range_yuv420p_inter_64x48_2frames`
- `gr_yuv420p_inter_64x48_2frames`
- `range_rgb_inter_64x48_2frames`
- `gr_rgb_inter_64x48_2frames`
- `range_yuv420p_inter_64x48_3frames`
- `gr_yuv420p_inter_64x48_3frames`
- `range_rgb_inter_64x48_3frames`
- `gr_rgb_inter_64x48_3frames`
- `gr_intra_gray8_1slice_ygrad_small`
- `gr_intra_gray8_1slice_xgrad_small`
- `gr_intra_420p8_1slice_yflat_uvflat_small`
- `gr_intra_420p8_1slice_yflat_uvstep_small`
- `range_gray_v1_legacy_1slice`
- `range_yuv420p_v1_legacy_1slice`
- `gr_gray_v1_legacy_1slice`
- `gr_yuv420p_v1_legacy_1slice`
- `range_gray_v0_legacy_1slice_1x1`
- `range_gray_v0_legacy_1slice_4x1`
- `range_gray_v0_legacy_1slice_32x16`
- `range_gray_v0_legacy_1slice_16x1_nonzero`
- `gr_gray_v0_legacy_1slice_1x1`
- `gr_gray_v0_legacy_1slice_4x1`
- `gr_gray_v0_legacy_1slice_16x1`

When possible, keep the frame size modest, for example 32x24 or 64x48. Smaller
vectors keep diagnostics and generated headers easier to inspect, while still
covering the syntax and context-state behavior under test.

## Entry Template

```markdown
## Vector: <name>

- Path:
- Input media origin:
- Input media license:
- Generator:
- FFmpeg source URL:
- FFmpeg version:
- FFmpeg build license notes:
- Command:
- Input SHA-256:
- Generated SHA-256:
- Date generated:
- Generated by:
- Contents:
- Review notes:
```

## Clean-Room Constraints

- Vectors may be generated from media files and public container/codec metadata.
- Do not copy or translate FFmpeg source code into generated vector files.
- Do not commit FFmpeg binaries, headers, or local build outputs as vectors.
- Keep committed generated data mechanically reproducible from the recorded
  inputs and commands whenever possible.
- Prefer synthetic or minimized inputs over sensitive real-world media.
