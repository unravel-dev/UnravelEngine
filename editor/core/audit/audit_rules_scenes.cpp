#include "audit_catalog.h"
#include "audit_facts.h"
#include "audit_rules.h"

#include <deploy/shader_cook.h>
#include <engine/assets/impl/asset_extensions.h>
#include <graphics/graphics.h>
#include <hpp/uuid.hpp>
#include <logging/logging.h>

#include <cmath>
#include <limits>
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
        case limited_feature::collision_decals:
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
        case limited_feature::collision_decals:
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
        case limited_feature::collision_decals:
            return fmt::format("{} no decals where {} particles land, because Graphics > Allowed Features > Decals is off",
                               is_plural ? "leave" : "leaves",
                               is_plural ? "their" : "its");
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
/// The frame rate a profile's collision cast limit is weighed at.
constexpr double CAST_LIMIT_FRAME_RATE = 60.0;

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

void check_emitter_materials(rule_context& context, const document_scan& scan)
{
    for(const auto& emitter : scan.emitter_materials)
    {
        context.add(context.make(rules::EMITTER_MATERIAL,
                                 make_document_subject(emitter.document, emitter.entity_id, emitter.entity),
                                 fmt::format("The Particle Emitter's Material {} cannot draw particles, so they draw the "
                                             "engine's plain particle material.",
                                             emitter.message)));
    }
}

void check_local_space_collisions(rule_context& context, const document_scan& scan)
{
    for(const auto& emitter : scan.local_space_collisions)
    {
        context.add(context.make(rules::LOCAL_SPACE_COLLISION,
                                 make_document_subject(emitter.document, emitter.entity_id, emitter.entity),
                                 "The Particle Emitter's Collision does nothing, because its particles simulate in Local "
                                 "space."));
    }
}

void check_collision_casts(rule_context& context, const document_scan& scan)
{
    for(const auto& use : scan.collider_emitters)
    {
        const auto casts_per_frame = std::uint64_t(std::ceil(use.casts_per_second / CAST_LIMIT_FRAME_RATE));
        for(const auto& profile : context.get_input().profiles)
        {
            const std::uint32_t limit = profile.resolved.graphics.particles.max_collision_casts;
            if(casts_per_frame <= limit || !is_shipped_by(profile, use.document))
            {
                continue;
            }
            const bool is_one = use.entities.size() == 1;
            auto item = context.make(rules::COLLISION_CASTS,
                                     make_document_subject(use.document, {}, {}),
                                     fmt::format("{} Particle {} with physics colliders {} about {} casts a frame at 60 "
                                                 "frames a second, and Graphics > Particles > Max Collision Casts allows "
                                                 "{}. Contacts past the limit come late.",
                                                 use.entities.size(),
                                                 is_one ? "Emitter that collides" : "Emitters that collide",
                                                 is_one ? "needs" : "need",
                                                 casts_per_frame,
                                                 limit));
            item.details = list_entities(use.entities);
            item.profiles.push_back(profile.name);
            context.add(std::move(item));
        }
    }
}

/// Whether a profile's builds carry WebGL 2 shaders: WebGL 2 maps clip depth through -1..1, which keeps less precision
/// than the other backends (desktop OpenGL clips depth 0..1 where the driver has clip control).
auto ships_webgl2(const profile_record& profile) -> bool
{
    const auto renderers = deployment::get_shader_variants(profile.deploy);
    return renderers.count(gfx::get_renderer_filename_extension(bgfx::RendererType::OpenGLES)) != 0;
}

void check_unreported_scene_contacts(rule_context& context, const document_scan& scan)
{
    for(const auto& emitter : scan.unreported_scene_contacts)
    {
        context.add(context.make(rules::SCENE_CONTACT_UNREPORTED,
                                 make_document_subject(emitter.document, emitter.entity_id, emitter.entity),
                                 emitter.message));
    }
}

/// Why a profile's builds do not collide particles with scene surfaces and where that shows, as the end of the
/// issue's message from "and" on; empty when they collide.
auto get_scene_collision_gap(const profile_record& profile) -> std::string
{
    if(profile.resolved.graphics.particles.scene_collision == ps_soa::scene_collision::off)
    {
        return "and the profile's Graphics > Particles > Scene Collision is Off. In its builds the particles pass "
               "through every surface.";
    }
    if(ships_webgl2(profile))
    {
        return "and the profile's builds can run on WebGL 2, which has no compute shaders. Where a browser runs WebGL 2, "
               "the particles pass through every surface.";
    }
    return {};
}

void check_scene_collision_profiles(rule_context& context, const document_scan& scan)
{
    for(const auto& use : scan.scene_surface_emitters)
    {
        for(const auto& profile : context.get_input().profiles)
        {
            const std::string gap = get_scene_collision_gap(profile);
            if(gap.empty() || !is_shipped_by(profile, use.document))
            {
                continue;
            }
            const bool is_one = use.entities.size() == 1;
            auto item = context.make(rules::SCENE_COLLISION_OFF,
                                     make_document_subject(use.document, {}, {}),
                                     fmt::format("{} Particle {} with Scene Surfaces, {}",
                                                 use.entities.size(),
                                                 is_one ? "Emitter collides" : "Emitters collide",
                                                 gap));
            item.details = list_entities(use.entities);
            item.profiles.push_back(profile.name);
            context.add(std::move(item));
        }
    }
}

void check_gi_ambient_materials(rule_context& context, const document_scan& scan)
{
    for(const auto& emitter : scan.gi_ambient_misfits)
    {
        context.add(context.make(rules::GI_AMBIENT_NOT_TAKEN,
                                 make_document_subject(emitter.document, emitter.entity_id, emitter.entity),
                                 emitter.message));
    }
}

void check_unshown_colors(rule_context& context, const document_scan& scan)
{
    for(const auto& unshown : scan.unshown_colors)
    {
        context.add(context.make(rules::COLOR_NOT_SHOWN,
                                 make_document_subject(unshown.document, unshown.entity_id, unshown.entity),
                                 fmt::format("The {}'s Color Over Length changes nothing on screen, because its Material {} "
                                             "has no Vertex Color node.",
                                             unshown.component,
                                             unshown.material)));
    }
}

void check_strip_materials(rule_context& context, const document_scan& scan)
{
    for(const auto& unsuited : scan.unsuited_strip_materials)
    {
        context.add(context.make(rules::STRIP_MATERIAL,
                                 make_document_subject(unsuited.document, unsuited.entity_id, unsuited.entity),
                                 fmt::format("The {}'s Material {} is no Surface material, so the strip draws the engine's strip "
                                             "material.",
                                             unsuited.component,
                                             unsuited.material)));
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

/// The step between neighbouring float values at @p coordinate meters, in millimeters.
auto get_float_step_mm(double coordinate) -> double
{
    const float value = float(coordinate);
    return double(std::nextafter(value, std::numeric_limits<float>::max()) - value) * 1000.0;
}

void check_far_content(rule_context& context, const document_scan& scan)
{
    for(const auto& content : scan.far_entities)
    {
        // One issue per entity: the precision is the engine's, the same for every profile shipping the document.
        auto item = context.make(rules::FAR_FROM_ORIGIN,
                                 make_document_subject(content.document, content.entity_id, content.entity_path),
                                 fmt::format("The entity is {:.1f} km from the world origin along an axis, where a "
                                             "float position moves in steps of {:.1f} mm.",
                                             content.coordinate / 1000.0,
                                             get_float_step_mm(content.coordinate)));
        item.discriminator = content.entity_id;
        for(const auto& profile : context.get_input().profiles)
        {
            const auto limit =
                context.get_threshold(rules::FAR_FROM_ORIGIN, thresholds::MAX_COORDINATE, profile.platform);
            if(limit > 0 && content.coordinate > double(limit) && is_shipped_by(profile, content.document))
            {
                item.profiles.push_back(profile.name);
            }
        }
        if(!item.profiles.empty())
        {
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
    check_emitter_materials(context, scan);
    check_local_space_collisions(context, scan);
    check_collision_casts(context, scan);
    check_unreported_scene_contacts(context, scan);
    check_scene_collision_profiles(context, scan);
    check_gi_ambient_materials(context, scan);
    check_unshown_colors(context, scan);
    check_strip_materials(context, scan);
    check_stereo_spatial(context, scan);
    check_camera_depth_ranges(context, scan);
    check_far_content(context, scan);
}

} // namespace unravel::audit::detail

namespace unravel::audit
{

auto make_scene_contact_message(const scene_contact_needs& needs) -> std::string
{
    if(needs.draws_meshes)
    {
        return "The Particle Emitter's Collide With is Scene Surfaces, and its particles are meshes, which do not touch "
               "scene surfaces. Its Collision does nothing.";
    }
    std::string losses;
    if(needs.fires_on_collision)
    {
        losses += " Its sub-emitter on Collision never fires.";
    }
    if(needs.fires_on_death)
    {
        losses += " Its sub-emitter on Death fires where the particle would be without its contacts.";
    }
    if(needs.sends_death_events)
    {
        losses += " Its death events give the place the particle would be without its contacts.";
    }
    if(needs.lights_scene)
    {
        losses += " Its particle lights follow the paths the particles would take without their contacts.";
    }
    if(losses.empty())
    {
        return {};
    }
    return "The Particle Emitter collides with Scene Surfaces, and the game is told of no such contact." + losses;
}

auto make_gi_ambient_message(const std::string& material, std::uint32_t texture_count, bool reads_scene_color, std::uint32_t max_textures)
    -> std::string
{
    const bool has_too_many = texture_count > max_textures;
    if(!has_too_many && !reads_scene_color)
    {
        return {};
    }
    std::string reads;
    if(has_too_many)
    {
        reads = fmt::format("{} textures", texture_count);
    }
    if(reads_scene_color)
    {
        reads += has_too_many ? " and the Scene Color" : "the Scene Color";
    }
    return fmt::format("The Particle Emitter's Ambient Light is Global Illumination, and its Material {} reads {}. A material "
                       "lit that way reads at most {} textures and no Scene Color, so these particles keep the sky's light.",
                       material,
                       reads,
                       max_textures);
}

} // namespace unravel::audit
