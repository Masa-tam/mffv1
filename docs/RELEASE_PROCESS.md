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
- `CONTRIBUTING.md`, `SECURITY.md`, and `docs/SUPPORT_POLICY.md` point to the
  current public repository and reporting policy.
- GitHub issue and pull request templates still match the clean-room and
  verification policy.
- `CODEOWNERS` and Dependabot settings still route maintenance work to the
  intended maintainer and dependency boundaries.
- `.github/release.yml` categories match the labels used by merged pull
  requests for the release.
- Optional committed external vectors, if any, have complete entries in
  `docs/test-vectors.md`.
- Local-only vector data and generator build outputs are absent from the commit.
- The package smoke test passes against the installed CMake package.
- GitHub Actions smoke checks pass for the commit being released, when the
  repository is public.

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

Verify repository-owned Markdown links:

```powershell
cmake -DMFFV1_MARKDOWN_LINK_ROOT=. -P cmake\CheckMarkdownLinks.cmake
```

The checker excludes generated build output, Git metadata, local Codex
workspaces, and third-party dependencies by default.

Verify that tracked files do not include local-only maintainer notes, generated
test-vector payloads, generator archives, or build output:

```powershell
cmake -P cmake\CheckPublicTrackedFiles.cmake
```

Also verify package consumer builds. The package smoke runner checks installed
headers, the exported `mffv1::mffv1` target, static artifact shape, installed
documentation, and installed documentation links:

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
- Contributing, security, and support policy documents.
- User-facing Markdown references under `docs/`, preserving the installed
  documentation directory layout.
- Changelog section for the released version.
- Any generated static-library package artifacts that are intentionally
  published.

Do not publish local FFmpeg binaries, generated vector headers, MKV files, or
generator build products as mffv1 release artifacts unless their license and
provenance have been reviewed separately.
