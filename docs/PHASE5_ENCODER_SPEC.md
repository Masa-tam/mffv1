# mffv1 Phase 5 Class Specification: Scalar Encoder

## Purpose

This document specifies the first clean-room FFV1 encoder architecture for
mffv1. It is written primarily as implementation guidance for LLM-assisted
development, while keeping ownership, invariants, and milestones explicit for
human review.

The encoder MUST be implemented from published specifications and
project-owned tests. Code from existing FFV1 implementations MUST NOT be used
as an implementation reference.

## Initial Conformance Target

The first end-to-end encoder milestone is deliberately narrow:

- FFV1 version 3, stable micro-version 4.
- Range coding with the default state transition table.
- Y-only, 8-bit unsigned input.
- One quantization table set containing the zero tables.
- One slice covering the complete frame.
- Keyframes only.
- No error-status field and no custom initial states.

The output MUST be accepted by the mffv1 decoder. Internal round-trip tests are
necessary but not sufficient for final conformance; later milestones add
independently produced vectors and black-box interoperability tests.

The first post-milestone extensions add planar 8-16 bit YCbCr 4:4:4, 4:2:2,
and 4:2:0, plus an optional full-resolution extra plane. Every coded plane
uses separate prediction line state and separate range context banks and is
coded in planar order. Subsampled chroma dimensions use ceiling division,
including for odd frame dimensions. Input values above the configured sample
depth are rejected rather than truncated. Planar 16-bit range coding uses the
normative signed-16 predictor interpretation shared with the decoder.

## Public API Contract

The existing public API remains the boundary:

```cpp
class IEncoder {
public:
    virtual ~IEncoder() = default;
    virtual Status configure(const StreamInfo& stream,
                             ConfigurationRecord& out_record) = 0;
    virtual Status encode_frame(FrameView input,
                                EncodedFrame& out_frame) = 0;
};
```

Public headers MUST remain declarative. Entropy coding, prediction, slice
state, SIMD dispatch, and worker types stay private to the library.

`configure()` is transactional:

- On success, the encoder stores normalized stream parameters and replaces
  `out_record.bytes` with a complete configuration record.
- On failure, the prior encoder configuration and `out_record` are unchanged.
- `StreamInfo::version` and `EncoderOptions::version` MUST match. A mismatch is
  `InvalidArgument`; silently choosing one would make the bitstream ambiguous.
- The initial milestone accepts only the conformance target above and reports
  other valid profiles as `UnsupportedFeature`.

`encode_frame()` is transactional:

- It requires a successful `configure()`.
- It validates every input plane before starting worker jobs.
- On success, it replaces `out_frame.bytes` with exactly one complete frame.
- On failure, `out_frame` and persistent reference state are unchanged.

## Ownership And Data Flow

```text
IEncoder / Encoder
  owns normalized StreamParameters
  owns EncoderFrameState
  owns SliceEncodeExecutor
  uses ConfigurationRecordWriter

encode_frame(FrameView)
  -> FrameValidator::validate_input
  -> SlicePlanner
  -> SliceEncodeExecutor
       -> SliceEncoder per independent slice
            -> Predictor and ContextModel
            -> RangeEncoder or GolombRiceWriter
  -> FrameAssembler
       -> slice headers and footers
       -> CRC parity
  -> commit EncodedFrame and reference state
```

The encoder MUST NOT write directly into caller-visible output while an
operation can still fail. Each slice produces an owned byte vector; the frame
assembler commits the final vector only after every slice succeeds.

The initial one-slice implementation invokes `SliceEncoder` directly after
validation. `SlicePlanner`, `SliceEncodeExecutor`, and `FrameAssembler` become
separate orchestration objects when multiple slices are introduced.

## Shared Scalar Semantics

The following decoder-side syntax helpers define codec mathematics and should
be shared by both directions:

- `syntax::Predictor` for median prediction and modular reconstruction rules.
- `syntax::ContextModel` for gradient quantization and context selection.
- `syntax::QuantTableSet`, `StateTransitionTable`, and normalized
  `StreamParameters`.
- RFC integer helpers and CRC-32 implementation.

Encoding needs an explicit inverse reconstruction helper:

```cpp
std::int32_t difference(std::int32_t sample,
                        std::int32_t prediction,
                        std::uint8_t bits) noexcept;
```

It MUST produce the canonical signed modular difference accepted by the
decoder. Tests MUST verify, for every supported bit width and boundary value,
that `reconstruct(prediction, difference(sample, prediction)) == sample`.

Reader state and writer state MUST NOT share a class. Similar names do not
imply interchangeable invariants.

## Bit And Symbol Writers

### `bitstream::BitWriter`

Location: `src/bitstream/bit_writer.hpp` and `.cpp`.

Responsibilities:

- Append bits most-significant-bit first.
- Append bounded unsigned fields.
- Add zero alignment padding when explicitly requested.
- Expose an owned byte vector only after finalization.
- Detect size arithmetic overflow before allocation.

The writer keeps a partial byte internally. Finalization MUST either reject an
unaligned stream or apply padding selected by the caller; implicit padding is
not allowed.

### `entropy::SymbolWriter`

Location: `src/entropy/symbol_writer.hpp`.

This private abstract interface mirrors syntax operations, not reader
implementation details:

```cpp
class SymbolWriter {
public:
    virtual ~SymbolWriter() = default;
    virtual Status write_bool(bool value) = 0;
    virtual Status write_unsigned(std::uint64_t value) = 0;
    virtual Status write_signed(std::int64_t value) = 0;
};
```

Configuration and slice-header writers depend on this interface so scripted
unit writers can verify syntax order without invoking arithmetic coding.

## Range Encoder

### `entropy::RangeEncoder`

Location: `src/entropy/range_encoder.hpp` and `.cpp`.

Responsibilities:

- Encode binary decisions using the RFC range-coder state machine.
- Encode unsigned and signed symbol trees used by FFV1 syntax.
- Own scalar context banks and the state transition table.
- Reconfigure context banks after a frame or slice header without resetting
  arithmetic interval state.
- Export context snapshots for non-keyframe continuation.
- Finalize to the shortest unambiguous byte sequence accepted by
  `RangeCoder`.

Required API shape:

```cpp
class RangeEncoder final : public SymbolWriter {
public:
    Status reset(std::span<const std::size_t> context_counts,
                 std::span<const std::span<const ScalarContextStates>>
                     initial_state_banks,
                 const StateTransitionTable& transitions);
    Status reconfigure_contexts(/* same normalized inputs */);
    Status write_bool(bool value) override;
    Status write_unsigned(std::uint64_t value) override;
    Status write_signed(std::int64_t value) override;
    Status write_signed(std::size_t bank, ContextId context,
                        std::int64_t value);
    Status finalize(std::vector<std::byte>& out);
};
```

Exact parameter aliases should follow `RangeCoder` where that improves
symmetry, but reader-only concepts such as byte underflow must not leak into
the writer.

Range-coder tests MUST encode decisions and symbols, then decode them with the
existing `RangeCoder`. Include empty/zero symbols, signed extrema used by the
codec, context-bank separation, custom transitions, carry propagation, long
runs, and finalization boundaries. Test expected bytes only when derived
directly from the RFC or from project-owned arithmetic derivations.

## Configuration Record Writer

### `codec::ConfigurationRecordWriter`

Location: `src/codec/configuration_record_writer.hpp` and `.cpp`.

Responsibilities:

- Write normalized `Parameters()` syntax through `SymbolWriter`.
- Reject stream states that cannot be represented by the requested version.
- For version 3+, append big-endian CRC parity so the full record has a zero
  CRC remainder.
- Leave dimension transport to the container/API; dimensions are not written
  into the FFV1 version 3 configuration record.

Syntax ordering tests use a scripted writer. Integration tests parse generated
records with `ConfigurationRecordParser` and compare every normalized field.

The first production increment should stop here: a valid generated
configuration record is a useful, independently testable encoder foundation
before frame coding begins.

## Input Validation

### `codec::FrameValidator::validate_input`

Location: `src/codec/frame_validator.hpp` and `.cpp`.

The encoder uses the input-specific path of the shared frame validator. It
validates the complete `FrameView` before any output or persistent state is
modified:

- Required plane count and order match normalized stream parameters.
- Plane pointers are non-null.
- Sample format matches bit depth.
- Width and height match full-resolution or subsampled plane geometry.
- Absolute stride is large enough for one row.
- Address calculations for the last row are representable.

Negative stride support must be an explicit decision backed by tests. Until
then, reject it as `UnsupportedFeature` rather than interpreting it
accidentally.

## Slice Planning And State

### `codec::SliceEncodePlan`

An immutable plan contains slice index, raster rectangle, per-plane input
windows, quant-table indexes, and header fields. Plans are created before
workers start and are ordered by slice index.

### `codec::SliceEncoderState`

This state is separate from decoder `SliceState` and owns:

- Previous and current reconstructed lines per coded plane.
- Range context snapshots or Golomb-Rice context/run state.
- Scratch buffers reused by one slice across frames.

Prediction MUST use reconstructed samples, not unread caller input neighbors.
For lossless coding they are equal after reconstruction, but retaining this
invariant prevents divergence when modular transforms and context folding are
involved.

Keyframes start from initial entropy contexts. Non-keyframes clone the matching
previous slice state by raster geometry. State is committed only after all
slices and frame assembly succeed.

## Slice Encoder

### `codec::SliceEncoder`

Location: `src/codec/slice_encoder.hpp` and `.cpp`.

For each sample in normative plane and scan order it:

1. Loads neighboring reconstructed samples.
2. Computes the median prediction.
3. Derives the quantized context from gradients.
4. Computes the canonical modular difference.
5. Applies context-sign folding.
6. Writes the entropy-coded difference.
7. Stores the reconstructed sample in line state.

RGB uses the normative reversible color transform in the encoding direction
before prediction. The initial Y-only milestone must not add placeholder RGB
behavior.

The slice encoder writes header, content, alignment, and footer into a private
buffer. Version 3 range coding continues one arithmetic stream across the
slice header and content while switching context banks at the specified point.

## Frame Assembly And Threading

### `codec::SliceEncodeExecutor`

The executor follows the decoder scheduler's deterministic contract:

- `thread_count == 0` resolves to available hardware concurrency with a
  minimum of one.
- Worker count is capped by slice count.
- Each worker owns its output bytes and working state.
- Error selection is deterministic by lowest input slice index.
- No worker writes shared frame bytes.

### `codec::FrameAssembler`

The assembler concatenates completed slices in normative order and patches or
appends footer sizes and CRC parity with checked arithmetic. Parallel and
serial encoding of the same frame and options MUST be byte-identical.

## SIMD Boundary

Entropy coding remains scalar because its state is serial. Future SIMD work is
limited to kernels with explicit scalar references:

- Reversible color transform.
- Gradient and median-prediction preparation where dependencies allow it.
- Input format conversion and plane loads.

Dispatch is selected once when the encoder instance is created from
`EncoderOptions::cpu`. Public headers expose only feature flags and the
`std::unique_ptr<IEncoder>` factory result. SIMD kernels MUST produce identical
bytes to scalar encoding, not merely equivalent decoded pixels.

## Diagnostics And Resource Limits

- Invalid caller data uses `InvalidArgument`.
- Calls in the wrong lifecycle state use `InvalidState`.
- Valid but unimplemented profiles use `UnsupportedFeature`.
- Internal size overflow or configured resource limits use
  `ResourceExhausted`.
- Every slice failure carries its slice index; syntax-field writers add the
  logical field name where useful.

All allocation sizes must be checked before multiplication or conversion to
container sizes. Encoder output growth must have a documented upper bound per
configured frame, even though normal FFV1 output is much smaller.

## Implementation Milestones

Each milestone builds and passes the full test suite before commit:

1. Add `BitWriter` and exhaustive bit-order/alignment tests.
2. Add `SymbolWriter` and `RangeEncoder` binary-decision round trips.
3. Add unsigned/signed range-symbol writing and context-bank tests.
4. Add `ConfigurationRecordWriter` for the initial version 3 profile and
   round-trip it through the existing parser.
5. Make `IEncoder::configure()` transactional and return that record.
6. Complete `FrameValidator::validate_input()` for Y-only 8-bit input and
   connect it to `IEncoder::encode_frame()`.
7. Add single-slice scalar difference generation and range coding.
8. Add version 3 slice header/footer assembly and CRC tests.
9. Round-trip generated frames through the public decoder API.
10. Add chroma, extra plane, higher bit depth, RGB, Golomb-Rice, multiple
    slices, non-keyframes, threading, and SIMD in separately tested increments.

## Clean-Room Verification Rules

- Record the RFC section used when a non-obvious arithmetic rule is added.
- Do not copy code, tables, comments, or test vectors from implementation
  source trees.
- Prefer algebraic properties and project-generated exhaustive tests.
- Label externally supplied conformance vectors with provenance and license.
- Treat encoder-decoder round trips as regression tests, not proof that both
  sides independently match the specification.
