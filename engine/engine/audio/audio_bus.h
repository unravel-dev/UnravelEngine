#pragma once
#include <cstdint>

namespace unravel
{

/**
 * @brief Mixer bus used by audio sources. Master gain is applied on top of these.
 */
enum class audio_bus : std::uint8_t
{
    sfx = 0,
    music = 1,
    ui = 2,
};

inline constexpr std::uint8_t audio_bus_count = 3;

} // namespace unravel
