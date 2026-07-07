# mffv1

mffv1 is a clean-room C++20 implementation of the FFV1 video codec. It is
designed as a modular codec library rather than a container or FFmpeg wrapper.

The project is currently pre-release software. The decoder and encoder already
cover the main version 3 profiles used by the test suite, but the public API
and compatibility guarantees should still be treated as provisional until a
stable release is tagged.

## Goals

- Clean-room implementation based on RFC 9043 and project-owned tests.
- MIT-licensed library code with explicit third-party boundaries.
- Container-independent decoder and encoder APIs.
- Slice-level parallel execution.
- Runtime CPU feature selection with scalar fallbacks and selected SIMD
  kernels.

## Current Codec Coverage

The decoder currently supports FFV1 versions 0 and 1 plus stable version 3
micro-version 4 or later. The encoder currently writes version 3 streams.

Implemented paths include:

- Range coding and Golomb-Rice coding.
- 1 through 16 bits per raw sample for decoding.
- 8 through 16 bits per raw sample for encoding.
- Y-only, planar YCbCr with subsampling, optional alpha, RGB, and RGBA.
- Version 3 slice headers, footers, optional CRC, and multiple slices.
- Stateful keyframe and non-keyframe operation.

See the references for exact behavior and limitations.

## Build

The primary development target is Visual Studio 2026 x64 with CMake.

```powershell
cmake --preset vs2026-x64
cmake --build --preset vs2026-x64-debug
ctest --preset vs2026-x64-debug --output-on-failure
```

GoogleTest is used for unit and compatibility tests. The project can also be
configured with `MFFV1_BUILD_TESTS=OFF` for library-only builds.

The package target is a static library. Shared-library ABI support is not part
of the current release surface.

Installed consumers should use the exported CMake target:

```cmake
find_package(mffv1 CONFIG REQUIRED)
target_link_libraries(app PRIVATE mffv1::mffv1)
```

The repository includes a package smoke runner that installs the library,
checks the installed headers and documentation, builds a minimal consumer, and
runs it:

```powershell
cmake `
  -DMFFV1_PACKAGE_SMOKE_GENERATOR="Visual Studio 18 2026" `
  -DMFFV1_PACKAGE_SMOKE_ARCHITECTURE=x64 `
  -P cmake\RunPackageSmoke.cmake
```

## Documentation

- [Build Guide](docs/BUILD.md)
- [Decoder Reference](docs/DECODER_REFERENCE.md)
- [Encoder Reference](docs/ENCODER_REFERENCE.md)
- [Frame Buffer Reference](docs/FRAME_BUFFER_REFERENCE.md)
- [License And Clean-Room Provenance](docs/LICENSE_AND_PROVENANCE.md)
- [Test Vector Registry](docs/test-vectors.md)
- [Release Process](docs/RELEASE_PROCESS.md)
- [Support Policy](docs/SUPPORT_POLICY.md)
- [Security Policy](SECURITY.md)
- [Changelog](CHANGELOG.md)

## Clean-Room Boundary

mffv1 is not affiliated with FFmpeg. FFmpeg may be used locally as a black-box
interoperability tool, but FFmpeg source code, internal comments, identifiers,
and implementation-specific structures are not implementation source material
for this project.

See [License And Clean-Room Provenance](docs/LICENSE_AND_PROVENANCE.md) for the
full contribution rule.

## License

mffv1 library code is licensed under the MIT License. Third-party dependencies
retain their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
