# The Ogg Vorbis encoder (libvorbis 1.3.7 with libogg 1.3.6) for the asset compiler, which compresses lossless audio
# sources at import. Built from the sources here into one static library, so neither project's own build and install
# rules reach the editor packages. Games decode Vorbis with audiopp's stb_vorbis and do not link this.

set(OGG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/ogg")
set(VORBIS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vorbis")

# libogg's configure step: its integer types come from stdint.h.
set(INCLUDE_INTTYPES_H 0)
set(INCLUDE_STDINT_H 1)
set(INCLUDE_SYS_TYPES_H 0)
set(SIZE16 int16_t)
set(USIZE16 uint16_t)
set(SIZE32 int32_t)
set(USIZE32 uint32_t)
set(SIZE64 int64_t)
set(USIZE64 uint64_t)
set(generated_include_dir "${CMAKE_CURRENT_BINARY_DIR}/include")
configure_file("${OGG_ROOT}/include/ogg/config_types.h.in" "${generated_include_dir}/ogg/config_types.h" @ONLY)

# libvorbis's library sources (lib/CMakeLists.txt) and its encoder; vorbisfile and the tools are not needed.
add_library(vorbisenc STATIC
    "${OGG_ROOT}/src/bitwise.c"
    "${OGG_ROOT}/src/framing.c"
    "${VORBIS_ROOT}/lib/analysis.c"
    "${VORBIS_ROOT}/lib/bitrate.c"
    "${VORBIS_ROOT}/lib/block.c"
    "${VORBIS_ROOT}/lib/codebook.c"
    "${VORBIS_ROOT}/lib/envelope.c"
    "${VORBIS_ROOT}/lib/floor0.c"
    "${VORBIS_ROOT}/lib/floor1.c"
    "${VORBIS_ROOT}/lib/info.c"
    "${VORBIS_ROOT}/lib/lookup.c"
    "${VORBIS_ROOT}/lib/lpc.c"
    "${VORBIS_ROOT}/lib/lsp.c"
    "${VORBIS_ROOT}/lib/mapping0.c"
    "${VORBIS_ROOT}/lib/mdct.c"
    "${VORBIS_ROOT}/lib/psy.c"
    "${VORBIS_ROOT}/lib/registry.c"
    "${VORBIS_ROOT}/lib/res0.c"
    "${VORBIS_ROOT}/lib/sharedbook.c"
    "${VORBIS_ROOT}/lib/smallft.c"
    "${VORBIS_ROOT}/lib/synthesis.c"
    "${VORBIS_ROOT}/lib/vorbisenc.c"
    "${VORBIS_ROOT}/lib/window.c"
)
target_include_directories(vorbisenc
    PUBLIC
        "${OGG_ROOT}/include"
        "${generated_include_dir}"
        "${VORBIS_ROOT}/include"
    PRIVATE
        "${VORBIS_ROOT}/lib"
)
if(UNIX)
    target_link_libraries(vorbisenc PRIVATE m)
endif()
set_target_properties(vorbisenc PROPERTIES POSITION_INDEPENDENT_CODE ON)
