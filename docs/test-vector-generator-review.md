# Test Vector Generator Review

This note records the first review of `testvectors/createVector.zip`.
The archive is intentionally not committed until the generator source,
license status, and provenance handling are accepted.

## Archive Snapshot

- Path: `testvectors/createVector.zip`
- SHA-256:
  `5B7B8FDC1BB56AABD4AF199B066DB72305B294D79266E4B9F84EA74CE8362B26`
- Contents:
  - `CMakeLists.txt`
  - `CMakePresets.json`
  - `mkv_to_cpp.cpp`

## Clean-Room Position

The generator uses FFmpeg through public headers and linked binaries. It reads
container packets, codec private data, and decoded frames through public API
calls, then writes a mechanical C++ header for the local test harness.

No FFmpeg source-code-derived algorithm implementation was identified in the
archive during this review. The generator must remain outside the mffv1 library
build and must not introduce any FFmpeg dependency into the library itself.

## Acceptance Concerns

- The archive has no explicit license file or source header. Add a clear
  license statement before committing the generator.
- If the generator is linked against a GPL FFmpeg build, review whether the
  generator archive should be distributed, and under what license, separately
  from the MIT-licensed mffv1 library.
- The CMake file uses global `include_directories()` and `link_directories()`.
  Prefer target-scoped include paths and imported library paths.
- FFmpeg allocation and setup calls are not fully checked:
  `avcodec_alloc_context3()`, `avcodec_parameters_to_context()`,
  `av_packet_alloc()`, and `av_frame_alloc()` need failure handling.
- Delayed decoder frames are not flushed after packet reading completes.
- The pixel format descriptor is assumed to be present.
- Plane size is computed as `int stride * int height`; use checked
  `std::size_t` arithmetic before copying plane data.
- Generated C++ string literals should escape file names.
- The plane mapping logic should be verified against planar RGB, alpha,
  packed formats, negative linesizes, and padded lines.
- The output path is fixed to `test_vector_data.hpp`; an explicit output
  argument would make scripted use safer.

## Current Recommendation

Do not commit `createVector.zip` yet. Keep it visible as an untracked review
candidate, update the generator source with the concerns above, then commit it
in a dedicated generator-only change once license and provenance expectations
are settled.
