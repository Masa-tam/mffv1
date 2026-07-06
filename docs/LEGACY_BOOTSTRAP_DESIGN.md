# Legacy Frame Bootstrap Design

This document designs future decoder support for FFV1 version 0/1 streams whose
`Parameters()` syntax is embedded in keyframes instead of provided through an
external Configuration Record or Codec Private block.

This is a design document, not implemented public behavior. The current
decoder still requires callers to configure version 0/1 streams before
`inspect_frame()` or `decode_frame()`.

## Problem

FFV1 version 3 stores stream parameters in a Configuration Record, which maps
cleanly to `IDecoder::configure()`.

FFV1 version 0/1 may have no Codec Private data. In that case the first
keyframe carries the initialization data in the frame bitstream:

```text
Frame
  keyframe
  Parameters()       when keyframe and no external configuration exists
  Slice content
```

For legacy containers such as AVI, an adapter may be able to provide coded
width and height but may not be able to extract `Parameters()` without running
FFV1 entropy parsing. mffv1 therefore needs a decoder-side bootstrap path.

## Goals

- Let callers initialize a decoder from a complete legacy keyframe payload.
- Let callers detect when a later legacy keyframe embeds parameters that differ
  from the current decoder configuration.
- Avoid implicit reconfiguration during `decode_frame()`.
- Preserve the existing strict error model and structured `Status`.
- Keep version 3 behavior unchanged.
- Keep public ownership simple: no caller-owned parsed-parameter structures.

## Non-Goals

- Do not demultiplex containers.
- Do not guess frame dimensions when the container does not provide them.
- Do not silently accept parameter changes that would invalidate reference
  state.
- Do not make `Status::message` a programmatic parameter-change signal.

## Proposed Public Types

The public API should expose parameter state as data, not as diagnostic text:

```cpp
enum class LegacyBootstrapState : std::uint8_t {
    NoEmbeddedParameters,
    Configured,
    MatchesCurrentConfiguration,
    DiffersFromCurrentConfiguration,
};

struct LegacyBootstrapInfo {
    LegacyBootstrapState state = LegacyBootstrapState::NoEmbeddedParameters;
    FrameInfo frame_info;
};

struct LegacyBootstrapResult {
    Status status;
    LegacyBootstrapInfo info;
};
```

The enum gives adapters a stable branch point:

- `Configured`: the decoder was not configured and is now configured from the
  keyframe.
- `MatchesCurrentConfiguration`: embedded parameters were present and are
  equivalent to the current stream configuration.
- `DiffersFromCurrentConfiguration`: embedded parameters were present but differ
  from the current stream configuration. The decoder remains unchanged.
- `NoEmbeddedParameters`: the frame did not contain bootstrap parameters.

## Proposed Decoder API

Add an explicit method to `IDecoder`:

```cpp
virtual LegacyBootstrapResult bootstrap_legacy_frame(ByteSpan frame_payload) = 0;
```

This method parses enough of a complete version 0/1 keyframe to read embedded
`Parameters()` and the first frame envelope.

Behavior:

- If the decoder is unconfigured and the frame has embedded parameters, parse
  them, apply container-provided dimensions from `DecoderOptions`, configure
  the decoder, clear reference state, and return `Configured`.
- If the decoder is configured and the embedded parameters match the current
  configuration, leave the decoder state unchanged and return
  `MatchesCurrentConfiguration`.
- If the decoder is configured and the embedded parameters differ, leave the
  decoder state unchanged and return `DiffersFromCurrentConfiguration` with
  `status.ok() == true`.
- If the frame does not contain embedded parameters, return
  `NoEmbeddedParameters`. If the decoder is unconfigured this is not sufficient
  to decode the frame, but it is not a syntax error by itself.
- If parsing fails, return a non-OK `Status` and leave the decoder unchanged.

`decode_frame()` should not auto-bootstrap. Hidden reconfiguration would make
reference-state lifetime surprising and would make changed parameters hard for
adapters to handle deliberately.

## Decode Workflow

A legacy container adapter can use:

```text
create_decoder(options with frame_width/frame_height)

for each frame:
  if frame may be a keyframe:
    result = decoder->bootstrap_legacy_frame(frame_payload)
    if result.state == DiffersFromCurrentConfiguration:
      create or reconfigure a fresh decoder from this same frame
      discard old reference state
  decode_frame(frame_payload, output)
```

The first keyframe can therefore both configure and decode. If a later keyframe
changes parameters, the adapter receives a stable signal before decode and can
restart decoding from that frame.

## Parameter Equivalence

Comparison must use normalized stream parameters after applying container
dimensions from `DecoderOptions`.

Fields that should compare:

- version
- entropy mode and state transition table
- colorspace
- bit depth
- chroma and extra-plane flags
- chroma subsampling
- quantization tables and context counts
- slice raster dimensions
- intra-only flag when present

For version 0/1, coded dimensions are external to FFV1 parameters. They should
come from `DecoderOptions::frame_width` and `DecoderOptions::frame_height` and
be part of the normalized comparison.

## Parser Requirements

The bootstrap parser must preserve the boundary between embedded `Parameters()`
and frame slice content.

For range-coded legacy streams, the range coder state after `Parameters()`
matters. The existing single-slice legacy range path currently replays the
frame header before decoding content. Bootstrap support must extend that model
so the slice descriptor can represent:

- keyframe symbol consumed,
- optional embedded `Parameters()` consumed,
- content byte offset,
- whether slice content continues from the frame range-coder state.

For Golomb-Rice legacy streams, a bit-level reader is required. The current
Golomb-Rice reader is sample-difference oriented and does not yet expose a
general `ur` `SymbolReader` for headers or parameters.

## Suggested Implementation Stages

1. Add internal `LegacyFrameBootstrapParser` that can parse version 0/1 range
   keyframe `Parameters()` into `StreamParameters` without committing decoder
   state.
2. Add parameter-equivalence tests for equal and changed normalized streams.
3. Add public result types and `IDecoder::bootstrap_legacy_frame()`.
4. Decode the same bootstrap frame after configuration for single-slice range
   streams.
5. Extend the model to legacy range multi-slice content once content boundary
   replay is fully represented.
6. Add Golomb-Rice bootstrap only after a general Golomb-Rice `ur` symbol reader
   exists.

## Error Policy

Parameter changes are not errors when reported by `bootstrap_legacy_frame()`.
They are stream events that the caller can handle.

Malformed embedded parameters, unsupported features, or truncated frames remain
normal non-OK `Status` failures and must leave decoder state unchanged.
