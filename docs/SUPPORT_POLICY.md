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

Before `1.0.0`, compatibility is best effort. Public API changes should be
recorded in `CHANGELOG.md`, and the reference documents should be updated in
the same change.

After `1.0.0`, the project should avoid breaking source compatibility within a
major version unless a security or correctness issue requires it.

## Security And Correctness Reports

Reports should include:

- mffv1 commit or release tag.
- Build configuration and compiler.
- Minimal input data or reproduction steps when it can be shared.
- Whether the issue affects decoding, encoding, container integration, or test
  vector generation.

Do not attach FFmpeg source code or generated data with unclear licensing.
When the repository is public, use GitHub Issues for ordinary reports. If a
report needs private coordination because it contains sensitive input data,
contact the maintainer first and share only the minimum information needed to
arrange a safe disclosure path.

## Clean-Room Reporting Notes

Correctness reports may cite public specifications, independently generated
test vectors, or black-box behavior. Do not submit patches copied or translated
from FFmpeg or another implementation with an incompatible license.
