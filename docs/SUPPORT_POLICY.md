# Support Policy

mffv1 is currently pre-release software. The API and binary layout may change
until a stable release is tagged.

## Supported Versions

| Version | Status |
| --- | --- |
| Unreleased main branch | Active development, no stability guarantee. |
| Pre-1.0 tags | Best-effort fixes only. |
| 1.x and later | To be defined when the first stable release is published. |

## Compatibility Policy

Before `1.0.0`, compatibility is best effort. Pre-1.0 releases are intended to
make the library usable and reviewable while the public API, package shape,
performance expectations, and interoperability coverage are still being proven.
Breaking changes are allowed when they improve correctness, safety, API
clarity, or release packaging. Public API changes should be recorded in
`CHANGELOG.md`, and the reference documents should be updated in the same
change.

Do not interpret a `0.x` release as a stable ABI promise. Consumers that need
long-term stability should pin an exact tag and review changelog entries before
upgrading.

After `1.0.0`, the project should avoid breaking source compatibility within a
major version unless a security or correctness issue requires it.

## Security And Correctness Reports

Reports should include:

- mffv1 commit or release tag.
- Build configuration and compiler.
- Minimal input data or reproduction steps when it can be shared safely.
- Whether the issue affects decoding, encoding, container integration, or test
  vector generation.

Do not attach FFmpeg source code or generated data with unclear licensing.
When the repository is public, use
[GitHub Issues](https://github.com/Masa-tam/mffv1/issues) for ordinary reports.
If a report needs private coordination because it contains sensitive input data,
contact the maintainer first and share only the minimum information needed to
arrange a safe disclosure path.

Video inputs may contain personal data, unreleased creative work, commercial
project material, or other confidential content. Prefer synthetic,
minimized, or redacted reproducers over original media. If the original media
is required to explain the problem, do not upload it to a public issue.

## Clean-Room Reporting Notes

Correctness reports may cite public specifications, independently generated
test vectors, or black-box behavior. Do not submit patches copied or translated
from FFmpeg or another implementation with an incompatible license.
