# Test Vector Generator Review

This note records reviews of the local `createVector.zip` candidate that was
previously evaluated under `testvectors/`. The current project direction is to
keep generator projects outside the mffv1 repository and let mffv1 consume only
the generated `test_vector_data.hpp` contract when users provide one locally.

## Archive Snapshot

- Path: `testvectors/createVector.zip`
- SHA-256:
  `F2C5D746AF4BE43C21F5EB79187568E56F3044032DE81A36E1E03CA5327E9930`
- Contents:
  - `CMakeLists.txt`
  - `CMakePresets.json`
  - `LICENSE`
  - `mkv_to_cpp.cpp`

This snapshot replaces the first candidate reviewed at SHA-256
`5B7B8FDC1BB56AABD4AF199B066DB72305B294D79266E4B9F84EA74CE8362B26`
and the second candidate reviewed at SHA-256
`A166BE96812388CC461A9E84D546CB70582DFF7661F19197EF515E24A9DDCFA6`,
and the third candidate reviewed at SHA-256
`7A1CF7E54C8DDDF58751BB266C4BDDD5839CBDAA9A5E41978B96AB587E9AAD7D`,
and the fourth candidate reviewed at SHA-256
`C34398233EF59E027F044AC3E502CFAFF75A07796D70E0DDAD848613E766FBB3`.

## Clean-Room Position

The generator uses FFmpeg through public headers and linked binaries. It reads
container packets, codec private data, and decoded frames through public API
calls, then writes a mechanical C++ header for the local test harness.

No FFmpeg source-code-derived algorithm implementation was identified in the
archive during this review. The generator must remain outside the mffv1 library
build and must not introduce any FFmpeg dependency into the library itself.

## Resolved Review Concerns

- Source and CMake files now carry SPDX-style GPL-3.0-or-later license
  headers.
- The archive now includes a GPLv3 `LICENSE` file.
- CMake now uses target-scoped include paths and library paths.
- FFmpeg allocation and setup calls now have basic failure handling.
- The decoder is flushed after packet reading.
- Pixel format descriptor lookup is checked.
- Plane data size is computed with `std::size_t` values.
- Generated C++ string literals escape quotes and backslashes in vector names.
- The generator accepts an explicit `-o output.hpp` argument.
- The generated header contract now supports multiple frame payloads and
  expected plane sets per vector.
- The generator returns a non-zero process status when no vectors are
  produced.
- Negative FFmpeg `linesize` values are rejected explicitly.
- Output file creation failure is propagated through `WriteOutput()` and
  causes a non-zero process status.
- The generated header no longer declares its own SPDX license identifier.
- The generator source comments explicitly document packed-format rejection,
  alpha-plane mapping, and byte-for-byte stride-padding copies.

## Sample Vector Check

The refreshed local `test_vector_data_sample.hpp` was generated with additional
RGBA/YUVA inputs. The RGBA input was rejected by the generator because FFmpeg
returned it as a packed format. The YUVA input was accepted and appears in the
sample header as `smptebars_intra_yuva.mkv` with an explicit
`mffv1::PlaneRole::Alpha` plane.

The checked input commands were:

```text
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "smptebars=size=320x240:rate=25" -vf "format=gbrap" -frames:v 1 -c:v ffv1 -level 3 -g 1 -slices 4 -pix_fmt gbrap "smptebars_intra_rgba.mkv"
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "smptebars=size=320x240:rate=25" -vf "format=yuva444p" -frames:v 1 -c:v ffv1 -level 3 -g 1 -slices 4 -pix_fmt yuva444p "smptebars_intra_yuva.mkv"
```

## Remaining Acceptance Concerns

- The generator source is now GPL-3.0-or-later. Keep this archive and any
  generator binaries clearly separate from the MIT-licensed mffv1 library
  deliverables. The mffv1 library must not link to FFmpeg or the generator.
- Broader plane mapping should still be verified with more source formats
  before generated vectors are treated as accepted conformance data.
- Generated vector data needs a provenance entry only when it is committed to
  the repository. Local test-only generated headers remain outside the
  registry.

## Current Recommendation

Keep the generator as a separate GPL-licensed project or local tool outside the
mffv1 tree. Do not commit `createVector.zip` or generated vector data to this
repository by default. If a generator or vectors are ever promoted into the
repository, do so through a dedicated reviewed change with explicit provenance
and license records.
