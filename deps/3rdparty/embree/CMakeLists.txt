# DISABLED: Embree is not part of the build, and its submodule (deps/3rdparty/embree/embree) is not checked in.
# engine/core/raytracing builds its native backend alone and compiles its Embree backend only when the embree
# target below exists (RAYTRACING_WITH_EMBREE). To bring Embree back:
#   git submodule add https://github.com/RenderKit/embree.git deps/3rdparty/embree/embree
#   git -C deps/3rdparty/embree/embree checkout v4.4.1
# then uncomment everything below and re-run CMake.
#
# # Embree, the CPU ray tracing kernels behind the offline GI bakes (mesh distance field sign, surface cache cards).
# #
# # Built as a project of its own rather than with add_subdirectory: Embree names its internal targets math, sys,
# # simd, lexers and tasking, and the engine already has a target called math.
# include(ExternalProject)
#
# set(EMBREE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/embree")
# set(EMBREE_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/embree")
# # Each Embree source compiles its kernels for every ISA at -O3 and needs a couple of GB; 32 at once ran out of memory.
# set(EMBREE_BUILD_JOBS 8 CACHE STRING "Parallel compile jobs for the Embree build")
#
# get_property(EMBREE_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
# if(EMBREE_IS_MULTI_CONFIG)
#     set(EMBREE_CONFIG "$<CONFIG>")
#     set(EMBREE_LIBRARY_DIR "${EMBREE_BINARY_DIR}/$<CONFIG>")
# else()
#     set(EMBREE_CONFIG "${CMAKE_BUILD_TYPE}")
#     set(EMBREE_LIBRARY_DIR "${EMBREE_BINARY_DIR}")
# endif()
#
# if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|x64|X64)$")
#     # SSE2 everywhere, AVX2 where the CPU has it. AVX has to come with AVX2: Embree compiles its 8-wide BVH core
#     # only into the AVX kernels.
#     set(EMBREE_ISA_ARGS
#         -DEMBREE_MAX_ISA=NONE
#         -DEMBREE_ISA_SSE2=ON
#         -DEMBREE_ISA_SSE42=OFF
#         -DEMBREE_ISA_AVX=ON
#         -DEMBREE_ISA_AVX2=ON
#         -DEMBREE_ISA_AVX512=OFF)
#     set(EMBREE_ISA_LIBRARIES embree_avx2 embree_avx)
# else()
#     # ARM: Embree's own NEON defaults.
#     set(EMBREE_ISA_ARGS)
#     set(EMBREE_ISA_LIBRARIES)
# endif()
#
# set(EMBREE_TOOLCHAIN_ARGS
#     -DCMAKE_BUILD_TYPE=${EMBREE_CONFIG}
#     -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
#     -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
#     -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
#     -DCMAKE_POSITION_INDEPENDENT_CODE=ON
#     # Embree asks for -std=c++11, which the current Microsoft STL headers refuse; the target standard flag comes later
#     # on the command line and wins.
#     -DCMAKE_CXX_STANDARD=20
#     # Embree's compiler setup rewrites the per-configuration flags, which is where its CMake 3.10 policies put the
#     # runtime library selection (-D_DLL ...). Selected the newer way, the runtime matches the engine's.
#     -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
#     # Third-party code builds without warnings, as the rest of deps/3rdparty does.
#     "-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS} -w"
#     "-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS} -w")
# foreach(variable CMAKE_TOOLCHAIN_FILE CMAKE_LINKER CMAKE_RC_COMPILER CMAKE_AR CMAKE_RANLIB CMAKE_MSVC_RUNTIME_LIBRARY
#                  CMAKE_OSX_ARCHITECTURES CMAKE_OSX_DEPLOYMENT_TARGET CMAKE_OSX_SYSROOT CMAKE_CXX_COMPILER_LAUNCHER
#                  CMAKE_C_COMPILER_LAUNCHER)
#     if(${variable})
#         list(APPEND EMBREE_TOOLCHAIN_ARGS "-D${variable}=${${variable}}")
#     endif()
# endforeach()
#
# set(EMBREE_LIBRARIES embree4 ${EMBREE_ISA_LIBRARIES} sys math simd lexers tasking)
# set(EMBREE_LIBRARY_FILES)
# foreach(library ${EMBREE_LIBRARIES})
#     list(APPEND EMBREE_LIBRARY_FILES
#          "${EMBREE_LIBRARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}${library}${CMAKE_STATIC_LIBRARY_SUFFIX}")
# endforeach()
#
# ExternalProject_Add(embree_external
#     SOURCE_DIR "${EMBREE_SOURCE_DIR}"
#     BINARY_DIR "${EMBREE_BINARY_DIR}"
#     CMAKE_ARGS
#         ${EMBREE_TOOLCHAIN_ARGS}
#         # clang 21 accepts -fsycl, which Embree takes for Intel DPC++. Settled up front, it picks its GNU-style
#         # flags for clang++ and its MSVC ones for cl.
#         -DCOMPILER_HAS_SYCL_SUPPORT=OFF
#         -DEMBREE_STATIC_LIB=ON
#         -DEMBREE_TASKING_SYSTEM=INTERNAL
#         -DEMBREE_ISPC_SUPPORT=OFF
#         -DEMBREE_SYCL_SUPPORT=OFF
#         -DEMBREE_TUTORIALS=OFF
#         -DEMBREE_ZIP_MODE=OFF
#         -DEMBREE_INSTALL_DEPENDENCIES=OFF
#         -DEMBREE_STAT_COUNTERS=OFF
#         -DEMBREE_GEOMETRY_TRIANGLE=ON
#         -DEMBREE_GEOMETRY_QUAD=OFF
#         -DEMBREE_GEOMETRY_CURVE=OFF
#         -DEMBREE_GEOMETRY_SUBDIVISION=OFF
#         -DEMBREE_GEOMETRY_USER=OFF
#         -DEMBREE_GEOMETRY_INSTANCE=OFF
#         -DEMBREE_GEOMETRY_INSTANCE_ARRAY=OFF
#         -DEMBREE_GEOMETRY_GRID=OFF
#         -DEMBREE_GEOMETRY_POINT=OFF
#         ${EMBREE_ISA_ARGS}
#     BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config ${EMBREE_CONFIG} --parallel ${EMBREE_BUILD_JOBS}
#     # The outer build cannot see Embree's sources, so a submodule update would otherwise never rebuild it. Embree's
#     # own build decides what is out of date; when nothing is, it leaves the libraries untouched and nothing relinks.
#     BUILD_ALWAYS ON
#     INSTALL_COMMAND ""
#     BUILD_BYPRODUCTS ${EMBREE_LIBRARY_FILES}
#     LOG_CONFIGURE ON
#     LOG_BUILD ON
#     LOG_OUTPUT_ON_FAILURE ON)
#
# # What a consumer links. It must also add_dependencies() on embree_external: the headers include a configuration
# # header that Embree's own configure step writes.
# add_library(embree INTERFACE)
# target_include_directories(embree SYSTEM INTERFACE "${EMBREE_SOURCE_DIR}/include")
# if(UNIX AND NOT APPLE)
#     # Embree's static libraries reference each other in both directions.
#     target_link_libraries(embree INTERFACE -Wl,--start-group ${EMBREE_LIBRARY_FILES} -Wl,--end-group)
# else()
#     target_link_libraries(embree INTERFACE ${EMBREE_LIBRARY_FILES})
# endif()
# find_package(Threads REQUIRED)
# target_link_libraries(embree INTERFACE Threads::Threads ${CMAKE_DL_LIBS})
