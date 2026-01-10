set(BUILD_LIB OFF CACHE BOOL "Create targets for the watcher-c libraries" FORCE)
set(BUILD_BIN OFF CACHE BOOL "Create targets for the CLI binaries" FORCE)
set(BUILD_HDR ON CACHE BOOL "Create targets for the headers (both the C++ single-header library and the watcher-c library header)" FORCE)
set(BUILD_PKG_CONFIG OFF CACHE BOOL "Create targets for the pkg-config files" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Create targets for the test programs" FORCE)



add_subdirectory(watcher)

target_include_directories("wtr.hdr_watcher"
    INTERFACE
		${CMAKE_CURRENT_SOURCE_DIR}/watcher/include
)
