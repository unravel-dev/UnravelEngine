set(BUILD_THREADPP_TESTS OFF CACHE BOOL "Build the tests" FORCE)
set(BUILD_THREADPP_SHARED ${BUILD_SHARED_LIBS} CACHE BOOL "Build shared library" FORCE)

add_subdirectory( threadpp )

if(UNRAVEL_UNITY_BUILD)
    set_target_properties(threadpp PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_UNIQUE_ID "ANONYMOUS"
    )
endif()
