# Test Vector Registry

This registry records external FFV1 test vectors that are committed to the
mffv1 repository. It also defines the provenance fields required before adding
new vectors. Installed packages include this registry for policy reference; the
local `testvectors/` workspace itself is a source-tree testing aid and is not
installed.

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
- `testvectors/REGISTRY.md`: provenance policy and optional local
  compatibility scope.
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
schema documented by `testvectors/README.md` in the source tree.

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

## Optional Local Compatibility Scope

When a local generated header is supplied, the generated-vector test can cover
the following FFV1 option ranges. This section describes the supported local
test shape; it is not a list of files committed to the repository.

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
investigation material. Compact no-Codec-Private range-coded legacy MKV
controls for version 0 and version 1 RGB/YUV444p are part of the active local
compatibility set and should decode during the normal generated-vector run.

`MFFV1_TEST_VECTOR_TRY_UNSUPPORTED=1` remains available for future
investigation headers that contain entries outside the current decoder
coverage. Use it together with a narrow `MFFV1_TEST_VECTOR_FILTER` when
diagnosing a newly added unsupported stream directly.

When possible, keep the frame size modest, for example 32x24 or 64x48. Smaller
vectors keep diagnostics and generated headers easier to inspect, while still
covering the syntax and context-state behavior under test.

## Committed Vector Entry Template

Use this template only when an external vector is intentionally promoted from a
local-only artifact into a committed repository file. It is the registry entry
that justifies why the vector can be redistributed with mffv1.

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
