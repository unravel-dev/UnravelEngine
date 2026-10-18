# find_package(Freetype) answers with the FreeType built from deps/3rdparty/freetype, so no
# installed copy is looked for.
if(NOT TARGET Freetype::Freetype)
    message(FATAL_ERROR "deps/3rdparty/freetype must be added before find_package(Freetype).")
endif()

set(Freetype_FOUND TRUE)
set(FREETYPE_FOUND TRUE)
set(FREETYPE_LIBRARIES Freetype::Freetype)
