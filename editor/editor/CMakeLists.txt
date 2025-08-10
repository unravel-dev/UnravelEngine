file(GLOB_RECURSE libsrc *.h *.cpp *.hpp *.c *.cc)

set(target_name editor)

if(WIN32 AND MSVC)
    list(APPEND libsrc "${CMAKE_CURRENT_SOURCE_DIR}/../../rc/UnravelEngine.rc")
endif()

add_executable(${target_name} ${libsrc})

target_include_directories(${target_name}
    PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_dependencies(${target_name} engine)
target_link_libraries(${target_name} PUBLIC engine imgui filedialog)

set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)


if(UNRAVEL_UNITY_BUILD)
    set_target_properties(${target_name} PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_UNIQUE_ID "ANONYMOUS"
    )
endif()

if (CMAKE_C_COMPILER_ID MATCHES GNU|Clang AND NOT WIN32)
  target_link_options(${target_name} PRIVATE -no-pie)
endif()

