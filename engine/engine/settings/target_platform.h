#pragma once

#include <cstdint>

namespace unravel
{

/**
 * @brief The platform a build runs on: what a deploy produces and what per-platform asset settings
 *        are keyed by.
 */
enum class target_platform : std::uint8_t
{
    /// This computer's platform: the player executable beside the data.
    desktop,
    /// An Android package (.apk) built from the editor's Android player.
    android,
    /// A folder a web server serves: the editor's browser player (WebGPU or WebGL 2, .NET in the browser) beside
    /// the game's files.
    web,
};

} // namespace unravel
