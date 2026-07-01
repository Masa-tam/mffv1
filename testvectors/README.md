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

## FFmpeg Source

Use FFmpeg only as an external black-box tool and header/library provider for
the local generator. Do not copy FFmpeg source code into this repository.

Primary download page:

```text
https://ffmpeg.org/download.html
```

Windows build example:

```text
https://www.gyan.dev/ffmpeg/builds/
```

The gyan.dev release page provides `ffmpeg-release-essentials.zip` and SHA-256
files. These builds are convenient for local extraction, but check the build
license notes before redistribution. Keep all downloaded FFmpeg files outside
version control.

## Suggested Local Layout

Create the following local-only layout:

```text
testvectors/
  ffmpeg/
    bin/
      ffmpeg.exe
      ffprobe.exe
    include/
      libavcodec/
      libavformat/
      libavutil/
    lib/
      avcodec.lib
      avformat.lib
      avutil.lib
  createVector.zip
  createVector/
  generated/
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

5. Configure and build the generator with CMake from the extracted directory.
   Point its FFmpeg include, library, and binary paths at
   `testvectors/ffmpeg/`.
6. Run the generator on local MKV files containing FFV1 streams.
7. Replace `testvectors/test_vector_data.hpp` with the generated C++ header.
   The generated header should not define `NO_DEFINE_TEST_VECTOR_DATA`.
8. Reconfigure, rebuild, and run the mffv1 tests.

## Provenance Rules

- Generated vector data should come from media files and FFmpeg's public
  demuxing/codec-private output, not from FFmpeg source code.
- Do not commit FFmpeg binaries, FFmpeg headers, generated media files, or local
  generator build products unless their license and provenance have been
  reviewed separately.
- Keep the generator as a tool. The mffv1 library must not depend on FFmpeg.
