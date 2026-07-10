# mffv1 Clean Implementation Design

This document describes the first-stage design of mffv1, a specification-driven
C++ implementation of the FFV1 codec. The name `mffv1` identifies this project;
`FFV1` identifies the codec and format standardized by RFC 9043. The document
is intended to be readable by humans and directly useful to LLM-based coding
agents during implementation.

The implementation target is a complete FFV1 codec, with an initial focus on
correct, testable decoding and a matching encoder architecture. The design keeps
SIMD optimization and slice-level multithreading as first-class concerns without
letting either leak into the normative bitstream logic.

Status note: this is the original stage-1 architecture document. It preserves
early design intent, but some API sketches below predate the current public
headers. For implemented public API contracts, prefer `include/mffv1/*.hpp`,
`../docs/DECODER_REFERENCE.md`, `../docs/ENCODER_REFERENCE.md`, and the unit
tests.

Authoritative format reference:

- RFC 9043, "FFV1 Video Coding Format Versions 0, 1, and 3":
  https://www.rfc-editor.org/rfc/rfc9043.html

## Goals

- Implement FFV1 versions 0, 1, and 3 as defined by RFC 9043.
- Keep the implementation source-provenance friendly: use the public
  specification and generated test vectors, not source-derived behavior from
  incompatible code.
- Provide both decoder and encoder APIs in C++.
- Make the scalar implementation the correctness oracle.
- Add SIMD paths behind explicit, testable strategy interfaces.
- Support slice-level parallelism for version 3 and any other safely independent
  units defined by the bitstream.
- Keep container integration separate from codec logic. Matroska, AVI, or MOV
  adapters should feed codec frames and configuration records into this library,
  but should not live inside the core codec.

## Non-Goals For Stage 1

- No container muxer/demuxer implementation.
- No FFV1 version 4 support until the version 4 specification is finalized and
  explicitly added as a project goal.
- No handwritten platform-specific assembly in the first implementation pass.
- No lossy conversion, color management, or display pipeline beyond exposing
  decoded sample planes exactly as represented by the bitstream.

## Clean Implementation Rules

The codec must be developed from public specifications, independently written
tests, and black-box interoperability observations.

- Normative source: RFC 9043.
- Acceptable references: public mathematical descriptions, RFC errata, generated
  bitstreams, and project-owned test cases.
- Avoid copying code structure, identifiers, constants tables, comments, or
  implementation-specific tricks from incompatible libraries.
- If external conformance samples are used, record provenance in
  `../testvectors/REGISTRY.md` before committing them.
- Keep design decisions traceable to specification sections, measured behavior,
  or project-owned reasoning.
- Write scalar code first. SIMD and threading must be behavior-preserving
  substitutions that can be disabled at runtime or compile time.

## Architecture Overview

```text
Application / Container Adapter
        |
        v
Codec Public API
        |
        +-- Configuration parser / writer
        +-- Frame parser / writer
        +-- Slice scheduler
        +-- Slice decoder / encoder
        |       +-- Entropy coder
        |       +-- Predictor
        |       +-- Quantization context
        |       +-- Pixel format mapper
        |
        +-- Frame buffers
        +-- CRC and validation
        +-- Diagnostics
```

The core library should be organized around immutable stream parameters, per
frame work items, and per slice mutable coding state. This layout makes it clear
which state can be shared, which state must be isolated, and which functions are
safe for parallel execution.

## Proposed Source Layout

```text
CMakeLists.txt
cmake/
  Mffv1Options.cmake      Build options, compiler features, warnings.

include/mffv1/
  codec.hpp              Public decoder/encoder entry points.
  config.hpp             Stream parameters and configuration record types.
  frame.hpp              Frame, plane, stride, and pixel format abstractions.
  options.hpp            Runtime options for threading, validation, and SIMD.
  result.hpp             Error and diagnostic types.

src/
  bitstream/             Bit reader/writer and symbol IO.
  codec/                 High-level decoder and encoder orchestration.
  entropy/               Range coder and Golomb-Rice coder.
  mffv1/                  Format-specific syntax, prediction, contexts.
  simd/                  Optional SIMD dispatch and kernels.
  threading/             Task scheduler abstraction.
  util/                  CRC, checked arithmetic, endian helpers.

tests/
  unit/                  Deterministic small tests.
  conformance/           Known-good bitstream tests.
  fuzz/                  Fuzz harnesses and corpus seeds.
```

## Public API Shape

The API should avoid exposing internal bitstream state. Applications provide
configuration records and compressed frame payloads, and receive decoded frame
planes. Encoders receive frame planes and produce compressed payloads plus a
configuration record when needed.

Public headers should stay thin. They may define constants, enums, lightweight
POD-like request/response structures, and pure virtual interfaces, but should
avoid inline codec logic, templates that encode implementation behavior, or
compile-time CPU-specific interpretation. The compiled library owns concrete
decoder and encoder classes.

```cpp
namespace mffv1 {

enum class CpuFeature : uint64_t {
    Sse2  = 1ull << 0,
    Ssse3 = 1ull << 1,
    Avx2  = 1ull << 2,
    Neon  = 1ull << 16,
};

struct CpuFeatures {
    uint64_t allowed = 0;       // 0 means scalar-only unless auto_detect is set.
    bool auto_detect = true;
};

struct DecoderOptions {
    int thread_count = 0;       // 0 means auto.
    bool verify_crc = true;
    bool strict = true;
    uint32_t frame_width = 0;    // External container dimension; 0 means unset.
    uint32_t frame_height = 0;   // Must be set together with frame_width.
    CpuFeatures cpu = {};
};

class IDecoder {
public:
    virtual ~IDecoder() = default;

    virtual Result<void> configure(ByteSpan configuration_record) = 0;
    virtual Result<FrameInfo> inspect_frame(ByteSpan frame_payload) const = 0;
    virtual Result<void> decode_frame(ByteSpan frame_payload,
                                      MutableFrameView output) = 0;
};

struct EncoderOptions {
    int thread_count = 0;
    int version = 3;
    EntropyMode entropy_mode = EntropyMode::Range;
    CpuFeatures cpu = {};
};

class IEncoder {
public:
    virtual ~IEncoder() = default;

    virtual Result<ConfigurationRecord> configure(const StreamInfo& stream) = 0;
    virtual Result<EncodedFrame> encode_frame(FrameView input) = 0;
};

std::unique_ptr<IDecoder> create_decoder(const DecoderOptions& options);
std::unique_ptr<IEncoder> create_encoder(const EncoderOptions& options);

} // namespace mffv1
```

The final API can evolve, but the implementation should preserve these
boundaries:

- Byte-level parsing is not exposed.
- Frame ownership is controlled by the caller.
- The library accepts explicit strides.
- All fallible operations return structured errors.
- Diagnostic paths can name syntax elements and byte offsets.
- Concrete codec implementations are hidden behind factory-created
  `std::unique_ptr` instances.
- CPU extension support is requested through options and resolved inside the
  library.

## Core Data Model

### Stream Parameters

`StreamParameters` is immutable after configuration parsing. It contains:

- FFV1 version and micro version.
- Entropy coder type.
- Colorspace type.
- Chroma plane presence.
- Bit depth.
- Chroma subsampling.
- Extra plane presence.
- Slice grid dimensions.
- Quantization table sets.
- Initial range coder states, when present.
- Error-correction and intra flags.

### Frame Buffers

`FrameView` and `MutableFrameView` describe caller-owned planes:

- Plane pointer.
- Width and height in samples.
- Stride in bytes.
- Sample type: unsigned 8-bit, unsigned 16-bit, or future-compatible packed
  representation if a supported format requires it.
- Plane role: Y, Cb, Cr, alpha, R, G, or B.

The codec should normalize internal per-sample math to fixed-width integer types
large enough for the bit depth. Avoid implicit signed overflow. Right shifts
whose signed behavior matters must use helper functions that implement the RFC
semantics exactly.

### Slice State

Each slice owns:

- Entropy coder state.
- Context model state.
- Quantization table indexes.
- Border and line buffers.
- Slice-local diagnostics.
- Output plane windows.

No mutable slice state may be shared across worker threads. Shared configuration
objects must be immutable.

## Bitstream Layer

The bitstream layer provides small, auditable primitives:

- `BitReader` and `BitWriter` for bit-level IO.
- Checked byte alignment.
- Unsigned and signed symbol readers corresponding to RFC syntax types.
- Bounded reads that report underflow instead of invoking undefined behavior.
- Optional source offset tracking for diagnostics.

Keep syntax parsing separate from entropy decoding. The bitstream module knows
how to read bits; the FFV1 syntax layer decides which symbol is expected.

## Entropy Coding

FFV1 uses two entropy coding modes:

- Range coding.
- Golomb-Rice coding.

The design should expose a common symbol interface:

```cpp
class SymbolReader {
public:
    virtual Result<int32_t> read_symbol(ContextId context) = 0;
};

class SymbolWriter {
public:
    virtual Result<void> write_symbol(ContextId context, int32_t value) = 0;
};
```

Virtual dispatch does not have to be used in hot loops. The common interface is
a design boundary; implementation can use templates, tagged unions, or static
dispatch after profiling. Correctness tests should run both entropy paths.

## Prediction And Context Modeling

Prediction is the core scalar correctness path. It should be implemented as a
small pure module where possible:

- Median predictor.
- Border sample handling.
- Quantized context derivation.
- Sample difference reconstruction.
- Range wrapping according to bit depth.

The predictor must operate on line buffers rather than entire frames when
possible. This keeps memory bounded and simplifies slice isolation.

Recommended implementation layers:

```text
decode_line()
  decode_sample_difference()
  derive_context()
  predict_sample()
  reconstruct_sample()
  store_sample()
```

The scalar path must be deterministic and easy to compare against independent
vectors. SIMD kernels may accelerate repeated context preparation, predictor
calculation, color transforms, or sample copy/packing, but they must not change
bitstream-visible behavior.

## Color And Pixel Format Handling

The codec core should represent planes and samples, not application-level images.
Color metadata and container-level color signaling are outside the first-stage
core, except where required by the FFV1 bitstream syntax.

Implementation responsibilities:

- Support YCbCr-like planar coding with chroma subsampling.
- Support RGB coding as defined by the FFV1 version being decoded.
- Support optional transparency or extra planes where specified.
- Preserve bit depth without scaling.
- Reject unsupported sample layouts with explicit errors.

Avoid automatic conversion between RGB and YCbCr in the core library. Conversion
can be an optional utility module later.

## Slice-Level Multithreading

Version 3 slice structure is the primary concurrency boundary. A frame decode
should be transformed into independent `SliceJob` values after frame-level
syntax has been validated.

```text
Frame payload
  -> parse frame envelope
  -> discover and validate slices
  -> create SliceJob list
  -> run jobs in scheduler
  -> collect diagnostics
  -> verify frame completion
```

Threading rules:

- A slice job writes only to its own rectangular output windows.
- A slice job reads only immutable stream parameters and its own payload bytes.
- Slice CRC validation is slice-local.
- Errors include slice index and byte range.
- The scheduler must support deterministic single-thread mode.

The default scheduler can be a simple fixed-size worker pool. A custom scheduler
hook may be added later for applications that already own a task system.

## SIMD Design

SIMD support should be optional, explicit, and easy to disable.

```text
Scalar implementation
        |
        +-- SSE2 / SSSE3 / AVX2 on x86/x64
        +-- NEON on ARM64
        +-- future extension points
```

Dispatch should happen at coarse boundaries:

- Runtime CPU feature detection and caller-provided `CpuFeatures` select a
  `SimdDispatch` table.
- Build flags can compile out unsupported architectures.
- Tests must be able to force scalar mode and each compiled SIMD mode.
- If `CpuFeatures::auto_detect` is false, the library must not execute
  instructions outside the explicitly allowed feature mask.

Good SIMD candidates:

- Median predictor over vectors of samples where dependencies allow it.
- Pixel/sample copy and endian normalization.
- Plane differencing helpers used by the encoder.
- CRC acceleration where available.
- RGB transform helpers, if profiling proves useful.

Bad SIMD candidates in the first pass:

- Entropy decoding inner loops with complex adaptive state.
- Any code where vectorization obscures exact overflow or wrapping behavior.
- Cross-sample predictor logic if it introduces hard-to-audit dependencies.

Every SIMD kernel needs:

- A scalar reference function.
- Alignment assumptions documented in the function contract.
- Tests covering unaligned input, short tails, odd widths, and maximum bit depth.

## Decoder Flow

```text
Decoder::configure()
  -> parse configuration record
  -> validate stream parameters
  -> build immutable quantization tables
  -> initialize reusable frame-level state

Decoder::decode_frame()
  -> validate output frame shape
  -> parse frame syntax
  -> locate slices
  -> schedule slice decode jobs
  -> decode each slice into output planes
  -> verify CRCs when enabled
  -> return diagnostics or success
```

Strict mode rejects malformed or unsupported bitstreams early. The public
`DecoderOptions::strict` field is reserved for a future relaxed compatibility
mode, but the current factory rejects `strict == false` with
`UnsupportedFeature`. Any future relaxed mode must never read out of bounds or
write outside caller-provided frames.

## Encoder Flow

The encoder should share the same data model, predictor, context, entropy, CRC,
and threading infrastructure as the decoder.

```text
Encoder::configure()
  -> validate requested stream settings
  -> choose quantization table sets
  -> write configuration record

Encoder::encode_frame()
  -> split frame into slices
  -> schedule slice encode jobs
  -> predict and code each slice
  -> write slice footers and CRCs
  -> assemble frame payload
```

Initial encoder policy should favor conformance and simplicity:

- Version 3 by default.
- Range coder by default.
- Intra-only by default.
- Conservative slice grid selection.
- Deterministic output for identical input and options.

Compression tuning should be added only after bit-exact round-trip tests and
interoperability tests are stable.

## Error Handling

Use a structured `Error` type with:

- Error category: syntax, unsupported feature, CRC mismatch, invalid argument,
  resource exhaustion, internal invariant failure.
- Human-readable message.
- Optional byte offset.
- Optional frame index, if known by caller or adapter.
- Optional slice index.
- Optional RFC syntax element name.

Never signal malformed input with assertions. Assertions are reserved for
internal invariants that cannot be triggered by untrusted input.

## Memory And Safety

- All dimensions and buffer sizes use checked arithmetic.
- Reject dimensions that cannot be represented safely in `size_t`.
- Validate plane strides before decoding.
- Keep per-slice allocations bounded and reusable.
- Prefer `std::span`, small value types, and RAII-owned buffers.
- Avoid global mutable state except for immutable CPU feature detection caches.
- Treat all compressed input as untrusted.

## Testing Strategy

The test framework is GoogleTest. Tests should be integrated through CMake and
CTest so they can run consistently from local development, CI, and future
automation agents.

GoogleTest usage rules:

- Keep tests in separate test targets instead of exposing codec internals through
  public headers.
- Prefer parameterized tests for version, entropy mode, pixel format, bit depth,
  and SIMD/scalar dispatch coverage.
- Keep generated fixtures and external samples provenance-tracked.
- Make every SIMD test compare against the scalar reference path.

### Unit Tests

- Bit reader/writer boundary behavior.
- RFC-specific arithmetic helpers.
- Median predictor.
- Quantization context derivation.
- Range coder state transitions.
- Golomb-Rice coding.
- CRC routines.
- Slice rectangle mapping.

### Conformance Tests

- Decode curated FFV1 v0, v1, and v3 samples.
- Round-trip encode/decode generated frames.
- Cross-check decoded samples against independently stored hashes.
- Validate CRC mismatch reporting.
- Test uncommon bit depths and chroma subsampling combinations.

### Differential Tests

Differential tests may compare input/output behavior against external tools as
black boxes, provided no source code is copied or structurally followed. Record
tool versions and commands in test metadata.

### Fuzzing

Fuzz these entry points:

- Configuration record parser.
- Frame parser.
- Slice decoder.
- Full decoder with small frame size limits.

Fuzz builds should enable sanitizers where available.

## Implementation Phases

### Phase 1: Design And Skeleton

- Add CMake build system targeting C++20.
- Create public headers and module layout.
- Implement result/error types.
- Implement bitstream readers and RFC arithmetic helpers.
- Add unit test framework.

### Phase 2: Minimal Decoder

- Parse configuration records.
- Decode one supported version 3 profile in scalar single-thread mode.
- Produce exact frame planes.
- Add conformance tests for that profile.

### Phase 3: Complete Decoder Coverage

- Add versions 0 and 1.
- Add remaining entropy mode coverage.
- Add chroma subsampling and RGB paths.
- Add CRC validation.
- Add strict diagnostics.

### Phase 4: Slice Threading

- Introduce slice job discovery.
- Add deterministic scheduler.
- Add multi-threaded decode tests.
- Verify output equality between single-thread and multi-thread modes.

### Phase 5: Encoder

- Implement scalar version 3 encoder.
- Treat FFV1 versions 0 and 1 as decoder compatibility targets only. Do not
  implement legacy encoder output unless a future downstream integration proves
  that new legacy streams are required.
- Share predictor, contexts, entropy, and CRC modules.
- Add round-trip tests.
- Add interoperability black-box tests.

### Phase 6: SIMD Optimization

- Add CPU feature detection.
- Add scalar/SIMD dispatch tables.
- Implement small kernels with scalar references.
- Benchmark before and after each kernel.

### Phase 7: Hardening

- Expand fuzzing.
- Add malformed stream tests.
- Document supported profiles.
- Stabilize ABI/API policy if the library will be distributed.

## LLM Implementation Notes

When using this document as implementation context:

- Start with scalar correctness. Do not begin with SIMD.
- Keep syntax names close to RFC 9043 names.
- Add tests beside each module as it is introduced.
- Prefer small files with single responsibilities.
- Do not infer behavior from incompatible source code.
- If the RFC is ambiguous, document the ambiguity and add a project decision
  before implementing compatibility behavior.
- Preserve deterministic single-thread behavior as the baseline.

## Project Decisions

- Build system: CMake.
- Language standard: C++20.
- Test framework: GoogleTest, integrated through CMake and CTest.
- Public API style: thin public headers with constants, enums, small data
  structures, and pure virtual interfaces. Concrete implementations are compiled
  into the library and created through factory functions returning
  `std::unique_ptr`.
- CPU feature selection: callers may provide a CPU feature support mask through
  options. The library resolves that request to scalar or SIMD implementation
  classes internally.
- First conformance milestone: no externally imposed profile. Start from the
  easiest profile that establishes correct architecture, then expand until all
  RFC 9043 versions and supported coding modes are complete.
