---
name: Compatibility report
about: Report FFV1 bitstream behavior observed through black-box testing
title: ""
labels: compatibility
assignees: ""
---

## Summary

Describe the FFV1 compatibility issue and whether it affects decoding,
encoding, frame inspection, or legacy bootstrap.

## Stream Characteristics

- FFV1 version and micro-version:
- Entropy coding mode:
- Pixel format or plane layout:
- Bits per raw sample:
- Slice layout:
- Container, if relevant:

## Observed Behavior

Describe the mffv1 result and the expected result. Include status code,
diagnostic location, or first mismatching sample when available.

## Reproducer

Prefer synthetic, minimized, or redacted data. Do not upload private media,
FFmpeg source code, or generated data with unclear licensing.

## Provenance

Explain how the bitstream or observation was produced. Black-box observations
are welcome; copied implementation details are not.

- [ ] This report is based on public specifications, generated test data,
      black-box behavior, or independent reasoning.
- [ ] This report does not include code, comments, identifiers, internal tables,
      or implementation details copied or translated from FFmpeg or another
      incompatibly licensed implementation.
