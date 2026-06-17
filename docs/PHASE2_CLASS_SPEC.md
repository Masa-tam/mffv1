# Phase 2 Class Specification: Scalar Base Decoder

This document specifies the C++ classes and module boundaries for Phase 2 of
the FFV1 clean implementation. It is written for LLM implementation agents and
human reviewers. The goal is to make the scalar baseline decoder easy to build,
test, and later optimize with SIMD or slice-level multithreading.

Phase 2 starts from the easiest conforming decode path and grows toward full
coverage. The first target is a scalar, single-threaded decoder path for a
small FFV1 version 3 profile. The design must not prevent adding FFV1 versions
0 and 1, Golomb-Rice coding, RGB paths, CRC checks, SIMD dispatch, or slice
parallelism later.

Normative reference:

- RFC 9043, "FFV1 Video Coding Format Versions 0, 1, and 3":
  https://www.rfc-editor.org/rfc/rfc9043.html

## Phase 2 Scope

Phase 2 implements the base C++ decoding infrastructure. It should produce real
decoded samples for at least one deliberately narrow profile before expanding.

Required in Phase 2:

- Internal stream parameter representation.
- Configuration record parser skeleton with strict validation.
- Frame parser skeleton.
- Slice discovery and scalar slice decode pipeline.
- Range coder base implementation.
- Context state storage for range-coded symbols.
- Median predictor and sample reconstruction helpers.
- Plane-window validation against caller-provided `MutableFrameView`.
- Tests for each component before broad codec integration.

Allowed to remain incomplete in early Phase 2 commits:

- Encoder.
- Golomb-Rice entropy mode.
- RGB transform.
- Multithreaded scheduling.
- SIMD kernels.
- Full conformance matrix.

## First Decode Profile

The recommended first profile is:

- FFV1 version 3.
- Range coder entropy mode.
- Intra frame.
- Single slice.
- 8-bit sample depth.
- Planar Y-only, then planar YCbCr without chroma subsampling.
- Strict mode.
- CRC validation may initially parse-but-not-enforce, then become mandatory.

This profile is intentionally small. The class design below still includes the
extension points needed for the full codec.

## Ownership Model

```text
DecoderImpl
  owns StreamParameters
  owns reusable FrameScratch
  owns ScalarDispatch

FrameDecodeContext
  borrows immutable StreamParameters
  borrows output MutableFrameView
  owns parsed SliceDescriptor list

SliceDecoder
  borrows immutable StreamParameters
  borrows one SliceDescriptor
  borrows one output SliceOutputWindow
  owns SliceState
```

Rules:

- Public API classes remain pure virtual in `include/ffv1`.
- Concrete classes live under `src`.
- Public headers do not expose parsing, entropy, predictor, or slice state.
- Immutable configuration can be shared freely.
- Mutable coding state is frame-local or slice-local.
- Scalar implementation is the reference path for all future optimized paths.

## Public API Classes

### `ffv1::IDecoder`

Location: `include/ffv1/codec.hpp`

Purpose:

- Stable public decoder interface.
- Hide all concrete implementation details.

Methods:

- `Status configure(ByteSpan configuration_record)`
  Parses and validates the stream configuration record.

- `Status inspect_frame(ByteSpan frame_payload, FrameInfo& out_info) const`
  Performs lightweight frame inspection without writing pixels.

- `Status decode_frame(ByteSpan frame_payload, MutableFrameView output)`
  Decodes one compressed frame into caller-owned planes.

Implementation class:

- `ffv1::codec::DecoderImpl`, hidden in `src/codec`.

### `ffv1::IEncoder`

Location: `include/ffv1/codec.hpp`

Phase 2 status:

- Stays as API skeleton.
- May continue returning `ErrorCode::NotImplemented`.
- Must keep build and public ABI stable while decoder internals evolve.

## Core Internal Value Types

### `ffv1::codec::StreamParameters`

Location: `src/ffv1/stream_parameters.hpp`

Purpose:

- Immutable normalized FFV1 stream configuration.
- Remove syntax-level optional/default handling from hot decode paths.

Fields:

- `int version`
- `int micro_version`
- `EntropyMode entropy_mode`
- `int width`
- `int height`
- `int bits_per_raw_sample`
- `int colorspace_type`
- `bool chroma_planes`
- `bool extra_plane`
- `int log2_h_chroma_subsample`
- `int log2_v_chroma_subsample`
- `int num_h_slices`
- `int num_v_slices`
- `std::vector<QuantTableSet> quant_table_sets`
- `std::vector<InitialState> initial_states`
- `bool error_status_enabled`

Invariants:

- Dimensions are positive before frame parsing. They may come from an external
  container-facing API field rather than the FFV1 configuration record itself.
- Bit depth is within the supported Phase 2 subset.
- Slice counts are positive.
- Quantization tables are normalized before assignment.

### `ffv1::codec::FrameDecodeContext`

Location: `src/codec/frame_decode_context.hpp`

Purpose:

- Own one frame decode operation's parsed metadata.
- Keep frame-level parsing separate from slice execution.

Fields:

- `const StreamParameters* stream`
- `MutableFrameView output`
- `std::vector<SliceDescriptor> slices`
- `FrameInfo frame_info`
- `Diagnostics diagnostics`

Invariants:

- `stream` is never null after construction.
- `output` is validated before any slice writes.
- Slice descriptors do not overlap outside allowed plane windows.

### `ffv1::codec::SliceDescriptor`

Location: `src/ffv1/slice_descriptor.hpp`

Purpose:

- Describe a slice payload and its output rectangle.
- Later becomes the unit of work for multithreaded decode.

Fields:

- `std::uint32_t index`
- `std::uint32_t x`
- `std::uint32_t y`
- `std::uint32_t width`
- `std::uint32_t height`
- `ByteSpan payload`
- `std::uint64_t payload_byte_offset`
- `std::uint32_t expected_crc`
- `bool has_crc`

Invariants:

- Rectangle is inside the coded frame.
- Payload span points into the frame payload and remains valid during decode.
- Slice index is stable and used in diagnostics.

### `ffv1::codec::SliceOutputWindow`

Location: `src/codec/slice_output_window.hpp`

Purpose:

- Map one slice rectangle to writable plane spans.
- Centralize stride, bit depth, and plane bounds validation.

Fields:

- Plane-specific pointers and strides.
- Plane-specific slice dimensions after chroma subsampling.
- Sample format.

Methods:

- `Status validate(const StreamParameters&, MutableFrameView, const SliceDescriptor&)`
- `std::uint8_t* row_u8(PlaneId plane, std::uint32_t y)`
- `std::uint16_t* row_u16(PlaneId plane, std::uint32_t y)`

Invariants:

- Row accessors are valid only after successful validation.
- Accessors never compute addresses outside caller-provided planes.

## Bitstream And Syntax Classes

### `ffv1::bitstream::BitReader`

Location: `src/bitstream/bit_reader.hpp`

Phase 2 changes:

- Keep current MSB-first bit reading behavior.
- Add syntax helpers only when RFC parsing needs them.
- Do not mix entropy coder state into this class.

Required additions:

- `Status skip_bits(std::uint64_t bit_count)`
- `Status require_byte_aligned() const`
- Optional diagnostic byte offset helpers.

### `ffv1::ffv1::ConfigurationParser`

Location: `src/ffv1/configuration_parser.hpp`

Purpose:

- Parse FFV1 configuration records into `StreamParameters`.
- Apply RFC defaults and validate supported profile constraints.

Construction:

```cpp
class ConfigurationParser {
public:
    Status parse(ByteSpan record, StreamParameters& out_stream);
};
```

Responsibilities:

- Consume only configuration syntax.
- Validate version, micro version, coder type, colorspace, bit depth, and slice
  grid.
- Build normalized quantization table sets.
- Report unsupported but valid features as `UnsupportedFeature`.
- Report malformed records as `SyntaxError`.

Non-responsibilities:

- Frame payload parsing.
- Container metadata parsing.
- Output buffer validation.

### `ffv1::ffv1::FrameParser`

Location: `src/ffv1/frame_parser.hpp`

Purpose:

- Parse one compressed frame payload.
- Discover slices before slice decoding starts.

Construction:

```cpp
class FrameParser {
public:
    explicit FrameParser(const StreamParameters& stream);

    Status parse(ByteSpan payload, FrameDecodeContext& out_frame) const;
};
```

Responsibilities:

- Validate frame-level syntax.
- Locate slice payload ranges.
- Fill `SliceDescriptor` values.
- Keep slice payload bytes borrowed from the input frame payload.

Non-responsibilities:

- Entropy symbol decoding.
- Pixel reconstruction.
- Thread scheduling.

## Entropy Classes

### `ffv1::entropy::SymbolReader`

Location: `src/entropy/symbol_reader.hpp`

Purpose:

- Design boundary for entropy-coded symbol reads.
- Allows range coder and Golomb-Rice implementations to share decoder pipeline
  code later.

Interface:

```cpp
class SymbolReader {
public:
    virtual ~SymbolReader() = default;
    virtual Status read_symbol(ContextId context, std::int32_t& out_value) = 0;
};
```

Implementation note:

- Hot paths may later replace virtual calls with templates or function tables.
- Phase 2 may use a concrete range coder directly if it keeps this boundary
  visible in class names and tests.

### `ffv1::entropy::RangeCoder`

Location: `src/entropy/range_coder.hpp`

Purpose:

- Decode range-coded FFV1 symbols.

Construction:

```cpp
class RangeCoder final {
public:
    Status reset(ByteSpan payload, const InitialStateTable& initial_state);
    Status read_symbol(ContextId context, std::int32_t& out_value);
    std::uint64_t byte_position() const noexcept;
};
```

State:

- Borrowed compressed payload.
- Range coder registers.
- Context probability states.
- Diagnostic byte position.

Invariants:

- Reads never pass the payload end.
- Context indexes are validated before use.
- State updates match RFC arithmetic exactly.

Testing:

- Initialization from known byte sequences.
- Underflow handling.
- Context index validation.
- Round-trip with a future encoder or project-owned generated fixtures.

### `ffv1::entropy::ContextState`

Location: `src/entropy/context_state.hpp`

Purpose:

- Store adaptive state for one slice entropy coder.

Fields:

- Range coder probability state arrays.
- Optional counters needed by Golomb-Rice later.

Rules:

- Owned by `SliceState`.
- Never shared across slices.
- Reset from immutable stream initial states at slice start.

## Prediction And Reconstruction Classes

### `ffv1::ffv1::QuantTableSet`

Location: `src/ffv1/quant_table.hpp`

Purpose:

- Store normalized quantization table data used by context derivation.

Fields:

- Per-table quantization entries.
- Precomputed lookup ranges where useful.

Rules:

- Built during configuration parsing.
- Immutable during frame and slice decode.
- Scalar lookup is the reference for any future SIMD lookup acceleration.

### `ffv1::ffv1::Predictor`

Location: `src/ffv1/predictor.hpp`

Purpose:

- Provide pure scalar sample prediction and reconstruction helpers.

Interface:

```cpp
class Predictor {
public:
    static std::int32_t median_predict(std::int32_t left,
                                       std::int32_t top,
                                       std::int32_t top_left) noexcept;

    static std::int32_t reconstruct(std::int32_t prediction,
                                    std::int32_t difference,
                                    std::uint8_t bits_per_raw_sample) noexcept;
};
```

Rules:

- No internal mutable state.
- No direct frame or slice pointer ownership.
- All behavior must be unit-tested before use in `SliceDecoder`.

SIMD relevance:

- This class defines the scalar behavior that SIMD predictor kernels must match.
- SIMD implementations should live behind dispatch classes and compare against
  this class in tests.

### `ffv1::ffv1::LineState`

Location: `src/ffv1/line_state.hpp`

Purpose:

- Hold neighboring samples needed to decode a slice line-by-line.

Fields:

- Previous line samples.
- Current line samples.
- Optional per-plane border state.

Rules:

- Owned by `SliceState`.
- Reset at slice boundary.
- Supports 8-bit and 16-bit storage with a common scalar access path.

## Slice Decode Classes

### `ffv1::codec::SliceState`

Location: `src/codec/slice_state.hpp`

Purpose:

- Own mutable state for one slice decode.

Fields:

- `entropy::ContextState context_state`
- `std::vector<ffv1::LineState> line_states`
- Plane-local counters and diagnostics.

Rules:

- Constructed per slice or reused only after explicit reset.
- Not thread-safe.
- Never shared between slices.

### `ffv1::codec::SliceDecoder`

Location: `src/codec/slice_decoder.hpp`

Purpose:

- Decode one slice from entropy-coded symbols into output planes.

Construction:

```cpp
class SliceDecoder {
public:
    SliceDecoder(const StreamParameters& stream, const ScalarDispatch& dispatch);

    Status decode(const SliceDescriptor& slice,
                  SliceOutputWindow& output,
                  SliceState& state) const;
};
```

Responsibilities:

- Initialize entropy reader for the slice.
- Decode samples in RFC order.
- Derive contexts.
- Call scalar predictor/reconstruction helpers.
- Write reconstructed samples to `SliceOutputWindow`.
- Report syntax and bounds errors with slice index.

Non-responsibilities:

- Parsing frame envelope.
- Allocating output frames.
- Starting worker threads.
- Choosing SIMD implementation.

Phase 2 restriction:

- Single-threaded call sequence only.
- Scalar dispatch only.

## Dispatch And Future Optimization Classes

### `ffv1::codec::ScalarDispatch`

Location: `src/codec/scalar_dispatch.hpp`

Purpose:

- Explicit table of scalar operations used by decode pipeline.
- Creates the same call boundary that SIMD dispatch will later replace.

Fields:

- Function pointer or lightweight callable for median predictor.
- Function pointer or lightweight callable for reconstruction/wrap.
- Future copy/packing hooks.

Rules:

- Default dispatch always exists.
- Tests can force scalar dispatch.
- No runtime CPU detection in Phase 2.

### `ffv1::codec::SimdDispatch`

Location: future `src/simd`

Phase 2 status:

- Not implemented.
- Public API already accepts `CpuFeatures`.
- `DecoderImpl` should preserve the option value but select scalar dispatch.
- Class names and decode boundaries should make SIMD insertion obvious later.

Future dispatch rule:

```text
CpuFeatures + compiled architecture
  -> DispatchFactory
  -> ScalarDispatch or SimdDispatch
  -> SliceDecoder uses selected operations
```

## Decoder Implementation Class

### `ffv1::codec::DecoderImpl`

Location: `src/codec/decoder.cpp`, later split into `decoder_impl.hpp/.cpp`

Purpose:

- Concrete implementation behind `IDecoder`.
- Own configured stream parameters and reusable decode helpers.

Fields:

- `DecoderOptions options_`
- `std::optional<StreamParameters> stream_`
- `ScalarDispatch scalar_dispatch_`
- Reusable scratch buffers, once needed.

Methods:

- `Status configure(ByteSpan configuration_record)`
- `Status inspect_frame(ByteSpan frame_payload, FrameInfo& out_info) const`
- `Status decode_frame(ByteSpan frame_payload, MutableFrameView output)`

Decode flow:

```text
decode_frame()
  -> ensure configured
  -> validate output frame view
  -> FrameParser::parse_with_range_header() for version >= 3
     or FrameParser::parse() for legacy headerless Phase 2 path
  -> for each SliceDescriptor:
       -> SliceOutputWindow::validate()
       -> SliceState::reset()
       -> SliceDecoder::decode()
  -> return first error or success
```

Rules:

- `configure()` replaces previous stream state only after successful parse.
- `decode_frame()` never writes output before validating plane windows.
- `inspect_frame()` may parse frame envelope but must not run entropy decode.
- In Phase 2, `options_.thread_count` and `options_.cpu` are accepted but do not
  change execution behavior.

## Validation Classes

### `ffv1::codec::FrameValidator`

Location: `src/codec/frame_validator.hpp`

Purpose:

- Validate caller-provided `MutableFrameView` against `StreamParameters`.

Methods:

- `Status validate_output(const StreamParameters&, MutableFrameView)`
- `Status validate_input(const StreamParameters&, FrameView)` for encoder later.

Rules:

- Check null plane arrays.
- Check plane count.
- Check sample format against bit depth.
- Check dimensions and chroma subsampling.
- Check strides and overflow.

## Diagnostics

### `ffv1::codec::Diagnostics`

Location: `src/codec/diagnostics.hpp`

Purpose:

- Build structured `Status` values with optional frame, slice, and byte
  location data.

Phase 2 can start with free helper functions:

- `Status syntax_error(std::string message, std::uint64_t byte_offset)`
- `Status slice_error(std::uint32_t slice_index, Status cause)`
- `Status unsupported(std::string feature_name)`

The public `Status` type should remain sufficient for applications. Internal
diagnostics should only help populate it consistently.

## Suggested Phase 2 File Layout

```text
src/
  codec/
    decoder_impl.hpp
    decoder.cpp
    frame_decode_context.hpp
    frame_validator.hpp
    scalar_dispatch.hpp
    slice_decoder.hpp
    slice_decoder.cpp
    slice_output_window.hpp
    slice_state.hpp

  entropy/
    context_state.hpp
    range_coder.hpp
    range_coder.cpp
    symbol_reader.hpp

  ffv1/
    configuration_parser.hpp
    configuration_parser.cpp
    frame_parser.hpp
    frame_parser.cpp
    line_state.hpp
    predictor.hpp
    predictor.cpp
    quant_table.hpp
    slice_descriptor.hpp
    stream_parameters.hpp

tests/
  unit/
    configuration_parser_tests.cpp
    frame_validator_tests.cpp
    predictor_tests.cpp
    range_coder_tests.cpp
    slice_output_window_tests.cpp
```

## Implementation Order

1. Add internal value types: `StreamParameters`, `SliceDescriptor`,
   `QuantTableSet`.
2. Add `FrameValidator` and tests for caller-owned output validation.
3. Add `Predictor` and reconstruction tests.
4. Add `ConfigurationParser` for the first narrow profile.
5. Add `FrameParser` for single-slice version 3 payload discovery.
6. Add `RangeCoder` with focused tests.
7. Add `SliceOutputWindow`, `SliceState`, and `SliceDecoder`.
8. Replace current decoder stub with `DecoderImpl` pipeline.
9. Add a tiny project-owned conformance fixture.

Each step must build and pass tests before moving to the next. Avoid broad
rewrites; the scalar pipeline should become increasingly real through small
commits.

## Test Requirements

Every class above needs at least one direct unit test before integration tests
depend on it, except simple passive structs.

Required early tests:

- `FrameValidator` rejects null, too-few, wrong-format, and undersized planes.
- `Predictor::median_predict()` matches known median predictor cases.
- `Predictor::reconstruct()` wraps sample differences at 8-bit and 16-bit.
- `ConfigurationParser` rejects empty and malformed configuration records.
- `FrameParser` rejects empty payloads and malformed slice tables.
- `SliceOutputWindow` maps luma and chroma rectangles correctly.
- `RangeCoder` reports payload underflow without out-of-bounds reads.

Integration tests:

- `create_decoder()` returns a valid decoder.
- `configure()` succeeds for a minimal project-owned configuration record once
  the parser supports it.
- `decode_frame()` returns `NotImplemented` only until the first real profile is
  connected, then returns decoded samples or a precise error.

## Clean Implementation Notes

- Keep syntax names close to RFC 9043 names.
- Do not copy class names, table layouts, or control-flow structure from
  incompatible implementations.
- Prefer small scalar functions with direct tests.
- When RFC behavior is ambiguous, add a comment in this document or
  `docs/DESIGN.md` before encoding the behavior in code.
- Future SIMD paths must prove equivalence to this scalar baseline.
