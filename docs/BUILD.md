# mffv1 Build Notes

This project uses CMake and C++20. The primary target environment is Visual
Studio 2026 x64.

The expected CMake executable is:

```text
D:\Data\DevTemp\SDK_for_DevBase\Tools\cmake\bin\cmake.exe
```

## Configure

```powershell
& 'D:\Data\DevTemp\SDK_for_DevBase\Tools\cmake\bin\cmake.exe' --preset vs2026-x64
```

The preset uses the Visual Studio 2026 generator with the x64 architecture.

## Options

| Option | Default | Meaning |
| --- | --- | --- |
| `MFFV1_BUILD_TESTS` | `ON` | Build the GoogleTest-based unit and conformance tests. |
| `MFFV1_ENABLE_STATUS_MESSAGES` | `ON` | Store diagnostic text in `Status::message`. When `OFF`, `Status::message` remains part of the public API but library-generated messages are empty. |
| `MFFV1_ENABLE_WARNINGS_AS_ERRORS` | `OFF` | Treat compiler warnings as errors. |

## Build

```powershell
& 'D:\Data\DevTemp\SDK_for_DevBase\Tools\cmake\bin\cmake.exe' --build --preset vs2026-x64-debug
```

## Tests

GoogleTest is the project test framework. CMake first searches for it with
`find_package(GTest CONFIG QUIET)`, then falls back to the
`third-party/googletest` submodule.

If GoogleTest is not available and the submodule is not initialized, configure
with tests disabled:

```powershell
& 'D:\Data\DevTemp\SDK_for_DevBase\Tools\cmake\bin\cmake.exe' -S . -B build\smoke -DMFFV1_BUILD_TESTS=OFF
```

When GoogleTest is available, tests should run through CTest:

```powershell
& 'D:\Data\DevTemp\SDK_for_DevBase\Tools\cmake\bin\cmake.exe' --build --preset vs2026-x64-debug
& 'D:\Data\DevTemp\SDK_for_DevBase\Tools\cmake\bin\ctest.exe' --preset vs2026-x64-debug
```

## Install

The library installs public headers, the static library, and a CMake package
under `lib/cmake/mffv1`.

```powershell
& 'D:\Data\DevTemp\SDK_for_DevBase\Tools\cmake\bin\cmake.exe' --install build\vs2026-x64 --config Debug --prefix build\package-smoke\install
```

Consumers should use the exported target:

```cmake
find_package(mffv1 CONFIG REQUIRED)
target_link_libraries(app PRIVATE mffv1::mffv1)
```

## Package Smoke Test

`tests/package_smoke` is a minimal consumer project that includes only public
headers and links the installed `mffv1::mffv1` package target.

```powershell
& 'D:\Data\DevTemp\SDK_for_DevBase\Tools\cmake\bin\cmake.exe' -S tests\package_smoke -B build\package-smoke\build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="$PWD\build\package-smoke\install"
& 'D:\Data\DevTemp\SDK_for_DevBase\Tools\cmake\bin\cmake.exe' --build build\package-smoke\build --config Debug
& '.\build\package-smoke\build\Debug\mffv1_package_smoke.exe'
```
