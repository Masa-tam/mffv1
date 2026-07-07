# Contributing To mffv1

Thank you for considering a contribution. mffv1 is a clean-room, MIT-licensed
C++20 FFV1 implementation, so contribution provenance matters as much as code
quality.

## Clean-Room Rule

Contributions must be derived from public specifications, independently
generated tests, black-box interoperability observations, or original reasoning.

Do not submit code, comments, identifiers, internal tables, or implementation
details copied or translated from FFmpeg or another implementation with an
incompatible license. If a behavior is learned from black-box testing, document
the input conditions and observed result rather than citing implementation
source.

For details, see [License And Clean-Room Provenance](docs/LICENSE_AND_PROVENANCE.md).

## Issues

Use [GitHub Issues](https://github.com/Masa-tam/mffv1/issues) for ordinary bug
reports, compatibility findings, and feature requests.

Reports should include:

- mffv1 commit or release tag.
- Compiler, generator, platform, and relevant CMake options.
- Whether the issue affects decoding, encoding, container integration, package
  consumption, or test-vector generation.
- A minimal reproducer when it can be shared safely.

Do not upload private media, unreleased creative work, commercial project
material, personal data, FFmpeg source code, or generated data with unclear
licensing to a public issue. Prefer synthetic, minimized, or redacted
reproducers.

For sensitive reports, see [Security Policy](SECURITY.md) and
[Support Policy](docs/SUPPORT_POLICY.md).

## Patches

Keep patches focused and update the relevant public reference when behavior
changes. Public API changes should update headers, tests, references, and
`CHANGELOG.md` together.

The repository uses `CODEOWNERS` to route review to the maintainer. Dependabot
may open maintenance pull requests for GitHub Actions and Git submodules; those
updates still need the same clean-room and license-boundary review as ordinary
changes.

Release notes use GitHub labels through `.github/release.yml`. Add labels such
as `bug`, `compatibility`, `documentation`, or `maintenance` to pull requests
when they should appear in a specific release-note category.

The pull request template asks for clean-room confirmation, relevant test
commands, and documentation status. Treat unchecked items as an explicit note
that the step was not applicable or could not be run.

Before submitting a patch, run the most relevant verification:

```powershell
cmake --preset vs2026-x64
cmake --build --preset vs2026-x64-debug
ctest --preset vs2026-x64-debug --output-on-failure
```

For release-facing or packaging changes, also run:

```powershell
cmake -DMFFV1_MARKDOWN_LINK_ROOT=. -P cmake\CheckMarkdownLinks.cmake
cmake -P cmake\CheckPublicTrackedFiles.cmake
cmake `
  -DMFFV1_PACKAGE_SMOKE_GENERATOR="Visual Studio 18 2026" `
  -DMFFV1_PACKAGE_SMOKE_ARCHITECTURE=x64 `
  -P cmake\RunPackageSmoke.cmake
```

## Test Vectors

The `testvectors/` directory is an optional local workspace. Do not commit local
generated vector headers, FFmpeg binaries, downloaded media, or generator build
outputs by default.

Committed external vectors require recorded provenance and license review in
[Test Vector Registry](docs/test-vectors.md).
