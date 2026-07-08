# mffv1 Plans And Design Notes

This directory contains historical plans, design notes, implementation
investigations, and review records for mffv1 development.

These files are useful for understanding why the code is shaped the way it is,
but they are not the primary user-facing reference. For implemented public API
contracts, prefer:

- [Documentation Index](../docs/README.md)
- [Decoder Reference](../docs/DECODER_REFERENCE.md)
- [Encoder Reference](../docs/ENCODER_REFERENCE.md)
- [Frame Buffer Reference](../docs/FRAME_BUFFER_REFERENCE.md)
- public headers under `include/mffv1/`
- unit tests under `tests/unit/`

When a plan document differs from the public references or tests, treat the
public references, headers, and tests as authoritative for current behavior.

## Documents

- [Architecture Design](DESIGN.md): original clean-room architecture and
  long-term module boundaries.
- [Phase 2 Class Specification](PHASE2_CLASS_SPEC.md): decoder construction
  plan and internal class boundaries.
- [Phase 5 Encoder Specification](PHASE5_ENCODER_SPEC.md): encoder
  architecture, entropy writers, slice assembly, threading, and SIMD
  boundaries.
- [Legacy Bootstrap Design](LEGACY_BOOTSTRAP_DESIGN.md): design notes for
  version 0/1 keyframe-embedded `Parameters()` initialization.
- [RFC 9043 Implementation Notes](rfc9043-implementation-notes.md): collected
  implementation interpretations and possible clarification topics.
- [Test Vector Compatibility Notes](test-vector-compatibility-notes.md):
  investigation notes for external vector compatibility work.
