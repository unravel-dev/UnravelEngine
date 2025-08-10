message(STATUS "Enabled tests.")

set(target_name hpp_test)

file(GLOB_RECURSE libsrc *.h *.cpp *.hpp *.c *.cc)

add_executable(${target_name} ${libsrc})

target_link_libraries(${target_name} PUBLIC hpp)

set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

enable_testing()
add_test(NAME ${target_name} COMMAND ${target_name})
