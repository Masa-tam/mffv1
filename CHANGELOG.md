# Changelog

All notable project changes should be recorded here.

This project is currently pre-release software. The format of this file follows
the spirit of Keep a Changelog. Version `0.1.0` is the first public release and
remains a pre-1.0 compatibility milestone.

## Unreleased

## 0.1.1 - 2026-07-14

### Changed

- Public status/result-returning API types and functions are annotated with
  `[[nodiscard]]` to help callers catch accidentally ignored errors.
- Public virtual codec interfaces are explicitly non-copyable and non-movable.
- Public enum values for `ErrorCode`, `PlaneRole`, `SampleFormat`, and
  `LegacyBootstrapState` are explicitly assigned.
- Slice decoding validates that caller-provided `SliceState` line geometry
  matches the selected output slice window before writing samples.
- Golomb-Rice read-ahead decoding uses slice-sized temporary output buffers
  instead of allocating one full-frame temporary buffer per slice.
- Parallel slice decoding now validates raster coverage when raster metadata
  is available and falls back to serial decoding when non-overlap cannot be
  proven locally.
- Golomb-Rice multi-slice read-ahead preference now applies to all version 3
  extra-plane streams, including YUVA, not only RGBA.
- Slice output window validation clamps oversized chroma subsampling shifts
  and leaves no stale or partial windows after validation failures.

## 0.1.0 - 2026-07-10

Initial public pre-1.0 release.

### Added

- Specification-driven independent C++20 FFV1 codec library with
  container-independent public decoder and encoder factories.
- Public API based on pure virtual decoder and encoder interfaces returned as
  `std::unique_ptr` instances.
- FFV1 version 3 decoder support for stable micro-version 4 or later.
- FFV1 version 0 and 1 decoder support for streams configured from external
  parameters or by the legacy bootstrap API.
- FFV1 version 3 encoder support.
- Range coding and Golomb-Rice coding for decoding and encoding.
- Stateful keyframe and non-keyframe operation.
- Y-only, planar YCbCr with subsampling, optional alpha, RGB, and RGBA frame
  layouts.
- Decoding for 1 through 16 bits per raw sample.
- Encoding for 8 through 16 bits per raw sample.
- Version 3 slice headers, footers, optional CRC, and multiple slices.
- Deterministic slice-level multi-thread execution with configurable worker
  count.
- Runtime CPU feature selection with scalar fallbacks plus selected SSE2 and
  AVX2 RGB color-transform kernels on x86 builds.
- CMake build, install, and package configuration support for the static
  `mffv1::mffv1` target.
- Installed `mffv1/build_config.hpp` reporting public build configuration such
  as `MFFV1_ENABLE_STATUS_MESSAGES`.
- Build option `MFFV1_ENABLE_STATUS_MESSAGES` for disabling generated
  diagnostic text in `Status::message`.
- User-facing documentation for building, decoding, encoding, frame buffers,
  support policy, security policy, and development provenance.
- GoogleTest-based unit and compatibility test suite.
- Optional local external test-vector header workflow that keeps generated
  vector payloads out of normal source releases.
- Release verification scripts for package smoke checks, installed
  documentation links, public tracked-file boundaries, sanitizer builds, and
  no-status package behavior.
- GitHub Actions smoke coverage for hosted Windows plus Ubuntu GCC and Clang
  build, test, and package checks.

### Known Limitations

- No stable release compatibility guarantee has been declared yet.
- Pre-1.0 releases may change public API and package shape between tags.
- The library does not demux or mux containers.
- Legacy version 0/1 decoding requires caller-provided dimensions and either
  external parameters or an explicit legacy bootstrap call.
- The encoder writes version 3 streams only.
- Shared-library ABI support is not part of the current release surface.
- SIMD coverage is intentionally narrow and most codec paths remain scalar.
