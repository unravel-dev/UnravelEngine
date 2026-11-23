#include "audit_catalog.h"
#include "audit_facts.h"
#include "audit_rules.h"

#include <deploy/shader_cook.h>
#include <engine/assets/impl/asset_extensions.h>
#include <graphics/graphics.h>
#include <hpp/uuid.hpp>
#include <logging/logging.h>

#include <map>
#include <unordered_set>

namespace unravel
{
auto audit::is_feature_allowed(const feature_limits& limits, limited_feature feature) -> bool
{
    switch(feature)
    {
        case limited_feature::global_illumination:
            return limits.global_illumination;
        case limited_feature::screen_space_reflections:
            return limits.screen_space_reflections;
        case limited_feature::screen_space_indirect_lighting:
            return limits.screen_space_indirect_lighting;
        case limited_feature::ambient_occlusion:
            return limits.ambient_occlusion;
        case limited_feature::bloom:
            return limits.bloom;
        case limited_feature::decals:
            return limits.decals;
        case limited_feature::post_process_materials:
            return limits.post_process_materials;
        case limited_feature::temporal_anti_aliasing:
            return limits.max_anti_aliasing == anti_aliasing_limit::temporal;
        case limited_feature::fxaa:
            return limits.max_anti_aliasing != anti_aliasing_limit::none;
    }
    return true;
}

auto audit::get_feature_setting_path(limited_feature feature) -> const char*
{
    switch(feature)
    {
        case limited_feature::global_illumination:
            return "graphics/limits/global_illumination";
        case limited_feature::screen_space_reflections:
            return "graphics/limits/screen_space_reflections";
        case limited_feature::screen_space_indirect_lighting:
            return "graphics/limits/screen_space_indirect_lighting";
        case limited_feature::ambient_occlusion:
            return "graphics/limits/ambient_occlusion";
        case limited_feature::bloom:
            return "graphics/limits/bloom";
        case limited_feature::decals:
            return "graphics/limits/decals";
        case limited_feature::post_process_materials:
            return "graphics/limits/post_process_materials";
        case limited_feature::temporal_anti_aliasing:
        case limited_feature::fxaa:
            return "graphics/limits/max_anti_aliasing";
    }
    return "graphics/limits";
}

auto audit::get_feature_off_effect(limited_feature feature, bool is_plural) -> std::string
{
    // The verb agrees with the count of components the sentence starts with.
    const char* has = is_plural ? "have" : "has";
    switch(feature)
    {
        case limited_feature::global_illumination:
            return fmt::format("{} no effect, because Graphics > Allowed Features > Global Illumination is off: scenes "
                               "light with their probes and sky only",
                               has);
        case limited_feature::screen_space_reflections:
            return fmt::format("{} no effect, because Graphics > Allowed Features > Screen Space Reflections is off", has);
        case limited_feature::screen_space_indirect_lighting:
            return fmt::format("{} no effect, because Graphics > Allowed Features > Screen Space Indirect Lighting is off", has);
        case limited_feature::ambient_occlusion:
            return fmt::format("{} no effect, because Graphics > Allowed Features > Ambient Occlusion is off", has);
        case limited_feature::bloom:
            return fmt::format("{} no effect, because Graphics > Allowed Features > Bloom is off", has);
        case limited_feature::decals:
            return fmt::format("{} nothing, because Graphics > Allowed Features > Decals is off", is_plural ? "draw" : "draws");
        case limited_feature::post_process_materials:
            return fmt::format("{} none of {} materials, because Graphics > Allowed Features > Post Process Materials is off",
                               is_plural ? "run" : "runs",
                               is_plural ? "their" : "its");
        case limited_feature::temporal_anti_aliasing:
            return fmt::format("{} FXAA or no anti-aliasing instead, because Graphics > Allowed Features > Max "
                               "Anti-Aliasing is below Temporal",
                               is_plural ? "get" : "gets");
        case limited_feature::fxaa:
            return fmt::format("{} no effect, because Graphics > Allowed Features > Max Anti-Aliasing is None", has);
    }
    return fmt::format("{} no effect", has);
}
} // namespace unravel

namespace unravel::audit::detail
{
namespace
{

/// How many entity paths an issue lists before it counts the rest.
constexpr std::size_t LISTED_ENTITIES = 10;

/// The document, or the entity of the document when there is one: its uid is the id, its path the label.
auto make_document_subject(const std::string& document, const std::string& entity_id, const std::string& entity_path)
    -> subject
{
    if(entity_id.empty())
    {
        return {subject_kind::asset, document, {}, {}, 0};
    }
    return {subject_kind::entity, document, entity_id, entity_path, 0};
}

void check_missing_references(rule_context& context, const document_scan& scan)
{
    for(const auto& missing : scan.missing)
    {
        const auto uid = hpp::to_string(missing.uid);
        auto item = context.make(rules::MISSING_ASSET,
                                 make_document_subject(missing.document, missing.entity_id, missing.entity),
                                 fmt::format("A {} field refers to the asset {}, which is not in the project.",
                                             missing.asset_type.empty() ? std::string("asset") : missing.asset_type,
                                             uid));
        item.discriminator = uid;
        context.add(std::move(item));
    }
}

void check_unreadable(rule_context& context, const document_scan& scan)
{
    for(const auto& problem : scan.unreadable)
    {
        context.add(context.make(rules::UNREADABLE_DOCUMENT,
                                 make_document_subject(problem.document, {}, {}),
                                 make_sentence(problem.message)));
    }
}

void check_controllers(rule_context& context, const document_scan& scan)
{
    for(const auto& [controller, problems] : scan.controllers)
    {
        if(problems.empty())
        {
            continue;
        }
        auto item = context.make(rules::INVALID_CONTROLLER,
                                 make_document_subject(controller, {}, {}),
                                 make_sentence(problems.front()));
        for(std::size_t index = 1; index < problems.size(); ++index)
        {
            item.details.push_back(make_sentence(problems[index]));
        }
        context.add(std::move(item));
    }
}

void check_joints(rule_context& context, const document_scan& scan)
{
    for(const auto& joint : scan.joints)
    {
        context.add(
            context.make(rules::JOINT_PROBLEM, make_document_subject(joint.document, joint.entity_id, joint.entity), make_sentence(joint.message)));
    }
}

auto list_entities(const std::vector<std::string>& entities) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    for(std::size_t index = 0; index < entities.size() && index < LISTED_ENTITIES; ++index)
    {
        lines.push_back("Entity " + entities[index]);
    }
    if(entities.size() > LISTED_ENTITIES)
    {
        lines.push_back(fmt::format("And {} more.", entities.size() - LISTED_ENTITIES));
    }
    return lines;
}

void check_features(rule_context& context, const document_scan& scan)
{
    for(const auto& use : scan.features)
    {
        for(const auto& profile : context.get_input().profiles)
        {
            if(is_feature_allowed(profile.resolved.graphics.limits, use.feature) || !is_shipped_by(profile, use.document))
            {
                continue;
            }
            auto item = context.make(rules::FEATURE_OFF,
                                     make_document_subject(use.document, {}, {}),
                                     fmt::format("{} {} {} {}.",
                                                 use.entities.size(),
                                                 use.component,
                                                 use.entities.size() == 1 ? "component" : "components",
                                                 get_feature_off_effect(use.feature, use.entities.size() != 1)));
            item.discriminator = use.component;
            item.details = list_entities(use.entities);
            item.profiles.push_back(profile.name);
            context.add(std::move(item));
        }
    }
}

void check_scene_color_reads(rule_context& context, const document_scan& scan)
{
    for(const auto& key : scan.scene_color_materials)
    {
        for(const auto& profile : context.get_input().profiles)
        {
            if(profile.resolved.graphics.limits.translucent_scene_color || !is_shipped_by(profile, key))
            {
                continue;
            }
            auto item = context.make(rules::SCENE_COLOR_OFF,
                                     make_document_subject(key, {}, {}),
                                     "The material reads the lit scene behind it, and Graphics > Allowed Features > "
                                     "Translucency Scene Color is off: it refracts nothing and reads the scene as black.");
            item.profiles.push_back(profile.name);
            context.add(std::move(item));
        }
    }
}

/// Clips that scripts name, which a script may play in any way.
auto find_script_named_assets(const input& in) -> std::unordered_set<std::string>
{
    std::unordered_set<std::string> named;
    for(const auto& reference : in.references)
    {
        if(ex::is_format<script>(fs::path(reference.from).extension().generic_string()))
        {
            named.insert(reference.to);
        }
    }
    return named;
}

/// Whether a profile's builds carry WebGL 2 shaders: WebGL 2 maps clip depth through -1..1, which keeps less precision
/// than the other backends (desktop OpenGL clips depth 0..1 where the driver has clip control).
auto ships_webgl2(const profile_record& profile) -> bool
{
    const auto renderers = deployment::get_shader_variants(profile.deploy);
    return renderers.count(gfx::get_renderer_filename_extension(bgfx::RendererType::OpenGLES)) != 0;
}

void check_camera_depth_ranges(rule_context& context, const document_scan& scan)
{
    for(const auto& range : scan.cameras)
    {
        if(range.near_clip <= 0.0f)
        {
            continue;
        }
        const double ratio = double(range.far_clip) / double(range.near_clip);
        for(const auto& profile : context.get_input().profiles)
        {
            const auto limit = context.get_threshold(rules::CAMERA_DEPTH_RANGE, thresholds::MAX_FAR_TO_NEAR, profile.platform);
            if(limit <= 0 || ratio <= double(limit) || !is_shipped_by(profile, range.document) || !ships_webgl2(profile))
            {
                continue;
            }
            auto item = context.make(rules::CAMERA_DEPTH_RANGE,
                                     make_document_subject(range.document, range.entity_id, range.entity_path),
                                     fmt::format("The camera's far clip of {:g} m is {:.0f} times its near clip of {:g} m. "
                                                 "On WebGL 2 distant surfaces can flicker through each other.",
                                                 range.far_clip,
                                                 ratio,
                                                 range.near_clip));
            item.discriminator = range.entity_id;
            item.profiles.push_back(profile.name);
            context.add(std::move(item));
        }
    }
}

void check_stereo_spatial(rule_context& context, const document_scan& scan)
{
    const auto& in = context.get_input();
    std::map<std::string, std::pair<std::size_t, bool>> uses_by_clip;
    for(const auto& use : scan.audio_sources)
    {
        auto& entry = uses_by_clip[use.clip];
        entry.first++;
        entry.second = entry.second || !use.is_spatial;
    }
    const auto named_in_scripts = find_script_named_assets(in);
    for(const auto& asset : in.assets)
    {
        const auto found = uses_by_clip.find(asset.key);
        if(found == uses_by_clip.end() || found->second.second || named_in_scripts.count(asset.key) > 0)
        {
            continue;
        }
        if(!asset.facts.audio || asset.facts.audio->channels != 2)
        {
            continue;
        }
        auto item = context.make(rules::STEREO_SPATIAL,
                                 {subject_kind::asset, asset.key, {}, {}, 0},
                                 fmt::format("{} spatial audio source(s) play it and nothing else does: as mono it would "
                                             "take {} instead of {} decoded.",
                                             found->second.first,
                                             format_bytes(asset.facts.audio->decoded_bytes / 2),
                                             format_bytes(asset.facts.audio->decoded_bytes)));
        item.fix = fixes::FORCE_MONO;
        context.add(std::move(item));
    }
}

} // namespace

void check_scenes(rule_context& context)
{
    const auto& scan = context.get_input().scan;
    if(!scan.ran)
    {
        return;
    }
    check_missing_references(context, scan);
    check_unreadable(context, scan);
    check_controllers(context, scan);
    check_joints(context, scan);
    check_features(context, scan);
    check_scene_color_reads(context, scan);
    check_stereo_spatial(context, scan);
    check_camera_depth_ranges(context, scan);
}

} // namespace unravel::audit::detail
