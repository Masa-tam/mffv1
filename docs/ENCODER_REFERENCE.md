# mffv1 Encoder Reference

## Status

The mffv1 encoder has a complete scalar baseline suitable for technical-preview
evaluation and pre-release integration testing. It produces FFV1 version 3
frames with a configurable keyframe cadence and supports the principal planar
YCbCr and RGB profiles described below.

A stable general release still requires external interoperability testing,
licensed conformance vectors, fuzzing, sanitizer coverage, and packaging.
Runtime CPU dispatch is implemented. The RGB forward color transform uses AVX2
or SSE2 row kernels on supported x86/x64 systems and otherwise uses the scalar
reference implementation.

This document describes implemented public behavior. Internal design notes and
future-work records are kept outside the user-facing documentation set.

## Header And Namespace

Include the umbrella header:

```cpp
#include <mffv1/codec.hpp>
```

All public names are in namespace `mffv1`. The library requires C++20.
Encoder instances are returned as `std::unique_ptr<IEncoder>` and hide entropy,
prediction, slice scheduling, threading, and CPU dispatch implementation
classes.

Input frame layout is described in the
[Frame Buffer Reference](FRAME_BUFFER_REFERENCE.md).

## Lifecycle

The normal lifecycle is:

1. Fill `EncoderOptions`.
2. Call `create_encoder()`.
3. Fill `StreamInfo`.
4. Call `configure()` and store the returned Configuration Record.
5. Prepare caller-owned input planes.
6. Call `encode_frame()` once per frame.

One encoder instance represents one configured FFV1 stream. Calling
`configure()` again replaces the previous stream configuration on success.

Encoder instances are not thread-safe. Do not call methods concurrently on
the same instance. Slice encoding inside one call may run in parallel.
Separate encoder instances may be used concurrently.

## Creating An Encoder

```cpp
mffv1::EncoderOptions options;
options.version = 3;
options.entropy_mode = mffv1::EntropyMode::Range;
options.thread_count = 0;

auto result = mffv1::create_encoder(options);
if (!result.status.ok()) {
    // Handle result.status.
}
std::unique_ptr<mffv1::IEncoder> encoder =
    std::move(result.encoder);
```

Declaration:

```cpp
EncoderFactoryResult create_encoder(const EncoderOptions& options);
```

On success, `status.ok()` is true and `encoder` is non-null. A negative
`thread_count` is rejected and returns a null encoder.

### `EncoderOptions`

| Field | Meaning |
| --- | --- |
| `thread_count` | `0` selects hardware concurrency, `1` forces serial slice encoding, and a positive value sets the maximum slice workers. |
| `version` | Requested FFV1 version. The current encoder accepts only version 3. Version 0 and 1 are decoder compatibility targets, not encoder output targets. |
| `entropy_mode` | Selects `EntropyMode::Range` or `EntropyMode::GolombRice`. |
| `keyframe_interval` | Number of frames between generated keyframes. The default `1` makes every frame a keyframe. Values greater than `1` enable non-keyframe continuation. `0` is rejected. |
| `cpu` | Controls runtime CPU feature selection. Dispatch is resolved once when the encoder is created. |

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

The current dispatch table prefers AVX2 and then SSE2 for the RGB forward
color-transform row kernel when permitted. SSSE3 and NEON are detected but do
not yet select specialized kernels. All dispatch choices produce byte-identical
FFV1 output.

## Configuring A Stream

```cpp
mffv1::StreamInfo stream;
stream.width = width;
stream.height = height;
stream.version = 3;
stream.bits_per_raw_sample = 8;
stream.color_space = mffv1::ColorSpace::YCbCr;
stream.has_chroma_planes = false;

mffv1::ConfigurationRecord record;
const mffv1::Status status =
    encoder->configure(stream, record);
```

Declaration:

```cpp
virtual Status configure(const StreamInfo& stream,
                         ConfigurationRecord& out_record) = 0;
```

On success, `out_record.bytes` contains a complete FFV1 version 3 Configuration
Record including four-byte CRC parity. Container integration should store or
transport these bytes as codec configuration data.

The input `StreamInfo` does not need to remain alive after the call.

`configure()` is transactional:

- On success, the encoder replaces its prior configuration and replaces
  `out_record.bytes`.
- On failure, the prior encoder configuration and `out_record` are unchanged.

### `StreamInfo`

| Field | Meaning |
| --- | --- |
| `width` | Coded frame width. Must be non-zero. |
| `height` | Coded frame height. Must be non-zero. |
| `version` | Must equal `EncoderOptions::version`; currently both must be 3. |
| `bits_per_raw_sample` | Input sample depth from 8 through 16. |
| `log2_h_chroma_subsample` | Horizontal YCbCr chroma subsampling exponent. |
| `log2_v_chroma_subsample` | Vertical YCbCr chroma subsampling exponent. |
| `has_chroma_planes` | Enables Cb/Cr for YCbCr or the required G/B color planes for RGB. |
| `has_extra_plane` | Enables a full-resolution alpha plane. |
| `color_space` | Selects planar YCbCr or planar RGB input. |
| `num_h_slices` | Number of columns in the version 3 slice raster. |
| `num_v_slices` | Number of rows in the version 3 slice raster. |
| `error_status_enabled` | Enables version 3 slice error-status and CRC footer fields. Generated slices currently write error status `0`. |

### Supported Color Layouts

The current encoder supports:

- Y-only, optionally with a full-resolution alpha plane.
- Planar YCbCr 4:4:4.
- Planar YCbCr 4:2:2.
- Planar YCbCr 4:2:0.
- Any supported YCbCr layout with a full-resolution alpha plane.
- Planar RGB.
- Planar RGBA.

For RGB, `has_chroma_planes` must be true and both subsampling exponents must be
zero. The name `has_chroma_planes` is inherited from FFV1 Parameters syntax; in
an RGB stream it means that all three color planes are present.

Supported YCbCr exponent pairs are:

| Layout | Horizontal | Vertical |
| --- | ---: | ---: |
| 4:4:4 | 0 | 0 |
| 4:2:2 | 1 | 0 |
| 4:2:0 | 1 | 1 |

Subsampling is invalid when `has_chroma_planes` is false.

### Current Coded Profile

The generated Configuration Record currently declares:

- FFV1 version 3, stable micro-version 4.
- Range or Golomb-Rice entropy coding.
- One zero quantization table set.
- Default range state transitions.
- Intra-only coding when `EncoderOptions::keyframe_interval == 1`.
- Non-intra coding when `EncoderOptions::keyframe_interval > 1`.
- Default initial range states.
- Error-status and slice CRC fields when `StreamInfo::error_status_enabled`
  is true; otherwise, these slice footer fields are omitted.

The lower-level `codec::ConfigurationRecordWriter` can also serialize custom
quantization table sets, custom range state transitions from
`syntax::StreamParameters` by writing range coder type 2 and the signed
transition deltas, and coded initial range states with `states_coded=1`. The
public encoder profile builder still generates one zero quantization table set,
the default range state transition table, and default initial states.

With the default options, every generated frame is a keyframe. When
`keyframe_interval` is greater than one, frame zero and every Nth frame after it
are keyframes, and intervening frames continue each slice's prediction and
entropy state from the previous encoded frame.

Range and Golomb-Rice non-keyframes are stateful. A failed `encode_frame()` call
does not advance the keyframe cadence and does not commit partial slice state.

## Slice Grid

The encoder emits one slice per raster cell in row-major order. Keyframes reset
prediction, entropy contexts, and Golomb-Rice run state per slice. Non-keyframes
continue the previous state for the matching raster cell. Only the first slice
carries the frame keyframe flag.

Both slice counts must be non-zero. The grid must not create an empty region
in any coded plane. In particular, subsampled chroma planes may impose a
smaller maximum grid than the luma dimensions.

For FFV1 version 3 frames larger than CIF (`352 x 288` pixels), at least four
raster cells are required by the parallel slice-area constraint.

Serial and parallel encoding with otherwise identical options produces
byte-identical output.

## Encoding A Frame

```cpp
mffv1::EncodedFrame frame;
const mffv1::Status status =
    encoder->encode_frame(input, frame);
```

Declaration:

```cpp
virtual Status encode_frame(FrameView input,
                            EncodedFrame& out_frame) = 0;
```

The encoder must be successfully configured first. Input plane arrays and
sample storage need to remain valid only until the call returns.

`encode_frame()` is transactional:

- All frame and plane metadata is validated before slice workers start.
- Every slice writes to its own private byte buffer.
- The final frame is assembled in raster order only after all slices succeed.
- On success, `out_frame.bytes` is replaced.
- On failure, `out_frame` is unchanged.

When multiple slices fail, the reported error is selected by the lowest
zero-based slice index, independent of worker completion order.

### Range Context Scope

For version 3 range-coded output, the encoder writes the first slice's
`keyframe` symbol, resets scalar contexts to a fresh Slice Header scope, then
continues the same arithmetic range coder state into Slice Content while
reconfiguring scalar context banks from Slice Header quantization-table index
slots.

Range-coded Slice Content banks are slot-based. For YCbCr streams with chroma
planes, luma uses slot 0 and both Cb and Cr use slot 1. An optional extra plane
uses its own slot. The current public encoder writes the default quantization
table set indexes, but this slot mapping is kept aligned with the decoder so
custom table support can reuse the same state model later.

Golomb-Rice adaptive VLC context state follows the same version 3 slot mapping.
For YCbCr, luma uses slot 0, Cb and Cr share slot 1, and an optional extra
plane uses slot 2. For RGB/RGBA, the coded RCT Y plane uses slot 0, coded Cb
and Cr share slot 1, and alpha uses slot 2. This is independent of whether two
slots currently reference the same quantization-table-set value. Chroma and RCT
chroma borders are encoded against a zero border value, so neutral chroma
samples are represented by their residual from zero at frame and slice edges.

### Input Sample Validation

Samples are unsigned. For bit depths below 16, every stored value must fit the
configured bit depth. For example, a 10-bit stream accepts values from 0
through 1023 even though storage uses `std::uint16_t`.

An out-of-range value returns `InvalidArgument`. The status includes a slice
index when the error was discovered by a slice worker.

## Input Frame Requirements

`FrameView` is non-owning:

```cpp
struct FrameView {
    const PlaneView* planes;
    std::size_t plane_count;
};

struct PlaneView {
    const void* data;
    PlaneInfo info;
};
```

The input plane count must exactly equal the configured coded plane count.
Every data pointer must be non-null. Roles, sample formats, dimensions, and
strides are validated.

Unlike decoder output planes, encoder input plane dimensions must exactly
match the configured dimensions. Extra rows or columns are not accepted.

See the [Frame Buffer Reference](FRAME_BUFFER_REFERENCE.md) for plane order,
dimension formulas, storage, stride, ownership, and complete examples.

## Minimal Y-Only Example

```cpp
const std::uint32_t width = 1920;
const std::uint32_t height = 1080;

mffv1::EncoderOptions options;
options.thread_count = 0;
auto created = mffv1::create_encoder(options);
if (!created.status.ok()) {
    return created.status;
}

mffv1::StreamInfo stream;
stream.width = width;
stream.height = height;
stream.bits_per_raw_sample = 8;
stream.has_chroma_planes = false;
stream.num_h_slices = 2;
stream.num_v_slices = 2;

mffv1::ConfigurationRecord configuration;
auto status = created.encoder->configure(stream, configuration);
if (!status.ok()) {
    return status;
}

std::vector<std::uint8_t> pixels(
    static_cast<std::size_t>(width) * height);

mffv1::PlaneView plane;
plane.data = pixels.data();
plane.info.role = mffv1::PlaneRole::Y;
plane.info.sample_format = mffv1::SampleFormat::UInt8;
plane.info.width = width;
plane.info.height = height;
plane.info.stride_bytes = width;

const mffv1::FrameView input{&plane, 1};
mffv1::EncodedFrame frame;
status = created.encoder->encode_frame(input, frame);
```

`configuration.bytes` belongs with the stream configuration.
`frame.bytes` contains one complete FFV1 frame payload; container framing and
timestamps remain the caller's responsibility.

## Error Handling

All operations return `Status`. Branch on `ErrorCode`, not diagnostic message
text.

| Code | Encoder meaning |
| --- | --- |
| `Ok` | Operation succeeded. |
| `InvalidArgument` | Options, stream geometry, planes, stride, or sample values are invalid. |
| `InvalidState` | `encode_frame()` was called before successful configuration. |
| `UnsupportedFeature` | A valid FFV1 profile is outside current encoder support. |
| `SyntaxError` | An internal syntax writer rejected values that cannot form the requested stream. |
| `CrcMismatch` | Reserved for CRC-related operations; normally not returned by encoding. |
| `ResourceExhausted` | A size, address, container, or allocation-related limit was exceeded. |
| `NotImplemented` | Reserved for future recognized paths; unsupported encoder profiles use `UnsupportedFeature`. |
| `InternalError` | An internal invariant failed. |

`Status::message` is diagnostic and is not a stable machine-readable contract.
Builds configured with `MFFV1_ENABLE_STATUS_MESSAGES=OFF` keep the field but
leave library-generated messages empty. Use `status.location.has_slice_index`
before reading `slice_index`.

## Implemented Encoder Coverage

- FFV1 version 3, stable micro-version 4.
- Range coding with the default transition table.
- Golomb-Rice coding.
- Configurable keyframe cadence with stateful non-keyframe continuation.
- 8 through 16 bits per raw sample.
- Y-only, YCbCr 4:4:4, 4:2:2, 4:2:0, and optional alpha.
- RGB and RGBA with the reversible FFV1 color transform.
- Independent version 3 slices and deterministic parallel encoding.
- Complete Configuration Records with CRC parity.
- Optional version 3 error-status and per-slice CRC footer output.
- Runtime CPU feature resolution and immutable kernel dispatch.
- AVX2 and SSE2 RGB forward color-transform rows with scalar tails.

## Current Limitations

- Versions 0, 1, and 2 are not encoded.
  Versions 0 and 1 remain decode-only compatibility targets because their
  historical range-state and no-Codec-Private behavior is container- and
  implementation-sensitive. New encoder output should use version 3.
- Custom quantization tables are absent from the public encoder profile.
- Generated error-status values are always `0`; the encoder does not report
  correctable or uncorrectable slice errors in its own output.
- Sample depths above 16 are unsupported.
- Colorspaces other than planar YCbCr and planar RGB are unsupported.
- Input format conversion, packed pixels, and negative strides are unsupported.
- SIMD coverage is currently limited to AVX2 and SSE2 RGB forward color
  transforms; YCbCr paths, prediction, entropy coding, and other architectures
  remain scalar.
- The library does not mux containers or generate container metadata.
- Output buffer reuse or caller-provided compressed output storage is not
  currently exposed.

## Release Qualification Remaining

Before declaring a stable encoder release, the project should complete:

1. Licensed external conformance vectors for every supported profile.
2. Black-box interoperability checks with independent FFV1 decoders.
3. Expanded encoder fuzz corpora retained from the standalone fuzz harnesses.
4. Passing sanitizer runs on supported toolchains, including the documented
   Visual Studio AddressSanitizer preset.
5. Final versioned release notes, release tag, and support window declaration.
6. Measured SIMD kernels with exhaustive scalar equivalence tests.
