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

Legacy version 0 AVI-derived vectors may be kept in a local generated header as
investigation material. The current test harness decodes the generated
single-slice version 0 range-coded and Golomb-Rice vectors when present.

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

Recommended naming pattern for local generated headers:

- `range_intra_420p8_1slice_chroma_grad`
- `range_intra_420p10_2x2_chroma_grad`
- `range_intra_yuva8_1slice`
- `range_intra_rgba8_1slice`, only if planar data is generated
- `range_intra_420p8_1slice_qidx`
- `rgb_black_1slice`
- `rgb_red_1slice`
- `rgb_green_1slice`
- `rgb_blue_1slice`
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
