# Publication Checklist

This checklist covers repository settings that are not fully represented by
files in the source tree. Complete it when publishing `mffv1` or before the
first public release tag.

## Repository Identity

- Repository URL: `https://github.com/Masa-tam/mffv1`
- Description: `Clean-room C++20 FFV1 codec library`
- Website: leave empty unless a project site exists.
- Topics:
  - `ffv1`
  - `codec`
  - `video-codec`
  - `c-plus-plus`
  - `cpp20`
  - `clean-room`
  - `lossless-video`
  - `cmake`

## GitHub Features

- Enable Issues.
- Enable Discussions only if a public support forum is desired.
- Enable Actions.
- Enable Dependabot alerts.
- Enable Dependabot security updates when available for the repository.
- Keep Wikis disabled unless user documentation moves there deliberately.

## Branch Protection

For the default branch:

- Require pull request review before merge once outside personal-only
  development.
- Require the `CI / Windows smoke` check before merge when GitHub Actions is
  active.
- Require branches to be up to date before merge if the project starts taking
  external contributions.
- Do not allow force-pushes.
- Do not allow deletions.

## Labels

Ensure labels used by `.github/release.yml` exist:

- `breaking-change`
- `bug`
- `ci`
- `compatibility`
- `dependencies`
- `documentation`
- `enhancement`
- `feature`
- `maintenance`
- `skip-changelog`
- `third-party`

## First Public Release

- Confirm `README.md` accurately describes pre-release status.
- Confirm `SECURITY.md` and `docs/SUPPORT_POLICY.md` point to the current
  public repository.
- Confirm `CHANGELOG.md` has a versioned section before tagging.
- Confirm no local-only `testvectors/test_vector_data.hpp`, generated media,
  FFmpeg binaries, or generator build output is committed.
- Confirm the release verification in [Release Process](RELEASE_PROCESS.md)
  has passed.
