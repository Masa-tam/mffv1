# mffv1 External Test Vector Drop

This directory is a local-only drop point for optional FFV1 interoperability
test vectors. The mffv1 repository intentionally does not commit generated
vector headers, FFmpeg binaries, FFmpeg headers, media files, or generator
outputs in this directory.

Only this `README.md` and `.gitignore` are intended to be tracked here. The
directory `.gitignore` ignores every other path by default, and the public tree
check rejects tracked test-vector payloads unless that policy is deliberately
changed.

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
  diagnostics for matched legacy vectors, including unsupported-vector reports.
- `MFFV1_TEST_VECTOR_TRY_UNSUPPORTED`: when nonzero, attempt to decode matched
  vectors even if the legacy bootstrap-support precheck would normally fail or
  the harness has a named compatibility gap for that vector, so the public
  decode error path can be inspected directly.

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

The tests also perform lightweight metadata checks on vector names. Names that
contain `inter` should expose at least two frame payloads, and names that
contain `2frames` or `3frames` should expose exactly that many frame payloads
and expected-plane sets. This helps catch generator or batch-file naming
mistakes before they become misleading compatibility notes.

Some local vectors may intentionally cover profiles that are still under
investigation. The public test harness names those known gaps and skips them by
default so that new local coverage does not hide regressions in already
supported profiles. Set `MFFV1_TEST_VECTOR_TRY_UNSUPPORTED=1` with
`MFFV1_TEST_VECTOR_FILTER` to force one of those vectors through the decoder and
inspect the failure diagnostics.

## Commit Policy

Do not commit local vector data from this directory by default. If generated
vectors are intentionally promoted to repository fixtures, change the tracked
file policy through a separate reviewed change and record their provenance in
`testvectors/REGISTRY.md`. Avoid force-adding files from this directory for
ordinary library, documentation, or test-maintenance commits.
