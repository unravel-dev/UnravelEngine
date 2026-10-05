#pragma once

#include <hpp/optional.hpp>
#include <hpp/string_view.hpp>

#include <cstdint>

namespace raytracing
{

/**
 * @brief The ray tracer behind a @ref triangle_scene.
 *
 * Every backend answers the same queries with the same meaning; they differ in speed and in how they round.
 */
enum class backend : uint8_t
{
    ///< Intel Embree: wide SIMD BVHs and ray packets. Only in a build with deps/3rdparty/embree
    ///< (@ref is_backend_available).
    embree,
    ///< The engine's own 4-wide BVH on the math library.
    native,
};

/// Whether this build has @p value: the native backend always, Embree only when its dependency is part of the build.
auto is_backend_available(backend value) -> bool;

/// The backend a scene is built with when its caller names none: native, until @ref set_default_backend changes it.
auto get_default_backend() -> backend;

/// Sets the backend later scene builds default to, unless this build lacks it. Scenes already built keep theirs.
/// @return false, changing nothing, when @p value is not available.
auto set_default_backend(backend value) -> bool;

/// The backend's name: "embree" or "native".
auto to_string(backend value) -> hpp::string_view;

/// The backend @ref to_string names @p name, if any, whether or not this build has it.
auto parse_backend(hpp::string_view name) -> hpp::optional<backend>;

} // namespace raytracing
