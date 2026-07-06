# mffv1 Decoder Reference

## Status

The mffv1 decoder is currently suitable for technical-preview and internal
integration use. Its public API is small and stable enough to integrate, but a
general stable release still requires external conformance testing, fuzzing,
sanitizer coverage, and packaging.

This reference describes the implemented behavior of the current decoder. It
does not describe the encoder; see the
[Encoder Reference](ENCODER_REFERENCE.md).

## Header And Namespace

Include the umbrella header:

```cpp
#include <mffv1/codec.hpp>
```

All public names are in namespace `mffv1`.

The library requires C++20. Decoder instances are returned as
`std::unique_ptr<IDecoder>` and hide all parsing, entropy, threading, and SIMD
implementation classes.

Shared frame and plane layout rules are documented in the
[Frame Buffer Reference](FRAME_BUFFER_REFERENCE.md).

## Lifecycle

The normal lifecycle is:

1. Fill `DecoderOptions`.
2. Call `create_decoder()`.
3. Call `configure()` with FFV1 Parameters or a Configuration Record.
4. Optionally call `inspect_frame()`.
5. Prepare output planes.
6. Call `decode_frame()` once per frame, in presentation order.

For FFV1 version 0/1 streams that carry keyframe-embedded `Parameters()` and
have no external Codec Private data, call `bootstrap_legacy_frame()` on the
keyframe before inspection or decoding. This explicit bootstrap step may
configure an unconfigured decoder or report that embedded parameters differ
from the current decoder configuration.

One decoder instance represents one configured FFV1 stream. Reconfiguration
replaces the stream parameters and clears all reference state used by
non-keyframes.

Decoder instances are not thread-safe. Do not call methods concurrently on the
same instance. Separate instances may be used concurrently.

## Creating A Decoder

```cpp
mffv1::DecoderOptions options;
options.frame_width = width;
options.frame_height = height;
options.thread_count = 0;

auto result = mffv1::create_decoder(options);
if (!result.status.ok()) {
    // Handle result.status.
}
std::unique_ptr<mffv1::IDecoder> decoder = std::move(result.decoder);
```

Declaration:

```cpp
DecoderFactoryResult create_decoder(const DecoderOptions& options);
```

On success, `status.ok()` is true and `decoder` is non-null. On failure,
`decoder` is null.

### `DecoderOptions`

| Field | Meaning |
| --- | --- |
| `thread_count` | `0` selects hardware concurrency, `1` forces serial slice decoding, and positive values set the maximum slice workers. Negative values are rejected. |
| `verify_crc` | Verifies version 3+ slice CRC parity when the stream enables error status. Configuration Record CRC is always verified. |
| `strict` | Must be `true`. `false` is reserved for a future relaxed parsing mode and is currently rejected by `create_decoder()`. |
| `frame_width` | Coded frame width supplied by the container. |
| `frame_height` | Coded frame height supplied by the container. |
| `cpu` | Controls runtime CPU feature selection. Dispatch is resolved once when the decoder is configured. |

`create_decoder()` rejects `strict == false` with `UnsupportedFeature` because
the current decoder has only one parsing contract: strict validation with no
relaxed compatibility mode.

`frame_width` and `frame_height` must either both be zero or both be non-zero.
They should normally be supplied because FFV1 version 3 Configuration Records
do not contain coded dimensions. Frame inspection and decoding fail with
`InvalidState` while dimensions remain unknown.

### CPU Feature Selection

`CpuFeatures` contains an allowed feature mask and an automatic-detection flag:

```cpp
mffv1::CpuFeatures cpu;
cpu.auto_detect = false;
cpu.allowed = 0; // Force scalar operation.
```

The current build recognizes `Sse2`, `Ssse3`, `Avx2`, and `Neon`.

- With `auto_detect == true` and `allowed == 0`, all detected and compiled
  features are available to dispatch.
- With `auto_detect == true` and a non-zero mask, available features are the
  intersection of the mask, detected hardware, OS support, and compiled
  target support.
- With `auto_detect == false`, hardware detection is not used for selection.
  The mask is limited only to features supported by the build target. The
  caller is responsible for supplying a truthful mask for the executing CPU.
- With `auto_detect == false` and `allowed == 0`, scalar operation is forced.

The current decoder dispatch prefers AVX2 and then SSE2 for the RGB inverse
JPEG 2000 reversible color transform. Other decoder operations remain scalar.
The selected table is immutable and shared by slice workers.

## Configuring A Stream

```cpp
mffv1::Status status = decoder->configure(configuration_record);
```

Declaration:

```cpp
virtual Status configure(ByteSpan configuration_record) = 0;
```

The input storage only needs to remain valid for the duration of the call.
Parsed stream parameters are owned by the decoder afterward.

For FFV1 version 3, pass the complete Configuration Record, including its
four-byte CRC parity. A non-zero CRC remainder is rejected even when
`DecoderOptions::verify_crc` is false.

For versions 0 and 1, the current API expects the range-coded `Parameters()`
payload to be supplied to `configure()` by the container integration when that
payload is already available. If a legacy container stores `Parameters()` only
inside keyframes, use `bootstrap_legacy_frame()` instead.

A successful call clears prior frame-reference state. A failed call leaves the
previous configuration unchanged.

## Legacy Keyframe Bootstrap

```cpp
mffv1::LegacyBootstrapResult bootstrap =
    decoder->bootstrap_legacy_frame(frame_payload);
```

Declaration:

```cpp
virtual LegacyBootstrapResult bootstrap_legacy_frame(ByteSpan frame_payload) = 0;
```

`bootstrap_legacy_frame()` parses a complete FFV1 version 0/1 frame far enough
to read keyframe-embedded `Parameters()`. It does not decode samples.
For range-coded single-slice legacy keyframes, the same frame payload can be
passed to `decode_frame()` after a successful bootstrap; the decoder preserves
the embedded-parameter boundary and continues slice content from the correct
range-coder state.

On success, inspect `bootstrap.info.state`:

| State | Meaning |
| --- | --- |
| `NoEmbeddedParameters` | The frame did not carry embedded parameters, usually because it is not a keyframe. The decoder state is unchanged. |
| `Configured` | The decoder was unconfigured and is now configured from the keyframe parameters. |
| `MatchesCurrentConfiguration` | Embedded parameters were present and match the current decoder configuration. |
| `DiffersFromCurrentConfiguration` | Embedded parameters were present but differ from the current decoder configuration. The decoder state is unchanged. |

Parameter changes are reported as a successful result with
`DiffersFromCurrentConfiguration`, not as a failed `Status`. A caller can then
create or reconfigure a decoder deliberately from that keyframe and discard
old reference state.

`decode_frame()` does not call `bootstrap_legacy_frame()` implicitly. This keeps
configuration changes visible to container adapters and prevents hidden
reference-state resets.

## Inspecting A Frame

```cpp
mffv1::FrameInfo info;
mffv1::Status status = decoder->inspect_frame(frame_payload, info);
```

Declaration:

```cpp
virtual Status inspect_frame(ByteSpan frame_payload,
                             FrameInfo& out_info) const = 0;
```

`inspect_frame()` parses frame and slice metadata without decoding samples and
without changing non-keyframe reference state.

On success, `FrameInfo` contains:

| Field | Meaning |
| --- | --- |
| `width` | Configured coded width. |
| `height` | Configured coded height. |
| `version` | Parsed FFV1 version. |
| `micro_version` | Parsed FFV1 micro-version. Versions without a micro-version report zero. |
| `entropy_mode` | Parsed entropy coder, `EntropyMode::Range` or `EntropyMode::GolombRice`. |
| `bits_per_raw_sample` | Coded sample depth. |
| `plane_count` | Number of output planes required by the stream. |
| `planes` | Required plane table. Entries `0 .. plane_count - 1` contain role, sample format, required width, required height, and minimum stride in bytes. |
| `color_space` | `ColorSpace::YCbCr` or `ColorSpace::Rgb`. |
| `has_chroma_planes` | True when the stream carries Cb/Cr planes for YCbCr or G/B planes for RGB. |
| `has_extra_plane` | True when the stream carries an additional alpha-like plane. |
| `log2_h_chroma_subsample` | Horizontal YCbCr chroma subsampling exponent. |
| `log2_v_chroma_subsample` | Vertical YCbCr chroma subsampling exponent. |
| `error_status_enabled` | True when version 3+ slices carry error-status and CRC footer fields. |
| `intra_only` | True when the stream declares all frames as independently coded keyframes. |
| `keyframe` | True when the frame declares itself as a keyframe. |
| `slice_count` | Number of slices parsed from the frame payload. |

`planes` is a fixed-size `std::array<PlaneInfo, kMaxFramePlanes>` with room for
the maximum FFV1 plane count used by this API. Entries at indexes greater than
or equal to `plane_count` are not part of the stream layout.
`PlaneInfo::stride_bytes` is the minimum contiguous stride required for
allocation; callers may provide larger positive strides to `decode_frame()`.

The current `FrameInfo` does not expose per-slice error-status values.

## Decoding A Frame

```cpp
mffv1::Status status = decoder->decode_frame(frame_payload, output);
```

Declaration:

```cpp
virtual Status decode_frame(ByteSpan frame_payload,
                            MutableFrameView output) = 0;
```

Frames must be submitted in stream order. A non-keyframe requires successfully
decoded reference state from the preceding frame. A non-keyframe submitted to
a fresh or reconfigured decoder fails with `InvalidState`.

The decoder commits entropy reference state only after all slices decode
successfully. A failed frame does not replace the previous reference state.
Caller output memory is not transactional and may be partially modified when
decoding fails; discard that output frame after any error.

The frame payload and output plane storage must remain valid until the call
returns. The decoder does not retain either pointer.

## Output Planes

`MutableFrameView` is a non-owning array of `MutablePlaneView` values:

```cpp
struct MutableFrameView {
    MutablePlaneView* planes;
    std::size_t plane_count;
};

struct MutablePlaneView {
    void* data;
    PlaneInfo info;
};
```

Every required plane must have a non-null data pointer. Additional planes and
dimensions larger than the coded requirement are allowed and ignored outside
the coded rectangle.

### Plane Order

YCbCr streams use:

1. `PlaneRole::Y`
2. `PlaneRole::Cb`, when chroma is present
3. `PlaneRole::Cr`, when chroma is present
4. `PlaneRole::Alpha`, when an extra plane is present

RGB streams use:

1. `PlaneRole::R`
2. `PlaneRole::G`
3. `PlaneRole::B`
4. `PlaneRole::Alpha`, when an extra plane is present

Plane roles are validated. Swapped or mismatched roles return
`InvalidArgument`.

### Sample Format

| Bit depth | Required format |
| --- | --- |
| 1 through 8 | `SampleFormat::UInt8` |
| 9 through 16 | `SampleFormat::UInt16` |

Sixteen-bit plane storage uses native C++ `std::uint16_t` representation.

### Plane Dimensions

Luma, RGB, and alpha planes use the full configured dimensions.

YCbCr chroma dimensions are rounded up:

```text
chroma_width  = ceil(width  / 2^log2_h_chroma_subsample)
chroma_height = ceil(height / 2^log2_v_chroma_subsample)
```

`PlaneInfo::width` and `height` must be at least these values.

### Stride

`stride_bytes` is the byte distance between successive rows. It must be
positive and at least:

```text
required_plane_width * bytes_per_sample
```

Negative strides are not supported. Padding bytes beyond the coded row width
are not modified.

## Minimal Y-Only Example

```cpp
std::vector<std::uint8_t> pixels(
    static_cast<std::size_t>(width) * height);

mffv1::MutablePlaneView plane;
plane.data = pixels.data();
plane.info.role = mffv1::PlaneRole::Y;
plane.info.sample_format = mffv1::SampleFormat::UInt8;
plane.info.width = width;
plane.info.height = height;
plane.info.stride_bytes = width;

mffv1::MutableFrameView output{&plane, 1};
const mffv1::Status status = decoder->decode_frame(frame_payload, output);
```

## Error Handling

All operations return `Status`:

```cpp
struct Status {
    ErrorCode code;
    std::string message;
    ErrorLocation location;

    bool ok() const noexcept;
};
```

`Status::message` is intended for diagnostics, not stable programmatic
matching. Branch on `ErrorCode` and location flags. Builds configured with
`MFFV1_ENABLE_STATUS_MESSAGES=OFF` keep the field but leave library-generated
messages empty.

### Error Codes

| Code | Meaning |
| --- | --- |
| `Ok` | Operation succeeded. |
| `InvalidArgument` | Caller input, dimensions, planes, or options are invalid. |
| `InvalidState` | Decoder lifecycle or reference state is insufficient. |
| `UnsupportedFeature` | The bitstream uses a valid feature outside current support. |
| `SyntaxError` | The bitstream violates syntax or is truncated. |
| `CrcMismatch` | Configuration or slice CRC verification failed. |
| `ResourceExhausted` | A size or allocation-related implementation limit was exceeded. |
| `NotImplemented` | A recognized path has not yet been implemented. |
| `InternalError` | An internal invariant failed. |

### Error Locations

`ErrorLocation` values are valid only when the corresponding flag is true:

```cpp
if (status.location.has_byte_offset) {
    use(status.location.byte_offset);
}
if (status.location.has_slice_index) {
    use(status.location.slice_index);
}
```

Byte offsets are relative to the frame or Configuration Record supplied to the
failing public call. Slice indexes are zero-based in payload order.

## Entropy State Notes

For version 3 range-coded frames, the first slice's `keyframe` symbol and the
Slice Header use separate default-initialized scalar context scopes. After the
Slice Header has been parsed, Slice Content continues the same arithmetic range
coder state but reconfigures scalar context banks from the Slice Header
quantization-table index slots.

Range-coded Slice Content banks are slot-based, not coded-plane-based. For
YCbCr streams with chroma planes, luma uses slot 0 and both Cb and Cr use slot
1. An optional extra plane uses its own slot. This behavior is part of the
current compatibility baseline for generated FFV1 version 3 vectors.

Version 3 Golomb-Rice content uses the same quantization-table index slot
mapping for adaptive VLC context state. For YCbCr, luma uses slot 0, Cb and Cr
share slot 1, and an optional extra plane uses slot 2. For RGB/RGBA, the coded
RCT Y plane uses slot 0, coded Cb and Cr share slot 1, and alpha uses slot 2.
Border samples are zero for all planes, including chroma/RCT chroma; neutral
chroma values are reconstructed as coded residuals from that zero border.
Full-run remainders may carry across row boundaries, but a run that reaches a
plane edge is clipped at that edge rather than carried into nonexistent
samples.

## Implemented Decoder Coverage

The current decoder implements:

- FFV1 versions 0 and 1, plus stable version 3 micro-version 4 or later.
- Range coding with default or stream-provided state transitions.
- Golomb-Rice coding.
- 1 through 16 bits per raw sample.
- Y-only, YCbCr with subsampling, extra plane, RGB, and RGBA layouts.
- Version 3 slice headers, footers, error status, and optional slice CRC.
- Multiple version 3 slices with deterministic parallel execution.
- Legacy version 0/1 range-coded multi-slice frame metadata can be parsed when
  the caller supplies complete stream parameters.
- Keyframes and non-keyframes with per-slice entropy-state continuation.
- Reordered non-keyframe slices matched to prior state by raster geometry.
- Runtime CPU dispatch with scalar, SSE2, and AVX2 RGB inverse color
  transforms.

## Current Limitations

- Unstable FFV1 version 2 is rejected.
- Version 3 micro-versions below 4 are rejected as unstable.
- Sample depths above 16 bits are unsupported.
- Colorspace types other than YCbCr and RGB are unsupported.
- Legacy version 0/1 frame-embedded `Parameters()` require an explicit
  `bootstrap_legacy_frame()` call before inspecting or decoding.
- Legacy version 0/1 Golomb-Rice multi-slice frame parsing is not implemented.
- Relaxed parsing mode is not implemented; `DecoderOptions::strict` must stay
  true.
- SIMD coverage is currently limited to the RGB inverse color transform on
  SSE2 and AVX2 targets.
- `FrameInfo` exposes stream-level layout metadata and a required plane table,
  but callers still choose buffer ownership and any stride padding.
- The library does not demultiplex containers or parse codec container
  metadata beyond the FFV1 payloads passed by the caller.
- The public API does not allocate output frames.

## Release Qualification Remaining

Before declaring a stable decoder release, the project should complete:

1. Licensed external conformance vectors covering supported profiles.
2. Black-box interoperability checks against independently produced streams.
3. Expanded parser and slice-decoder fuzz corpora retained from the standalone
   fuzz harnesses.
4. Passing sanitizer runs on supported toolchains, including the documented
   Visual Studio AddressSanitizer preset.
5. Final versioned release notes, release tag, and support window declaration.
