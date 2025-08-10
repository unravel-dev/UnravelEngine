set(target_name seq)

file(GLOB_RECURSE SOURCES "*.cpp" "*.h" "*.hpp")

set(TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests")
file(GLOB_RECURSE TESTS_SOURCES "${TESTS_DIR}/*.c"
                                "${TESTS_DIR}/*.cpp"
                                "${TESTS_DIR}/*.h"
                                "${TESTS_DIR}/*.hpp")

list(REMOVE_ITEM SOURCES ${TESTS_SOURCES})

add_library(${target_name} ${SOURCES})

target_include_directories(${target_name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

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

target_link_libraries(${target_name} hpp)

target_include_directories(${target_name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)


###############################################################################################

set(target_name seq_tests)
add_library(${target_name} EXCLUDE_FROM_ALL ${TESTS_SOURCES})

# Add definitions
set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_link_libraries(${target_name} PUBLIC suitepp)
target_link_libraries(${target_name} PUBLIC seq)
target_include_directories(${target_name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
