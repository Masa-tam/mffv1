# Test Vector Generator Review

This note records reviews of `testvectors/createVector.zip`. The archive is
intentionally not committed until the generator source, license status, and
provenance handling are accepted.

## Archive Snapshot

- Path: `testvectors/createVector.zip`
- SHA-256:
  `A166BE96812388CC461A9E84D546CB70582DFF7661F19197EF515E24A9DDCFA6`
- Contents:
  - `CMakeLists.txt`
  - `CMakePresets.json`
  - `mkv_to_cpp.cpp`

This snapshot replaces the first candidate reviewed at SHA-256
`5B7B8FDC1BB56AABD4AF199B066DB72305B294D79266E4B9F84EA74CE8362B26`.

## Clean-Room Position

The generator uses FFmpeg through public headers and linked binaries. It reads
container packets, codec private data, and decoded frames through public API
calls, then writes a mechanical C++ header for the local test harness.

No FFmpeg source-code-derived algorithm implementation was identified in the
archive during this review. The generator must remain outside the mffv1 library
build and must not introduce any FFmpeg dependency into the library itself.

## Resolved First-Review Concerns

- Source and CMake files now carry SPDX-style MIT license headers.
- CMake now uses target-scoped include paths and library paths.
- FFmpeg allocation and setup calls now have basic failure handling.
- The decoder is flushed after packet reading.
- Pixel format descriptor lookup is checked.
- Plane data size is computed with `std::size_t` values.
- Generated C++ string literals escape quotes and backslashes in vector names.
- The generator accepts an explicit `-o output.hpp` argument.
- The generated header contract now supports multiple frame payloads and
  expected plane sets per vector.

## Remaining Acceptance Concerns

- The archive has source-file SPDX headers but still has no standalone
  generator license file. Add one before committing the archive, especially if
  the generator's license differs from the main mffv1 MIT license.
- The generator directly links to FFmpeg libraries. If built or distributed
  against a GPL-enabled FFmpeg build, treat the generated executable as subject
  to GPL distribution obligations. Keeping the generator source under MIT is
  compatible with GPL use, but distributing generator binaries built against
  GPL FFmpeg should be handled separately from the MIT-licensed mffv1 library.
- Negative FFmpeg `linesize` values are not copied safely. The expression
  `src + y * src_linesize` mixes `std::size_t` and a potentially negative
  stride, which can wrap and read outside the plane. Either reject negative
  linesizes explicitly or normalize the starting pointer before row copying.
- The plane mapping logic should still be verified against planar RGB, alpha,
  packed formats, negative linesizes, and padded lines.
- The generator returns success even when all input files fail but an empty
  header is written. Consider returning a non-zero process status when no
  vectors were produced.

## Current Recommendation

Do not commit `createVector.zip` yet. Keep it visible as an untracked review
candidate until the remaining acceptance concerns are settled. Once accepted,
commit the generator archive in a dedicated generator-only change and keep
generated vector data separate unless its provenance entry is complete.
