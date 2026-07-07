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
- Installed `mffv1/build_config.hpp` records build-time public configuration
  such as `MFFV1_ENABLE_STATUS_MESSAGES`.
- Pre-1.0 installed CMake package version files require exact-version
  compatibility.
- Installed user-facing Markdown documentation under `share/doc/mffv1`.
- Build option `MFFV1_ENABLE_STATUS_MESSAGES` for disabling library-generated
  diagnostic message text in `Status::message`.
- Package smoke runner for installed-header, exported-target, static-artifact,
  installed-documentation, and installed-documentation-link checks.
- Package smoke installed-header allowlist to reject unexpected public headers.
- Package smoke CMake package metadata allowlist to reject unexpected exported
  package files.
- Package smoke library and runtime artifact allowlist to reject unexpected
  shipped binaries.
- Package smoke installed-documentation allowlist to reject unexpected packaged
  files.
- Package smoke consumer requests the current package version so installed
  CMake version metadata is exercised.
- Package smoke derives the requested package version from the root CMake
  project version by default.
- Markdown link checker for source and installed documentation.
- Public tracked-file checker for local-only maintainer notes, generated
  vectors, generator archives, and build outputs.
- Public tracked-file checker allowlist for the `testvectors` local drop
  directory.
- Ignore local agent workspace directories by default.
- Repository attributes normalize text files to LF and keep binary media and
  archives unmodified.
- GitHub Actions smoke coverage for hosted Windows plus Ubuntu GCC and Clang
  build, test, and package checks.
- GitHub Actions no-status package smoke coverage for the installed
  `mffv1/build_config.hpp` contract.
- GitHub Actions no-status package smoke uses the dedicated package smoke
  profile.
- Contribution guidance for portability fixes and new compiler/platform
  reports.
- Contribution guidance that keeps pre-1.0 optimization secondary to
  correctness, specification traceability, readability, and test coverage.
- Public repository documentation for GitHub Issues, security reporting,
  contribution rules, pull requests, CI, CODEOWNERS, Dependabot, and generated
  release-note categories.

### Changed

- Project name changed from `ffv1` to `mffv1` to avoid confusion with FFmpeg's
  official FFV1 implementation and to emphasize the modular, modern C++ design.
- The CMake target is explicitly static; `BUILD_SHARED_LIBS` does not change
  the supported package artifact type.

### Known Limitations

- No stable release compatibility guarantee has been declared yet.
- Pre-1.0 releases may change public API and package shape between tags.
- The library does not demux or mux containers.
- The decoder does not automatically extract legacy frame-embedded
  `Parameters()`.
- The encoder writes version 3 only.
- SIMD coverage is intentionally narrow and most codec paths remain scalar.
