# mffv1
![mffv1 logo](https://github.com/user-attachments/assets/64c86829-1bd3-450c-a6bd-1a7a26c89ecb)

[![CI](https://github.com/Masa-tam/mffv1/actions/workflows/ci.yml/badge.svg)](https://github.com/Masa-tam/mffv1/actions/workflows/ci.yml)

mffv1 is a specification-driven independent C++20 implementation of the FFV1
video codec. It is designed as a modular codec library rather than a container
or FFmpeg wrapper.

Repository: [https://github.com/Masa-tam/mffv1](https://github.com/Masa-tam/mffv1)

The project is currently pre-release software. The decoder and encoder already
cover the main version 3 profiles used by the test suite, but the public API
and compatibility guarantees should still be treated as provisional until a
stable `1.0.0` release is tagged. The first public release version is `0.1.0`.

## Goals

- Specification-driven implementation based on RFC 9043 and project-owned
  tests.
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
- [License And Development Provenance](docs/LICENSE_AND_PROVENANCE.md)
- [External Test Vector Registry](testvectors/REGISTRY.md)
- [Support Policy](docs/SUPPORT_POLICY.md)
- [Security Policy](SECURITY.md)
- [Contributing](CONTRIBUTING.md)
- [Release Process](docs/RELEASE_PROCESS.md)
- [Changelog](CHANGELOG.md)

## Development Provenance

mffv1 was developed against the FFV1 specification published as RFC 9043. The
development workflow did not use FFmpeg source code as an implementation
reference. FFmpeg executables may be used locally as external black-box
interoperability tools by comparing encoded and decoded outputs.

The implementation was developed with the assistance of AI coding agents. No
claim is made that the agents or their underlying models were trained without
exposure to FFmpeg or other existing FFV1 implementations. Accordingly, this
project is described as a specification-driven independent implementation
rather than a formal clean-room implementation.

See [License And Development Provenance](docs/LICENSE_AND_PROVENANCE.md) for
the full contribution rule.

## License

mffv1 library code is licensed under the MIT License. Third-party dependencies
retain their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
