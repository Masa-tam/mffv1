# Release Process

This document defines the release checklist for mffv1. It is intentionally
conservative because the project exists to provide a clean-room, permissively
licensed FFV1 implementation.

## Release Readiness

A stable release candidate should satisfy all of the following:

- Public API references match the installed headers.
- The current limitations in `DECODER_REFERENCE.md` and
  `ENCODER_REFERENCE.md` are accurate.
- `CHANGELOG.md` has a versioned release section with user-visible changes.
- `LICENSE`, `THIRD_PARTY_NOTICES.md`, and `docs/LICENSE_AND_PROVENANCE.md`
  are current.
- Optional committed external vectors, if any, have complete entries in
  `docs/test-vectors.md`.
- Local-only vector data and generator build outputs are absent from the commit.
- The package smoke test passes against the installed CMake package.

## Required Verification

Run the normal Visual Studio 2026 x64 workflow:

```powershell
cmake --preset vs2026-x64
cmake --build --preset vs2026-x64-debug
ctest --preset vs2026-x64-debug --output-on-failure
```

Build both Release diagnostic-message profiles:

```powershell
cmake --build --preset vs2026-x64-release
cmake --preset vs2026-x64-no-status
cmake --build --preset vs2026-x64-no-status-release
```

Verify the no-status `Status` contract:

```powershell
cmake --preset vs2026-x64-no-status-tests
cmake --build --preset vs2026-x64-no-status-tests-debug
ctest --preset vs2026-x64-no-status-tests-debug --output-on-failure
```

Run the sanitizer workflow when the toolchain supports it:

```powershell
cmake --preset vs2026-x64-asan
cmake --build --preset vs2026-x64-asan-debug
ctest --preset vs2026-x64-asan-debug --output-on-failure
```

Build the standalone fuzz harnesses before a release candidate:

```powershell
cmake --preset vs2026-x64-fuzz
cmake --build --preset vs2026-x64-fuzz-debug
ctest --preset vs2026-x64-fuzz-debug --output-on-failure
```

The fuzz CTest preset should include both empty-input and project-owned
seed-input smoke checks. Any retained regression corpus added for a release
should be project-owned or have recorded provenance.

Also verify a package consumer build:

```powershell
cmake `
  -DMFFV1_PACKAGE_SMOKE_GENERATOR="Visual Studio 18 2026" `
  -DMFFV1_PACKAGE_SMOKE_ARCHITECTURE=x64 `
  -P cmake\RunPackageSmoke.cmake
```

Repeat the package consumer check for the no-status Release install so the
generated and installed `mffv1/build_config.hpp` contract is verified:

```powershell
cmake `
  -DMFFV1_PACKAGE_SMOKE_PROFILE=no-status `
  -DMFFV1_PACKAGE_SMOKE_GENERATOR="Visual Studio 18 2026" `
  -DMFFV1_PACKAGE_SMOKE_ARCHITECTURE=x64 `
  -P cmake\RunPackageSmoke.cmake
```

## Versioning

Before `1.0.0`, source and binary compatibility may change between releases.
Each release tag should still document notable behavior changes in
`CHANGELOG.md`.

After `1.0.0`, use semantic versioning:

- Increment the major version for incompatible public API changes.
- Increment the minor version for backward-compatible feature additions.
- Increment the patch version for backward-compatible fixes.

## Release Artifacts

The repository release should include:

- Source archive.
- License and third-party notices.
- User-facing Markdown references under `docs/` and linked implementation
  notes under `plans/`, preserving the installed documentation directory
  layout.
- Changelog section for the released version.
- Any generated package artifacts that are intentionally published.

Do not publish local FFmpeg binaries, generated vector headers, MKV files, or
generator build products as mffv1 release artifacts unless their license and
provenance have been reviewed separately.
