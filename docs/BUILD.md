# mffv1 Build Notes

This project uses CMake and C++20. The primary target environment is Visual
Studio 2026 x64.

The expected CMake executable is:

```text
e:\data\SDK_for_DevBase\tools\cmake\bin\cmake.exe
```

## Configure

```powershell
& 'e:\data\SDK_for_DevBase\tools\cmake\bin\cmake.exe' --preset vs2026-x64
```

The preset uses the Visual Studio 2026 generator with the x64 architecture.

## Build

```powershell
& 'e:\data\SDK_for_DevBase\tools\cmake\bin\cmake.exe' --build --preset vs2026-x64-debug
```

## Tests

GoogleTest is the project test framework. CMake first searches for it with
`find_package(GTest CONFIG QUIET)`, then falls back to the
`third-party/googletest` submodule.

If GoogleTest is not available and the submodule is not initialized, configure
with tests disabled:

```powershell
& 'e:\data\SDK_for_DevBase\tools\cmake\bin\cmake.exe' -S . -B build\smoke -DMFFV1_BUILD_TESTS=OFF
```

When GoogleTest is available, tests should run through CTest:

```powershell
& 'e:\data\SDK_for_DevBase\tools\cmake\bin\cmake.exe' --build --preset vs2026-x64-debug
& 'e:\data\SDK_for_DevBase\tools\cmake\bin\ctest.exe' --preset vs2026-x64-debug
```
