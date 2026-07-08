# mffv1 Documentation

## Library User References

- [Encoder Reference](ENCODER_REFERENCE.md): encoder lifecycle, supported
  profiles, stream configuration, slices, threading, CPU dispatch, output, and
  current limitations.
- [Decoder Reference](DECODER_REFERENCE.md): decoder lifecycle, configuration,
  frame inspection, decoding, error handling, and current limitations.
- [Frame Buffer Reference](FRAME_BUFFER_REFERENCE.md): shared frame ownership,
  plane order, sample storage, dimensions, stride, and buffer examples.
- [Build Guide](BUILD.md): supported build workflow and CMake presets.
- [License And Provenance](LICENSE_AND_PROVENANCE.md): MIT licensing,
  clean-room rules, FFmpeg independence, and third-party boundaries.
- [Test Vector Registry](test-vectors.md): optional local vector workflow,
  supported external-vector test scope, and committed-vector provenance
  checklist.
- [Support Policy](SUPPORT_POLICY.md): pre-release compatibility and reporting
  expectations.
- [Security Policy](../SECURITY.md): reporting path for security-sensitive or
  confidential input issues.

## Maintainer References

- [Release Process](RELEASE_PROCESS.md): release readiness, verification, and
  artifact checklist for maintainers and downstream forks.
- [Contributing](../CONTRIBUTING.md): clean-room contribution rules and
  verification expectations.
- [Changelog](../CHANGELOG.md): user-visible project changes.

These references describe implemented public behavior and are the appropriate
starting point for library integration.

Design notes, historical plans, and implementation investigation records are
kept outside the user-facing documentation set. Public behavior should be
taken from the API references and public headers when documents differ.
