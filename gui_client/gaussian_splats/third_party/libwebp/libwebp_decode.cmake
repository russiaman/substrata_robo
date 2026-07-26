# Vendored libwebp, decode-only, scalar (no SIMD variants). See VENDORING.md.
set(WEBPDECDIR "${CMAKE_CURRENT_LIST_DIR}")

set(webp_decode_sources
${WEBPDECDIR}/src/dec/alpha_dec.c
${WEBPDECDIR}/src/dec/alphai_dec.h
${WEBPDECDIR}/src/dec/buffer_dec.c
${WEBPDECDIR}/src/dec/common_dec.h
${WEBPDECDIR}/src/dec/frame_dec.c
${WEBPDECDIR}/src/dec/idec_dec.c
${WEBPDECDIR}/src/dec/io_dec.c
${WEBPDECDIR}/src/dec/quant_dec.c
${WEBPDECDIR}/src/dec/tree_dec.c
${WEBPDECDIR}/src/dec/vp8_dec.c
${WEBPDECDIR}/src/dec/vp8_dec.h
${WEBPDECDIR}/src/dec/vp8i_dec.h
${WEBPDECDIR}/src/dec/vp8l_dec.c
${WEBPDECDIR}/src/dec/vp8li_dec.h
${WEBPDECDIR}/src/dec/webp_dec.c
${WEBPDECDIR}/src/dec/webpi_dec.h

${WEBPDECDIR}/src/dsp/alpha_processing.c
${WEBPDECDIR}/src/dsp/cpu.c
${WEBPDECDIR}/src/dsp/cpu.h
${WEBPDECDIR}/src/dsp/dec.c
${WEBPDECDIR}/src/dsp/dec_clip_tables.c
${WEBPDECDIR}/src/dsp/dsp.h
${WEBPDECDIR}/src/dsp/filters.c
${WEBPDECDIR}/src/dsp/lossless.c
${WEBPDECDIR}/src/dsp/lossless.h
${WEBPDECDIR}/src/dsp/lossless_common.h
${WEBPDECDIR}/src/dsp/rescaler.c
${WEBPDECDIR}/src/dsp/upsampling.c
${WEBPDECDIR}/src/dsp/yuv.c
${WEBPDECDIR}/src/dsp/yuv.h

${WEBPDECDIR}/src/utils/bit_reader_utils.c
${WEBPDECDIR}/src/utils/bit_reader_utils.h
${WEBPDECDIR}/src/utils/bit_reader_inl_utils.h
${WEBPDECDIR}/src/utils/color_cache_utils.c
${WEBPDECDIR}/src/utils/color_cache_utils.h
${WEBPDECDIR}/src/utils/endian_inl_utils.h
${WEBPDECDIR}/src/utils/filters_utils.c
${WEBPDECDIR}/src/utils/filters_utils.h
${WEBPDECDIR}/src/utils/huffman_utils.c
${WEBPDECDIR}/src/utils/huffman_utils.h
${WEBPDECDIR}/src/utils/palette.c
${WEBPDECDIR}/src/utils/palette.h
${WEBPDECDIR}/src/utils/quant_levels_dec_utils.c
${WEBPDECDIR}/src/utils/quant_levels_dec_utils.h
${WEBPDECDIR}/src/utils/rescaler_utils.c
${WEBPDECDIR}/src/utils/rescaler_utils.h
${WEBPDECDIR}/src/utils/random_utils.c
${WEBPDECDIR}/src/utils/random_utils.h
${WEBPDECDIR}/src/utils/thread_utils.c
${WEBPDECDIR}/src/utils/thread_utils.h
${WEBPDECDIR}/src/utils/utils.c
${WEBPDECDIR}/src/utils/utils.h
${WEBPDECDIR}/src/utils/bounds_safety.h

${WEBPDECDIR}/src/webp/decode.h
${WEBPDECDIR}/src/webp/encode.h
${WEBPDECDIR}/src/webp/format_constants.h
${WEBPDECDIR}/src/webp/mux_types.h
${WEBPDECDIR}/src/webp/types.h
)

SOURCE_GROUP(gaussian_splats\\libwebp FILES ${webp_decode_sources})
