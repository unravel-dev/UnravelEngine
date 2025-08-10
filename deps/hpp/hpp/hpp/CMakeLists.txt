set(target_name hpp)

add_library(${target_name} INTERFACE)
target_sources(${target_name} INTERFACE)

target_include_directories(${target_name} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)

set (target_for_ide "${target_name}_ide")
if (NOT TARGET ${target_for_ide})
    file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
    add_custom_target(${target_for_ide} SOURCES ${SOURCES})
endif()

set_target_properties(${target_name}_ide PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_compile_definitions(${target_name} INTERFACE
    #STX_NO_STD_FILESYSTEM
    #STX_NO_STD_VARIANT
    #STX_NO_STD_OPTIONAL
    #STX_NO_STD_STRING_VIEW
    #STX_NO_STD_SPAN
    #STX_NO_STD_SOURCE_LOCATION
)
