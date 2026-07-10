## Summary

Describe the change and the user-visible behavior it affects.

## Source-Provenance Confirmation

- [ ] This change is based on public specifications, generated tests,
      black-box observations, or original reasoning.
- [ ] This change does not copy or translate code, comments, identifiers,
      internal tables, or implementation details from FFmpeg or another
      incompatibly licensed implementation.
- [ ] Any new committed test vectors have recorded provenance and license
      review in `testvectors/REGISTRY.md`.

## Verification

Check the commands that were run:

- [ ] `cmake --preset vs2026-x64`
- [ ] `cmake --build --preset vs2026-x64-debug`
- [ ] `ctest --preset vs2026-x64-debug --output-on-failure`
- [ ] `cmake -DMFFV1_MARKDOWN_LINK_ROOT=. -P cmake\CheckMarkdownLinks.cmake`
- [ ] package smoke test, if the change affects CMake packaging, public
      headers, installed docs, or release artifacts

## Documentation

- [ ] Public API references were updated, or this change does not affect public
      behavior.
- [ ] `CHANGELOG.md` was updated, or this change has no user-visible release
      note.
- [ ] Release-preparation changes keep `CMakeLists.txt` version,
      `CHANGELOG.md`, and the intended tag name aligned, or this change is not
      release preparation.

## Portability

- [ ] New platform or compiler behavior includes compiler version, CMake
      generator/options, and test result, or this change is not portability
      related.
- [ ] CI coverage was added when the platform is available through GitHub
      Actions, or the reason is noted above.

## Optimization

- [ ] Performance changes remain isolated, preserve the readable scalar path,
      and include equivalence or regression tests, or this change is not an
      optimization.
