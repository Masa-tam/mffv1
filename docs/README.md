# mffv1 Documentation

## API References

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
- [Test Vector Registry](test-vectors.md): provenance checklist and committed
  external vector inventory.
- [Release Process](RELEASE_PROCESS.md): release readiness, verification, and
  artifact checklist.
- [Support Policy](SUPPORT_POLICY.md): pre-release compatibility and reporting
  expectations.
- [Changelog](../CHANGELOG.md): user-visible project changes.

These references describe implemented public behavior and are the appropriate
starting point for library integration.

## Design And Implementation Documents

- [Architecture Design](DESIGN.md): project-wide clean-room architecture and
  long-term module boundaries.
- [Phase 2 Class Specification](PHASE2_CLASS_SPEC.md): decoder-oriented class
  contracts and implementation boundaries.
- [Phase 5 Encoder Specification](PHASE5_ENCODER_SPEC.md): encoder internals,
  entropy writers, slice assembly, threading, and SIMD boundaries.

Design documents may include future work. Public behavior should be taken from
the API references and public headers when the documents differ.
