# Changelog

All notable project changes should be recorded here.

This project is currently pre-release software. The format of this file follows
the spirit of Keep a Changelog, but version numbers and compatibility promises
start only once the first public release is tagged.

## Unreleased

### Added

- Clean-room C++20 FFV1 decoder and encoder library skeleton.
- Public decoder and encoder factories returning `std::unique_ptr` instances.
- Version 3 Configuration Record parsing and writing with CRC parity.
- FFV1 version 0/1 decoder support for configured legacy streams.
- FFV1 version 3 decoder support for stable micro-version 4 or later.
- FFV1 version 3 encoder support.
- Range coding and Golomb-Rice coding paths.
- Y-only, planar YCbCr, optional alpha, RGB, and RGBA frame layouts.
- Slice parsing, validation, deterministic multi-slice execution, and
  configurable worker count.
- Runtime CPU feature selection with scalar fallbacks plus SSE2 and AVX2 RGB
  color-transform kernels on x86 builds.
- GoogleTest-based unit and local compatibility test infrastructure.
- Optional local test-vector header workflow that keeps generated vectors out
  of ordinary commits.
- CMake install and package configuration support.
- Build option `MFFV1_ENABLE_STATUS_MESSAGES` for disabling library-generated
  diagnostic message text in `Status::message`.

### Changed

- Project name changed from `ffv1` to `mffv1` to avoid confusion with FFmpeg's
  official FFV1 implementation and to emphasize the modular, modern C++ design.

### Known Limitations

- No stable release compatibility guarantee has been declared yet.
- The library does not demux or mux containers.
- The decoder does not automatically extract legacy frame-embedded
  `Parameters()`.
- The encoder writes version 3 only.
- SIMD coverage is intentionally narrow and most codec paths remain scalar.
