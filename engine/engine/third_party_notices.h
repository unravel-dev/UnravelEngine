#pragma once
#include <engine/engine_export.h>

#include <string>

namespace unravel
{

/**
 * @brief The libraries a binary may be built with or without, which the notices name only when it links them.
 */
struct linked_libraries
{
    /// OpenAL Soft, under the LGPL, and Oboe, which it plays through on Android.
    bool has_openal_soft{};
    /// libdwarf, under the LGPL, when cpptrace is set to read symbols with it.
    bool has_libdwarf{};
    /// cpptrace, which walks the stacks of the crash logs; the browser build has no crash handlers.
    bool has_cpptrace{};
    bool has_miniaudio{};
    /// What Emscripten links into the browser player: Dawn's WebGPU, its own runtime and musl.
    bool has_web_runtime{};
};

/**
 * @brief The libraries this binary links.
 */
ENGINE_EXPORT auto get_linked_libraries() -> linked_libraries;

/**
 * @brief The license notices of everything a game built with these libraries ships with, from
 * THIRD_PARTY_NOTICES.txt.
 *
 * The text is compiled into the engine, so it is there on every platform. It names the version of
 * the engine this binary was built from. The part on the LGPL is there only with OpenAL Soft or libdwarf.
 *
 * @param libraries The libraries whose notices the text keeps.
 * @return The notices as ASCII text with line feeds.
 */
ENGINE_EXPORT auto get_third_party_notices(const linked_libraries& libraries) -> std::string;

/**
 * @brief The license notices of this binary: get_third_party_notices of get_linked_libraries.
 */
ENGINE_EXPORT auto get_third_party_notices() -> std::string;

} // namespace unravel
