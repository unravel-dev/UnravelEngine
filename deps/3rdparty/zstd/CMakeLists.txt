cmake_minimum_required(VERSION 3.14)

# Only the library is needed: the compressor and decompressor, without zstd's own build (programs,
# tests, legacy formats, dictionary builder).
set(ZSTD_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/zstd/lib)

file(GLOB ZSTD_SOURCES
    ${ZSTD_LIB_DIR}/common/*.c
    ${ZSTD_LIB_DIR}/compress/*.c
    ${ZSTD_LIB_DIR}/decompress/*.c)

add_library(zstd STATIC ${ZSTD_SOURCES})

target_include_directories(zstd PUBLIC ${ZSTD_LIB_DIR})

# ZSTD_DISABLE_ASM: the x86-64 Huffman decoder is GNU assembly built only for ELF targets; one
# portable C build serves every platform. XXH_NAMESPACE keeps zstd's bundled xxHash symbols apart
# from other copies linked into the same binary.
target_compile_definitions(zstd PRIVATE ZSTD_DISABLE_ASM XXH_NAMESPACE=ZSTD_ ZSTD_LEGACY_SUPPORT=0)

set_target_properties(zstd PROPERTIES
    C_STANDARD 99
    POSITION_INDEPENDENT_CODE ON
)
