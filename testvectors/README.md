# mffv1 External Test Vectors

This directory is a placeholder for optional, locally generated FFV1 test
vectors. The repository intentionally does not include FFmpeg binaries,
FFmpeg headers, generated vectors, or sample MKV files.

The default `test_vector_data.hpp` defines:

```cpp
#define NO_DEFINE_TEST_VECTOR_DATA 1
```

Unit tests use this macro to skip external-vector checks when vectors have not
been generated.

Generated preview headers such as `test_vector_data_sample.hpp` are local
scratch artifacts and are ignored by Git. A generated `test_vector_data.hpp`
is also a local test artifact by default: use it to run interoperability tests,
then restore the placeholder before committing normal library changes. Commit
generated vector data only after its provenance entry is reviewed.
`createVector.zip` is intentionally not ignored so it can be reviewed and
committed separately if the generator code is accepted.

## Generator License Boundary

The mffv1 library remains independent of FFmpeg and is not linked to FFmpeg.
The optional mkv-to-C++ generator is a separate local GPL-3.0-or-later tool
that may link to FFmpeg libraries. Keep the generator archive, generator
binaries, and their license obligations separate from the MIT-licensed mffv1
library. Do not commit FFmpeg binaries or headers.

## FFmpeg Source

Use FFmpeg only as an external black-box tool and header/library provider for
the local generator. Do not copy FFmpeg source code into this repository.

Primary download page:

```text
https://github.com/BtbN/FFmpeg-Builds/releases
```

Download ffmpeg-master-latest-win64-gpl-shared.zip.

Keep all downloaded FFmpeg files outside version control.

## Suggested Local Layout

Create the following local-only layout:

```text
testvectors/
  ffmpeg/
    bin/
      *.dll
    include/
      libavcodec/
      libavformat/
      libavutil/
    lib/
      *.lib
  createVector.zip
  createVector/
  test_vector_data.hpp
```

If a downloaded archive does not contain `include/` and `lib/`, use a matching
FFmpeg development package or build FFmpeg locally. The generator may use
`ffmpeg.exe` / `ffprobe.exe` as external programs, but any direct linking to
FFmpeg libraries must remain confined to the generator tool.

## Generator Workflow

1. Download and extract FFmpeg locally.
2. Copy only the required binaries, headers, and import libraries into
   `testvectors/ffmpeg/`.
3. Place the mkv-to-C++ generator archive at:

   ```text
   testvectors/createVector.zip
   ```

4. Extract it into:

   ```text
   testvectors/createVector/
   ```

5. Configure and build the generator with CMake from the extracted directory using the provided preset:

   ```cmd
   cd testvectors/createVector
   cmake --preset vs2026-x64
   cmake --build --preset vs2026-x64-release
   ```

   *(Note: The build process will automatically copy the required FFmpeg DLLs into the output directory.)*

6. Run the generator on local MKV files containing FFV1 streams. You can pass the MKV files directly as arguments, or provide a text file containing a list of paths (one per line) prefixed with `@`:

   ```cmd
   cd build/vs2026-x64/Release
   mkv_to_cpp.exe "C:\path\to\video1.mkv" "C:\path\to\video2.mkv"

   :: Or using a list file:
   mkv_to_cpp.exe @list.txt
   ```

   This will generate `test_vector_data.hpp` in the current directory.

7. Copy the generated header back to the `testvectors/` root, replacing the placeholder file:

   ```cmd
   copy test_vector_data.hpp ..\..\..\..\test_vector_data.hpp
   ```

8. Reconfigure, rebuild, and run the mffv1 tests.
9. Restore the placeholder `test_vector_data.hpp` before committing ordinary
   mffv1 library changes unless the generated vectors are intentionally being
   added as reviewed repository fixtures.

## Generated Header Contract

The generated `test_vector_data.hpp` should be header-only and define the
following test-only API:

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
`info.stride_bytes * info.height` bytes. The tests compare those bytes exactly
after decoding through the public decoder API.

`frame_payloads[index]` and `expected_planes[index]` describe one decoded
frame. The tests configure one decoder per `DecodeVector`, then inspect and
decode the frame payloads in order. This means inter frames may rely on the
reference state produced by earlier payloads in the same vector.

## Provenance Rules

- Locally generated vector headers are optional user/developer test artifacts
  and are not committed by default.
- Users are responsible for ensuring they may use the media inputs they choose
  for local vector generation.
- Local vector generation does not change the mffv1 library license because
  the library does not depend on FFmpeg, the generator, or generated vectors.
- Generated vector data committed to the repository must come from media files
  and FFmpeg's public demuxing/codec-private output, not from FFmpeg source
  code.
- Do not commit FFmpeg binaries, FFmpeg headers, generated media files, or local
  generator build products unless their license and provenance have been
  reviewed separately.
- Keep the generator as a tool. The mffv1 library must not depend on FFmpeg.
