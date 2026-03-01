set(BUILD_MONOPP_SHARED ${BUILD_SHARED_LIBS} CACHE BOOL "Build shared library" FORCE)
set(BUILD_MONOPP_MONORT ON CACHE BOOL "Build the monort utility library" FORCE)
set(BUILD_MONOPP_MONORT_MANAGED OFF CACHE BOOL "Build the monort managed utility library" FORCE)
add_subdirectory( monopp )

if(UNRAVEL_UNITY_BUILD)
    set_target_properties(monopp PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_UNIQUE_ID "ANONYMOUS"
    )
endif()
