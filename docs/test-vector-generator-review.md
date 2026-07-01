# Test Vector Generator Review

This note records reviews of `testvectors/createVector.zip`. The archive is
intentionally not committed until the generator source, license status, and
provenance handling are accepted.

## Archive Snapshot

- Path: `testvectors/createVector.zip`
- SHA-256:
  `7A1CF7E54C8DDDF58751BB266C4BDDD5839CBDAA9A5E41978B96AB587E9AAD7D`
- Contents:
  - `CMakeLists.txt`
  - `CMakePresets.json`
  - `LICENSE`
  - `mkv_to_cpp.cpp`

This snapshot replaces the first candidate reviewed at SHA-256
`5B7B8FDC1BB56AABD4AF199B066DB72305B294D79266E4B9F84EA74CE8362B26`
and the second candidate reviewed at SHA-256
`A166BE96812388CC461A9E84D546CB70582DFF7661F19197EF515E24A9DDCFA6`.

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

## Remaining Acceptance Concerns

- The generator source is now GPL-3.0-or-later. Keep this archive and any
  generator binaries clearly separate from the MIT-licensed mffv1 library
  deliverables. The mffv1 library must not link to FFmpeg or the generator.
- Negative FFmpeg `linesize` handling is improved by using signed pointer
  arithmetic, but the generator still does not explicitly reject negative
  linesizes or normalize the starting pointer before row copying. Make that
  behavior deliberate before accepting vectors from such formats.
- The plane mapping logic should still be verified against planar RGB, alpha,
  packed formats, negative linesizes, and padded lines.
- `WriteOutput()` returns `void`; if the output file cannot be opened after
  vectors are produced, `main()` still returns success. Return a status from
  `WriteOutput()` so scripted generation can fail reliably.
- The generated header currently declares `SPDX-License-Identifier: MIT`.
  This is acceptable only if generated vector data is intentionally
  project-owned or otherwise permissively licensed by its input provenance.
  Keep this checked per vector source.

## Current Recommendation

Do not commit `createVector.zip` yet. Keep it visible as an untracked review
candidate until the remaining acceptance concerns are settled. Once accepted,
commit the GPL-licensed generator archive in a dedicated generator-only change
and keep generated vector data separate unless its provenance entry is
complete.
