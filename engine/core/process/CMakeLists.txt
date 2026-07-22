set(target_name process)

set(process_sources
    process.h
    startup_arguments.h
    startup_arguments.cpp
    command_line.cpp
)

if(WIN32)
    list(APPEND process_sources process_windows.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND process_sources process_linux.cpp)
else()
    list(APPEND process_sources process_fallback.cpp)
endif()

add_library(${target_name} ${process_sources})

target_include_directories(${target_name}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(${target_name} PUBLIC base)

set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)