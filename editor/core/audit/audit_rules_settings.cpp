#include "audit_catalog.h"
#include "audit_rules.h"
#include "audit_settings_table.h"

#include <logging/logging.h>

#include <cmath>

namespace unravel::audit::detail
{
namespace
{

auto make_setting_subject(const char* path) -> subject
{
    return {subject_kind::setting, path, {}, {}, 0};
}

void check_costlier_than_start(rule_context& context, const profile_record& profile)
{
    if(profile.platform == target_platform::desktop)
    {
        // A desktop profile starts with the project's own settings.
        return;
    }
    for(const auto& setting : get_performance_settings())
    {
        const auto current = setting.cost(profile.resolved);
        const auto start = setting.cost(profile.start);
        if(current <= start)
        {
            continue;
        }
        auto item = context.make(rules::COSTLIER_THAN_START,
                                 make_setting_subject(setting.path),
                                 fmt::format("{} is {}, where a new profile of its platform starts with {}. {}",
                                             setting.label,
                                             setting.describe(profile.resolved),
                                             setting.describe(profile.start),
                                             setting.reason));
        // Phones carry the measured costs; a browser build may run on a computer as well.
        item.level = profile.platform == target_platform::android ? severity::warning : severity::info;
        item.profiles.push_back(profile.name);
        item.fix = fixes::USE_PLATFORM_START;
        context.add(std::move(item));
    }
}

void check_default_compression(rule_context& context, const profile_record& profile)
{
    if(profile.resolved.assets.texture.default_compression == texture_importer_meta::compression_quality::none)
    {
        auto item = context.make(rules::TEXTURE_DEFAULT_UNCOMPRESSED,
                                 make_setting_subject("assets/texture/default_compression"),
                                 "Assets > Textures > Default Compression is None.");
        item.profiles.push_back(profile.name);
        context.add(std::move(item));
    }
    if(profile.resolved.assets.audio.default_compression == audio_importer_meta::compression_quality::none)
    {
        auto item = context.make(rules::AUDIO_DEFAULT_UNCOMPRESSED,
                                 make_setting_subject("assets/audio/default_compression"),
                                 "Assets > Audio > Default Compression is None.");
        item.profiles.push_back(profile.name);
        context.add(std::move(item));
    }
}

void check_physics_rate(rule_context& context, const profile_record& profile)
{
    const float step = profile.resolved.physics.fixed_timestep;
    if(step <= 0.0f)
    {
        return;
    }
    const double rate = 1.0 / double(step);
    const auto limit = context.get_threshold(rules::PHYSICS_RATE, thresholds::MAX_RATE, profile.platform);
    // A rate a rounding error above the limit (1 / 0.008333) is the limit.
    if(limit <= 0 || std::lround(rate) <= limit)
    {
        return;
    }
    auto item = context.make(rules::PHYSICS_RATE,
                             make_setting_subject("physics/fixed_timestep"),
                             fmt::format("Physics > Fixed Timestep is {:g} s: the physics steps {:.0f} times a second, over "
                                         "the {}-step guideline.",
                                         step,
                                         rate,
                                         limit));
    item.profiles.push_back(profile.name);
    context.add(std::move(item));
}

} // namespace

void check_settings(rule_context& context)
{
    for(const auto& profile : context.get_input().profiles)
    {
        check_costlier_than_start(context, profile);
        check_default_compression(context, profile);
        check_physics_rate(context, profile);
    }
}

} // namespace unravel::audit::detail
