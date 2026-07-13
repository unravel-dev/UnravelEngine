set(BUILD_MONOPP_SHARED ${BUILD_SHARED_LIBS} CACHE BOOL "Build shared library" FORCE)

set(DOTNETPP_BACKEND "coreclr" CACHE STRING "dotnetpp runtime backend (mono|coreclr)" FORCE)

add_subdirectory( monopp )

if(UNRAVEL_UNITY_BUILD)
    set_target_properties(monopp PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_UNIQUE_ID "ANONYMOUS"
    )
endif()
