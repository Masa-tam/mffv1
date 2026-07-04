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

## Requested Local Compatibility Vectors

The first local compatibility set has validated basic v3 range decoding for
10-bit gray, 8-bit and 10-bit 4:2:0, one-slice and 2x2 slice layouts, plus
flat 8-bit Golomb-Rice gray controls. The next most useful local vectors should
exercise less uniform content and syntax combinations that are not yet covered
by the current local set.

Current high-value requests:

- Range-coded 8-bit 4:2:0, one slice, with nonzero chroma gradients.
  The current passing 4:2:0 controls begin with neutral chroma, so this probes
  Cb/Cr prediction and shared chroma-slot range contexts beyond the first
  neutral run.
- Range-coded 10-bit 4:2:0, 2x2 slices, with nonzero chroma gradients.
  This combines subsampling, multi-slice payload location, and chroma context
  sharing under a higher bit depth.
- Range-coded 8-bit YUVA or gray+alpha, one slice.
  This verifies the extra-plane quant-table slot and range context bank
  allocation separately from chroma.
- Range-coded RGB or RGBA planar vectors that the generator accepts, one
  slice. If a packed FFmpeg output is rejected by the generator, keep it out of
  the local header and record that rejection outside the mffv1 tree.
- A range-coded vector with nonzero quantization-table-set indexes in the Slice
  Header, if the generator can produce one. This directly validates that range
  context banks are shared by slot, not by qset value.
- Golomb-Rice 8-bit gray with a small vertical ramp, one slice.
  This isolates row-boundary zero-run carry without chroma initialization.
- Golomb-Rice 8-bit gray with a small horizontal ramp, one slice.
  This isolates scalar/run-interruption transitions within a row.
- Golomb-Rice 8-bit 4:2:0, one slice, with flat Y and neutral flat chroma.
  This is the compact pass control for YCbCr chroma predictor initialization.
- Golomb-Rice 8-bit 4:2:0, one slice, with flat Y and a single simple chroma
  step. This checks chroma-plane alignment and expected-plane semantics after
  the flat chroma control is stable.

Recommended naming pattern for local generated headers:

- `range_intra_420p8_1slice_chroma_grad`
- `range_intra_420p10_2x2_chroma_grad`
- `range_intra_yuva8_1slice`
- `range_intra_rgba8_1slice`, only if planar data is generated
- `range_intra_420p8_1slice_qidx`
- `gr_intra_gray8_1slice_ygrad_small`
- `gr_intra_gray8_1slice_xgrad_small`
- `gr_intra_420p8_1slice_yflat_uvflat_small`
- `gr_intra_420p8_1slice_yflat_uvstep_small`

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
