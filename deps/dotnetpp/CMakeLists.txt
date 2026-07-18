set(BUILD_DOTNETPP_SHARED ${BUILD_SHARED_LIBS} CACHE BOOL "Build shared library" FORCE)

set(DOTNETPP_BACKEND "coreclr" CACHE STRING "dotnetpp runtime backend (mono|coreclr)" FORCE)

add_subdirectory( dotnetpp )

if(UNRAVEL_UNITY_BUILD)
    if(DOTNETPP_BACKEND STREQUAL "mono")
        set_target_properties(monopp PROPERTIES
            UNITY_BUILD ON
            UNITY_BUILD_UNIQUE_ID "ANONYMOUS"
        )
    elseif(DOTNETPP_BACKEND STREQUAL "coreclr")
        set_target_properties(clrpp PROPERTIES
            UNITY_BUILD ON
            UNITY_BUILD_UNIQUE_ID "ANONYMOUS"
        )
    endif()
endif()
