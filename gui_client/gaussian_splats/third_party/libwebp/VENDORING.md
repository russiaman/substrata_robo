# Vendored libwebp (decode-only)

Source: https://chromium.googlesource.com/webm/libwebp (mirror: https://github.com/webmproject/libwebp)
Commit: 733c91e461c18cf1127c9ed0a80dccbcfed599d3 (2026-07-02)
License: BSD-3-Clause (see COPYING)

## Scope

This is the file set of upstream's own `webpdecoder` CMake target (decode-only,
see `src/dec/Makefile.am`, `src/dsp/Makefile.am` COMMON_SOURCES, and
`src/utils/Makefile.am` COMMON_SOURCES) — no encoder, no demux/mux, no SIMD
variants (SSE2/SSE4.1/AVX2/NEON/MSA/MIPS sources excluded; the scalar C path
in dsp/*.c is used everywhere, matching the Emscripten/WASM target this is
built for).

Used to decode lossless WebP (VP8L) images referenced by SOG (PlayCanvas
Spatially Ordered Gaussians) splat files. See
`snapshots/2026-07-26_phase2-architecture-contract.md` §4.7 for the
dependency reasoning.

## Modifications

None. Files are byte-for-byte copies from upstream at the commit above.
