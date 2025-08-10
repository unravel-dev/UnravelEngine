file(GLOB_RECURSE libsrc *.h *.c *.cc)

set(target_name libunibreak)

add_library (${target_name} ${libsrc})

target_include_directories(${target_name}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(${target_name} PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    LINKER_LANGUAGE C
)
