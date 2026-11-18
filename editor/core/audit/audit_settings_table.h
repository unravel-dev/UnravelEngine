#pragma once

#include <engine/settings/settings.h>

#include <cstdint>
#include <string>
#include <vector>

namespace unravel::audit
{

/// A setting whose value decides a rendering or memory cost, as the settings rules compare it.
struct performance_setting
{
    /// Its path in the settings document ("graphics/limits/global_illumination").
    const char* path;
    /// Its place in Project Settings ("Graphics > Allowed Features > Global Illumination").
    const char* label;
    /// Why a phone or browser profile starts lower, in a sentence.
    const char* reason;
    /// The cost of a value: a higher number is more expensive.
    std::int64_t (*cost)(const settings& value);
    /// The value as Project Settings shows it ("on", "Temporal", "4096").
    std::string (*describe)(const settings& value);
    /// Copies the setting from `from` into `to`, leaving everything else.
    void (*copy)(settings& to, const settings& from);
};

/// Every setting the settings rules compare with a platform's starting values.
auto get_performance_settings() -> const std::vector<performance_setting>&;

/// The setting at a path, or null.
auto find_performance_setting(const std::string& path) -> const performance_setting*;

} // namespace unravel::audit
