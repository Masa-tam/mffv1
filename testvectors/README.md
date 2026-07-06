# mffv1 External Test Vector Drop

This directory is a local-only drop point for optional FFV1 interoperability
test vectors. The mffv1 repository intentionally does not commit generated
vector headers, FFmpeg binaries, FFmpeg headers, media files, or generator
outputs in this directory.

Only this `README.md` and `.gitignore` are intended to be tracked here.

## Default Behavior

Unit tests include a header named `test_vector_data.hpp`.

When `testvectors/test_vector_data.hpp` is absent, CMake generates a build-tree
fallback header containing:

```cpp
#pragma once

#define NO_DEFINE_TEST_VECTOR_DATA 1
```

The test-vector unit tests use this macro to skip external-vector checks.
This keeps the normal mffv1 build independent of FFmpeg, any vector generator,
and generated vector data.

## Test Controls

The generated-vector tests accept these optional environment variables:

- `MFFV1_TEST_VECTOR_FILTER`: run only vectors whose names contain the filter
  text.
- `MFFV1_TEST_VECTOR_TRACE_BOOTSTRAP`: when nonzero, report legacy bootstrap
  diagnostics for matched vectors that decode successfully.
- `MFFV1_TEST_VECTOR_REQUIRE_ALL_SUPPORTED`: when nonzero, fail if any matched
  generated vector is still classified as an unsupported compatibility gap.

## Local Vector Use

To run external-vector tests, place a locally generated
`test_vector_data.hpp` in this directory before configuring the build. CMake
will include that local header instead of generating the fallback header.

After running the tests, remove or replace the local header before committing
ordinary mffv1 library changes. Local generated headers are test artifacts and
are ignored by Git.

The generator used to create this header is intentionally outside this
repository. It may be provided by a separate project, submodule, package, or
manual process, as long as the resulting header follows the contract below.

## Generated Header Contract

The generated `test_vector_data.hpp` must be header-only and define this
test-only API:

```cpp
#pragma once

#include "mffv1/frame.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace mffv1_testvectors {

struct PlaneVector {
    mffv1::PlaneInfo info;
    std::span<const std::byte> samples;
};

struct DecodeVector {
    std::string_view name;
    std::uint32_t frame_width = 0;
    std::uint32_t frame_height = 0;
    std::span<const std::byte> configuration_record;
    std::vector<std::span<const std::byte>> frame_payloads;
    std::vector<std::span<const PlaneVector>> expected_planes;
};

std::span<const DecodeVector> decode_vectors();

} // namespace mffv1_testvectors
```

The `samples` span for each plane must contain at least
`info.stride_bytes * info.height` bytes. The tests compare only active sample
bytes in each row, namely `info.width * bytes_per_sample`; stride padding is
treated as caller-owned storage and is ignored by the comparison.

`frame_payloads[index]` and `expected_planes[index]` describe one decoded
frame. The tests configure one decoder per `DecodeVector`, then inspect and
decode the frame payloads in order. Inter frames may rely on the reference
state produced by earlier payloads in the same vector.

## Commit Policy

Do not commit local vector data from this directory by default. If generated
vectors are intentionally promoted to repository fixtures, add them through a
separate reviewed change and record their provenance in
`docs/test-vectors.md`.
