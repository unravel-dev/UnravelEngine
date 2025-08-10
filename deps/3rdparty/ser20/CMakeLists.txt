set( THREAD_SAFE ON CACHE BOOL "Use mutexes to ensure thread safety" FORCE)

add_subdirectory( ser20 )


if(ACE_UNITY_BUILD)
    set_target_properties(ser20 PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_UNIQUE_ID "ANONYMOUS"
    )
endif()
