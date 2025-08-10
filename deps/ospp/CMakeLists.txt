set(BUILD_OSPP_TESTS OFF CACHE BOOL "Build the tests" FORCE)
set(BUILD_OSPP_SHARED ${BUILD_SHARED_LIBS} CACHE BOOL "Build shared library" FORCE)
set(OSPP_BACKEND_TYPE "SDL" CACHE STRING "Backend type chosen by the user at CMake configure time" FORCE)

add_subdirectory( ospp )


if(ACE_UNITY_BUILD)
    set_target_properties(ospp PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_UNIQUE_ID "ANONYMOUS"
    )
endif()
