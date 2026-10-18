# FreeType for RmlUi's font engine, built from source on every platform. Optional dependencies
# stay off: compressed fonts use the zlib bundled with FreeType, and no system zlib, bzip2,
# libpng, HarfBuzz or Brotli is looked for.
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON)
set(DISABLE_FORCE_DEBUG_POSTFIX ON)

add_subdirectory(freetype)

set_target_properties(freetype PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The name CMake's FindFreetype gives the library; FreeType itself only exports it under that name.
add_library(Freetype::Freetype ALIAS freetype)
