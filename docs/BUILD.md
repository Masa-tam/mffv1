# mffv1 Build Notes

This project uses CMake and C++20. The primary target environment is Visual
Studio 2026 x64.

CMake 3.25 or newer is required. The examples below assume `cmake` and
`ctest` are available through the developer's shell or build environment.

## Configure

```powershell
cmake --preset vs2026-x64
```

The preset uses the Visual Studio 2026 generator with the x64 architecture.

## Options

| Option | Default | Meaning |
| --- | --- | --- |
| `MFFV1_BUILD_FUZZERS` | `OFF` | Build standalone fuzz harness executables. |
| `MFFV1_BUILD_STATUS_CONTRACT_TESTS_ONLY` | `OFF` | Build only the lightweight `Status` contract tests. Intended for the no-status test preset. |
| `MFFV1_BUILD_TESTS` | `ON` | Build the GoogleTest-based unit and conformance tests. |
| `MFFV1_ENABLE_SANITIZERS` | `OFF` | Enable supported compiler sanitizer instrumentation. MSVC uses AddressSanitizer; Clang and GCC use AddressSanitizer plus UndefinedBehaviorSanitizer. |
| `MFFV1_ENABLE_STATUS_MESSAGES` | `ON` | Store diagnostic text in `Status::message`. When `OFF`, `Status::message` remains part of the public API but library-generated messages are empty. |
| `MFFV1_ENABLE_WARNINGS_AS_ERRORS` | `OFF` | Treat compiler warnings as errors. |

## Build

```powershell
cmake --build --preset vs2026-x64-debug
```

Release builds can keep diagnostic status messages or omit them:

```powershell
cmake --build --preset vs2026-x64-release
cmake --preset vs2026-x64-no-status
cmake --build --preset vs2026-x64-no-status-release
```

`vs2026-x64-release` uses the default `MFFV1_ENABLE_STATUS_MESSAGES=ON`.
`vs2026-x64-no-status-release` keeps the `Status::message` field in the public
API but leaves library-generated messages empty to reduce diagnostic string
storage and copying in production-oriented builds. The no-status preset also
sets `MFFV1_BUILD_TESTS=OFF` because many unit tests intentionally assert exact
diagnostic text.

When consuming an installed no-status build, prefer the installed headers from
the same build. CMake generates and installs `mffv1/build_config.hpp`, which
records the matching `MFFV1_ENABLE_STATUS_MESSAGES` value. Projects that copy
only part of the include tree or mix headers from another build must define the
same macro value used to build the library.

To verify the no-status `Status` contract without building or running the
diagnostics-heavy unit tests:

```powershell
cmake --preset vs2026-x64-no-status-tests
cmake --build --preset vs2026-x64-no-status-tests-debug
ctest --preset vs2026-x64-no-status-tests-debug --output-on-failure
```

## Tests

GoogleTest is the project test framework. CMake first searches for it with
`find_package(GTest CONFIG QUIET)`, then falls back to the
`third-party/googletest` submodule.

If GoogleTest is not available and the submodule is not initialized, configure
with tests disabled:

```powershell
cmake -S . -B build\smoke -DMFFV1_BUILD_TESTS=OFF
```

When GoogleTest is available, tests should run through CTest:

```powershell
cmake --build --preset vs2026-x64-debug
ctest --preset vs2026-x64-debug
```

The GitHub Actions workflow uses hosted Windows and Ubuntu images as
portability smoke checks. Windows intentionally uses standard CMake
configure/build/test commands instead of the Visual Studio 2026-specific
preset, because the hosted image may not match the primary local development
toolchain. Ubuntu builds run both GCC and Clang Debug smoke jobs.

## Sanitizer Build

Use the sanitizer preset when checking memory safety issues before release or
after parser, entropy, threading, or buffer-boundary changes:

```powershell
cmake --preset vs2026-x64-asan
cmake --build --preset vs2026-x64-asan-debug
ctest --preset vs2026-x64-asan-debug
```

The Visual Studio preset enables MSVC AddressSanitizer. On Clang and GCC,
`MFFV1_ENABLE_SANITIZERS=ON` enables AddressSanitizer and
UndefinedBehaviorSanitizer.

When tests use the bundled `third-party/googletest` submodule, the sanitizer
flags are applied to GoogleTest as well. If an externally installed GoogleTest
package is used instead, it must be built with sanitizer-compatible options.

## Fuzz Harnesses

Fuzz harnesses are opt-in and build as standalone executables that accept zero
or more input files. When no file is supplied, each harness reads bytes from
standard input.

```powershell
cmake --preset vs2026-x64-fuzz
cmake --build --preset vs2026-x64-fuzz-debug
ctest --preset vs2026-x64-fuzz-debug
.\build\vs2026-x64-fuzz\fuzz\Debug\mffv1_fuzz_configuration_record.exe sample.bin
.\build\vs2026-x64-fuzz\fuzz\Debug\mffv1_fuzz_frame_decode.exe sample.bin
.\build\vs2026-x64-fuzz\fuzz\Debug\mffv1_fuzz_encoder.exe sample.bin
```

The current harnesses exercise Configuration Record parsing, frame inspection
and decoding, and encoder input handling through the public API. They are
intended as corpus and sanitizer entry points; they do not define compatibility
behavior by themselves.

The fuzz CTest preset runs empty-input and project-owned seed-input smoke
checks for each harness. The seed files under `fuzz/corpus/` are not
conformance vectors; they only keep the standalone file-input path exercised.

## Install

The library installs public headers, the static library, user-facing Markdown
references under `share/doc/mffv1`, and a CMake package under
`lib/cmake/mffv1`. The installed documentation preserves the repository
`docs/` subdirectory so relative links remain valid.

The supported package artifact is currently a static library. Shared-library
exports are intentionally not part of the release surface yet because the
public C++ API uses STL types and `std::unique_ptr` ownership across the
factory boundary, and the project has not declared a stable cross-DLL ABI.
The `mffv1` target is declared `STATIC`, so `BUILD_SHARED_LIBS` does not change
the library artifact type.

```powershell
cmake --install build\vs2026-x64 --config Debug --prefix build\package-smoke\install
```

Consumers should use the exported target:

```cmake
find_package(mffv1 CONFIG REQUIRED)
target_link_libraries(app PRIVATE mffv1::mffv1)
```

## Package Smoke Test

`tests/package_smoke` is a minimal consumer project that builds two installed
package checks: a public-header smoke executable and a link smoke executable
that creates encoder and decoder instances through the exported
`mffv1::mffv1` package target.
The package smoke runner also verifies that required installed Markdown files
exist and that local Markdown links under the installed documentation root
resolve.

```powershell
cmake `
  -DMFFV1_PACKAGE_SMOKE_GENERATOR="Visual Studio 18 2026" `
  -DMFFV1_PACKAGE_SMOKE_ARCHITECTURE=x64 `
  -P cmake\RunPackageSmoke.cmake
```

To check repository-owned Markdown links without installing the package:

```powershell
cmake -DMFFV1_MARKDOWN_LINK_ROOT=. -P cmake\CheckMarkdownLinks.cmake
```

The checker excludes generated build output, Git metadata, local Codex
workspaces, and third-party dependencies by default.

The smoke project also checks the installed `mffv1/build_config.hpp` value.
For a no-status install, use the no-status smoke profile:

```powershell
cmake `
  -DMFFV1_PACKAGE_SMOKE_PROFILE=no-status `
  -DMFFV1_PACKAGE_SMOKE_GENERATOR="Visual Studio 18 2026" `
  -DMFFV1_PACKAGE_SMOKE_ARCHITECTURE=x64 `
  -P cmake\RunPackageSmoke.cmake
```
