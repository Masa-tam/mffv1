# mffv1 Frame Buffer Reference

## Purpose

This document defines the public, caller-owned frame and plane views used by
both the mffv1 encoder and decoder.

- The encoder reads `FrameView` and `PlaneView`.
- The decoder writes `MutableFrameView` and `MutablePlaneView`.
- The library does not allocate uncompressed frame buffers.
- Views never own sample storage.

All public names are declared by:

```cpp
#include <mffv1/codec.hpp>
```

## Ownership And Lifetime

`FrameView` and `MutableFrameView` borrow both the plane array and each plane's
sample storage.

The caller must keep these objects valid until the public call returns:

- The `PlaneView` or `MutablePlaneView` array.
- Every plane data pointer.
- Every row address reachable through `stride_bytes`.

The library does not retain frame pointers after `encode_frame()` or
`decode_frame()` returns.

The compressed types are different: `ConfigurationRecord::bytes` and
`EncodedFrame::bytes` are owning `std::vector<std::byte>` values.

## Public Types

```cpp
struct PlaneInfo {
    PlaneRole role;
    SampleFormat sample_format;
    std::uint32_t width;
    std::uint32_t height;
    std::ptrdiff_t stride_bytes;
};

struct PlaneView {
    const void* data;
    PlaneInfo info;
};

struct MutablePlaneView {
    void* data;
    PlaneInfo info;
};

struct FrameView {
    const PlaneView* planes;
    std::size_t plane_count;
};

struct MutableFrameView {
    MutablePlaneView* planes;
    std::size_t plane_count;
};
```

An empty or null plane array is invalid when the configured stream requires
planes. Every required plane needs a non-null data pointer.

## Plane Roles And Order

Plane roles are explicit and validated. Array position and `PlaneRole` must
both match the configured stream.

### YCbCr

| Index | Role | Present when |
| ---: | --- | --- |
| 0 | `PlaneRole::Y` | Always |
| 1 | `PlaneRole::Cb` | Chroma planes enabled |
| 2 | `PlaneRole::Cr` | Chroma planes enabled |
| 3 or 1 | `PlaneRole::Alpha` | Extra plane enabled |

When chroma is absent and an extra plane is present, alpha immediately follows
Y at index 1.

### RGB

| Index | Role | Present when |
| ---: | --- | --- |
| 0 | `PlaneRole::R` | Always |
| 1 | `PlaneRole::G` | Always |
| 2 | `PlaneRole::B` | Always |
| 3 | `PlaneRole::Alpha` | Extra plane enabled |

RGB is planar. Packed RGB, BGR, RGB24, RGB48, and interleaved RGBA buffers are
not accepted directly.

## Sample Formats

| Configured bit depth | `SampleFormat` | C++ storage element |
| --- | --- | --- |
| 1 through 8 | `UInt8` | `std::uint8_t` |
| 9 through 16 | `UInt16` | `std::uint16_t` |

The current encoder supports bit depths 8 through 16. The decoder supports
depths 1 through 16 where allowed by the coded stream.

`UInt16` uses native C++ integer representation, not a serialized byte order.
Storage should be naturally aligned for `std::uint16_t`, especially for
decoder output.

For depths below the storage width, unused high bits are not part of the
sample. Encoder input values must not exceed:

```text
maximum_sample = 2^bits_per_raw_sample - 1
```

## Plane Dimensions

Luma, RGB, and alpha planes use full frame dimensions:

```text
plane_width  = frame_width
plane_height = frame_height
```

YCbCr chroma dimensions use ceiling division:

```text
chroma_width =
    ceil(frame_width / 2^log2_h_chroma_subsample)

chroma_height =
    ceil(frame_height / 2^log2_v_chroma_subsample)
```

Equivalent integer formulas are:

```text
chroma_width =
    (frame_width + (1 << h) - 1) >> h

chroma_height =
    (frame_height + (1 << v) - 1) >> v
```

This matters for odd frame sizes. A `5 x 3` 4:2:0 frame has `3 x 2` chroma
planes.

### Encoder Dimension Rule

Encoder input plane dimensions must exactly equal the configured dimensions.
Extra columns or rows are rejected.

### Decoder Dimension Rule

Decoder output plane dimensions may be larger than required. The decoder writes
only the coded rectangle and ignores storage outside it.

## Stride

`stride_bytes` is the byte distance from the start of one row to the start of
the next row.

The minimum is:

```text
minimum_stride =
    plane_width * bytes_per_sample
```

Examples:

- An 8-bit plane 1920 samples wide requires at least 1920 bytes.
- A 10-bit plane 1920 samples wide uses `UInt16` and requires at least
  3840 bytes.

Positive row padding is supported. The encoder ignores padding bytes. The
decoder preserves padding bytes outside the coded row width.

Negative strides are not supported.

The caller must ensure the last required row and sample are within the
allocated object. mffv1 validates arithmetic representability but cannot know
the actual allocation size behind a raw pointer.

## Contiguous Plane Helper

For a tightly packed plane:

```cpp
template <typename T>
std::ptrdiff_t packed_stride(std::uint32_t width)
{
    return static_cast<std::ptrdiff_t>(
        static_cast<std::size_t>(width) * sizeof(T));
}
```

The caller remains responsible for checking size conversions and allocation
overflow before constructing storage for untrusted dimensions.

## Encoder Example: Odd-Sized 4:2:0

```cpp
const std::uint32_t width = 5;
const std::uint32_t height = 3;
const std::uint32_t chroma_width = 3;
const std::uint32_t chroma_height = 2;

std::vector<std::uint8_t> y(width * height);
std::vector<std::uint8_t> cb(chroma_width * chroma_height);
std::vector<std::uint8_t> cr(chroma_width * chroma_height);

std::array<mffv1::PlaneView, 3> planes{{
    {
        y.data(),
        {
            mffv1::PlaneRole::Y,
            mffv1::SampleFormat::UInt8,
            width,
            height,
            static_cast<std::ptrdiff_t>(width),
        },
    },
    {
        cb.data(),
        {
            mffv1::PlaneRole::Cb,
            mffv1::SampleFormat::UInt8,
            chroma_width,
            chroma_height,
            static_cast<std::ptrdiff_t>(chroma_width),
        },
    },
    {
        cr.data(),
        {
            mffv1::PlaneRole::Cr,
            mffv1::SampleFormat::UInt8,
            chroma_width,
            chroma_height,
            static_cast<std::ptrdiff_t>(chroma_width),
        },
    },
}};

const mffv1::FrameView input{planes.data(), planes.size()};
```

The matching `StreamInfo` uses:

```cpp
stream.width = 5;
stream.height = 3;
stream.has_chroma_planes = true;
stream.log2_h_chroma_subsample = 1;
stream.log2_v_chroma_subsample = 1;
```

## Decoder Example: Ten-Bit Y Plane

```cpp
const std::uint32_t width = 1920;
const std::uint32_t height = 1080;

std::vector<std::uint16_t> pixels(
    static_cast<std::size_t>(width) * height);

mffv1::MutablePlaneView plane;
plane.data = pixels.data();
plane.info.role = mffv1::PlaneRole::Y;
plane.info.sample_format = mffv1::SampleFormat::UInt16;
plane.info.width = width;
plane.info.height = height;
plane.info.stride_bytes =
    static_cast<std::ptrdiff_t>(width * sizeof(std::uint16_t));

mffv1::MutableFrameView output{&plane, 1};
```

Decoded values occupy the low 10 bits and are returned as ordinary
`std::uint16_t` values from 0 through 1023.

## Padded Row Example

```cpp
const std::uint32_t width = 640;
const std::uint32_t height = 480;
const std::ptrdiff_t stride = 672;

std::vector<std::uint8_t> storage(
    static_cast<std::size_t>(stride) * height);

mffv1::PlaneView plane;
plane.data = storage.data();
plane.info.role = mffv1::PlaneRole::Y;
plane.info.sample_format = mffv1::SampleFormat::UInt8;
plane.info.width = width;
plane.info.height = height;
plane.info.stride_bytes = stride;
```

Only the first 640 bytes of each row are samples. The remaining 32 bytes are
caller-owned padding.

## Validation Differences

| Property | Encoder input | Decoder output |
| --- | --- | --- |
| Plane count | Must exactly match | May include additional planes |
| Width/height | Must exactly match | May be larger than required |
| Role | Must match | Must match |
| Sample format | Must match | Must match |
| Data pointer | Must be non-null | Must be non-null |
| Negative stride | Unsupported | Unsupported |
| Padding | Read but ignored outside width | Preserved outside width |
| Sample range | Validated against bit depth | Produced within bit depth |

## Common Errors

### Swapped Chroma

Passing Cr before Cb is invalid even when dimensions are identical. Roles and
order are both checked.

### Using Full Resolution For 4:2:0 Chroma

Encoder input requires exact chroma dimensions. A full-resolution Cb/Cr buffer
is not accepted for a subsampled stream.

### Treating Ten-Bit Samples As Packed Bytes

`SampleFormat::UInt16` means one native `std::uint16_t` per sample. Packed
10-bit formats require conversion before calling mffv1.

### Stride In Samples Instead Of Bytes

`stride_bytes` is always measured in bytes. A 10-bit plane with 1920 samples
per row normally uses a stride of 3840, not 1920.

### Temporary Plane Arrays

The plane array may be local, but it must remain alive until the codec call
returns. Do not pass a pointer to an already-destroyed temporary container.

## Threading

Parallel slice workers may read disjoint regions of the same encoder input
planes or write disjoint regions of the same decoder output planes. The caller
must not modify or access those buffers concurrently until the codec call
returns.

Calls on separate codec instances may use separate frames concurrently.

