set(BUILD_AUDIOPP_TESTS OFF CACHE BOOL "Build the tests" FORCE)
set(BUILD_AUDIOPP_SHARED ${BUILD_SHARED_LIBS} CACHE BOOL "Build shared library" FORCE)
set(AUDIOPP_DEFAULT_BACKEND "miniaudio" CACHE STRING "The backend a device opens with unless it asks for another: openal or miniaudio." FORCE)
# OpenAL Soft is under the LGPL, so the engine plays through miniaudio alone and neither builds nor links it.
set(AUDIOPP_WITH_OPENAL OFF CACHE BOOL "Build the OpenAL Soft backend." FORCE)

# Hide all warnings for 3rdparty code
if (MSVC)
    add_compile_options(/W0)
else()
    add_compile_options(-w)
endif()

add_subdirectory( audiopp )