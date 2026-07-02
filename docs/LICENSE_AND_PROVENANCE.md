# License And Clean-Room Provenance

## Project License

mffv1 is licensed under the MIT License. The authoritative license text is in
the repository root:

- [MIT License](../LICENSE)

The intended result is a permissively licensed, modular C++20 implementation
of the FFV1 codec that can be integrated independently of container libraries
and independently of FFmpeg.

## Clean-Room Basis

mffv1 is an independent implementation of the FFV1 video coding format. Its
normative format reference is:

- RFC 9043, "FFV1 Video Coding Format Versions 0, 1, and 3"
- https://www.rfc-editor.org/rfc/rfc9043.html

Project implementation work is based on:

- Public specifications and errata.
- Project-owned design documents and tests.
- Generated bitstreams and test vectors whose provenance permits their use.
- Black-box interoperability observations when their source and conditions are
  recorded.

FFmpeg source code is not used as implementation source material for mffv1.
Code, comments, identifiers, internal structures, and implementation-specific
tables are not copied or translated from FFmpeg or other incompatibly licensed
codec implementations.

FFV1 identifies the standardized codec format. mffv1 is a separate project and
is not affiliated with, endorsed by, or an official component of FFmpeg.

## Contribution Rule

Contributions must preserve the clean-room basis:

- Do not submit code copied or translated from FFmpeg or another implementation
  whose license is incompatible with this project.
- Derive codec behavior from public specifications, permitted test material,
  or independently documented reasoning.
- Record the source and license of externally supplied conformance vectors
  before committing them.
- Keep third-party code in an explicit dependency boundary with its original
  license and notice intact.

When provenance is uncertain, do not contribute the material until its origin
and permitted use are established.

## Third-Party Software

Third-party software is not covered by the mffv1 MIT License unless its own
license explicitly says otherwise. Current notices are collected in:

- [Third-Party Notices](../THIRD_PARTY_NOTICES.md)

GoogleTest is used for development and testing through the
`third-party/googletest` submodule and retains its own BSD-style license.

## Test Vectors

Generated project-owned vectors may be covered by the project license.
Externally supplied vectors must be accompanied by provenance and licensing
information. Record committed external vectors in:

- [Test Vector Registry](test-vectors.md)

The `testvectors/` directory is an optional local workspace for black-box
interoperability vectors. When no local `testvectors/test_vector_data.hpp`
exists, CMake generates a build-tree placeholder defining
`NO_DEFINE_TEST_VECTOR_DATA` so the related tests are skipped.

Locally generated vector headers are not part of the distributed library by
default. Users and developers may generate them from media they are allowed to
use, run the interoperability tests, and remove the local header before
committing ordinary changes. This local workflow does not change the mffv1
library license because the library builds and runs without FFmpeg, any
generator, or generated vectors.

The intended external-vector workflow is:

- Use FFmpeg only as a locally installed binary/header provider or black-box
  demuxing tool.
- Do not copy FFmpeg source code, implementation comments, internal structures,
  or implementation-specific tables into this repository.
- Do not commit downloaded FFmpeg binaries, FFmpeg headers, generated media
  files, or local generator build products unless their license and provenance
  have been reviewed separately.
- Generated C++ vector data committed to the repository must be traceable to
  input media files and public container/codec metadata extraction, not to
  FFmpeg source code.
