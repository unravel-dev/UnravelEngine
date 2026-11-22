#include "audit_document_scan.h"

#include <deploy/files.h>
#include <editor/editing/prefab_bake.h>
#include <engine/animation/animation_controller.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/missing_asset_recorder.h>
#include <engine/audio/ecs/components/audio_source_component.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/prefab.h>
#include <engine/ecs/scene.h>
#include <engine/meta/animation/animation_controller.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <engine/meta/rendering/material.hpp>
#include <engine/physics/ecs/components/joint_component.h>
#include <engine/rendering/ecs/components/assao_component.h>
#include <engine/rendering/ecs/components/bloom_component.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/decal_component.h>
#include <engine/rendering/ecs/components/fxaa_component.h>
#include <engine/rendering/ecs/components/gi_component.h>
#include <engine/rendering/ecs/components/gtao_component.h>
#include <engine/rendering/ecs/components/post_process_material_component.h>
#include <engine/rendering/ecs/components/ssil_component.h>
#include <engine/rendering/ecs/components/ssr_component.h>
#include <engine/rendering/ecs/components/taa_component.h>
#include <engine/rendering/material.h>
#include <engine/rendering/material_graph/graph_material.h>
#include <reflection/reflection.h>
#include <ser20/external/simdjson/simdjson.h>
#include <string_utils/utils.h>

#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace unravel
{
namespace
{

/// The deepest entity path the scan follows: deeper hierarchies are cut short.
constexpr int AUDIT_MAX_ENTITY_DEPTH = 64;

/// Entities by their uid's text: the path of each, as names from its root.
using audit_entity_paths = std::unordered_map<std::string, std::string>;

auto audit_entity_name(entt::handle entity) -> std::string
{
    const auto* tag = entity.try_get<tag_component>();
    return tag != nullptr && !tag->name.empty() ? tag->name : std::string("(unnamed)");
}

auto make_audit_entity_path(entt::handle entity) -> std::string
{
    std::vector<std::string> names;
    for(int depth = 0; entity && depth < AUDIT_MAX_ENTITY_DEPTH; ++depth)
    {
        names.push_back(audit_entity_name(entity));
        const auto* transform = entity.try_get<transform_component>();
        entity = transform != nullptr ? transform->get_parent() : entt::handle{};
    }
    std::reverse(names.begin(), names.end());
    std::string path;
    for(const auto& name : names)
    {
        path += path.empty() ? name : "/" + name;
    }
    return path;
}

auto audit_entity_uid(entt::handle entity) -> std::string
{
    const auto* id = entity.try_get<id_component>();
    return id != nullptr ? hpp::to_string(id->id) : std::string{};
}

auto collect_entity_paths(entt::registry& registry) -> audit_entity_paths
{
    audit_entity_paths paths;
    for(const auto entity : registry.view<id_component>())
    {
        const entt::handle handle(registry, entity);
        paths.emplace(audit_entity_uid(handle), make_audit_entity_path(handle));
    }
    return paths;
}

/// "unravel::audio_clip" -> "audio clip".
auto make_asset_type_label(const std::string& type_name) -> std::string
{
    const auto separator = type_name.rfind("::");
    auto label = separator == std::string::npos ? type_name : type_name.substr(separator + 2);
    std::replace(label.begin(), label.end(), '_', ' ');
    return label;
}

auto read_document_text(const std::string& key, std::string& text) -> bool
{
    std::ifstream stream(fs::resolve_protocol(key), std::ios::binary);
    if(!stream.good())
    {
        return false;
    }
    text = fs::read_stream_str(stream);
    return true;
}

/// Finds, for each uid, the entity of the document whose components hold it: an entity is an object with a "uid"
/// text and a "components" object.
class audit_uid_locator
{
public:
    explicit audit_uid_locator(const std::vector<std::string>& uids)
        : wanted_(uids.begin(), uids.end())
    {
    }

    void walk(const simdjson::dom::element& element, const std::string& entity)
    {
        switch(element.type())
        {
            case simdjson::dom::element_type::OBJECT:
                walk_object(element.get_object().value_unsafe(), entity);
                break;
            case simdjson::dom::element_type::ARRAY:
                for(const auto child : element.get_array().value_unsafe())
                {
                    walk(child, entity);
                }
                break;
            case simdjson::dom::element_type::STRING:
                note(std::string(element.get_string().value_unsafe()), entity);
                break;
            default:
                break;
        }
    }

    auto get_found() const -> const std::unordered_map<std::string, std::string>&
    {
        return found_;
    }

private:
    void walk_object(const simdjson::dom::object& object, const std::string& entity)
    {
        std::string_view uid;
        simdjson::dom::object components;
        const bool is_entity =
            object["uid"].get(uid) == simdjson::SUCCESS && object["components"].get(components) == simdjson::SUCCESS;
        if(is_entity)
        {
            walk(components, std::string(uid));
            return;
        }
        for(const auto field : object)
        {
            walk(field.value, entity);
        }
    }

    void note(std::string text, const std::string& entity)
    {
        text = string_utils::to_lower(text);
        if(wanted_.count(text) > 0 && found_.count(text) == 0)
        {
            found_.emplace(std::move(text), entity);
        }
    }

    std::unordered_set<std::string> wanted_;
    std::unordered_map<std::string, std::string> found_;
};

/**
 * @brief Adds the references a load recorded as missing that the document's own text holds: one a nested prefab
 * holds belongs to that prefab, which is checked as a document of its own.
 */
void add_missing(const std::string& document,
                 const std::vector<missing_asset_recorder::entry>& missing,
                 const audit_entity_paths& paths,
                 audit::document_scan& out)
{
    if(missing.empty())
    {
        return;
    }
    std::string text;
    if(!read_document_text(document, text))
    {
        return;
    }
    const auto lowered = string_utils::to_lower(text);
    std::vector<std::string> uids;
    for(const auto& entry : missing)
    {
        uids.push_back(string_utils::to_lower(hpp::to_string(entry.uid)));
    }
    audit_uid_locator locator(uids);
    simdjson::dom::parser parser;
    simdjson::dom::element root;
    if(parser.parse(text).get(root) == simdjson::SUCCESS)
    {
        locator.walk(root, {});
    }
    for(std::size_t index = 0; index < missing.size(); ++index)
    {
        const auto& uid = uids[index];
        if(lowered.find(uid) == std::string::npos)
        {
            continue;
        }
        audit::missing_reference reference;
        reference.document = document;
        reference.uid = missing[index].uid;
        reference.asset_type = make_asset_type_label(missing[index].type);
        const auto found = locator.get_found().find(uid);
        if(found != locator.get_found().end() && !found->second.empty())
        {
            reference.entity_id = found->second;
            const auto path = paths.find(found->second);
            reference.entity = path != paths.end() ? path->second : found->second;
        }
        out.missing.push_back(std::move(reference));
    }
}

void add_joint_problems(entt::registry& registry, const std::string& document, audit::document_scan& out)
{
    for(const auto entity : registry.view<joint_component>())
    {
        const entt::handle handle(registry, entity);
        const auto problem = find_joint_problem(handle.get<joint_component>());
        if(!problem.empty())
        {
            out.joints.push_back({document, audit_entity_uid(handle), make_audit_entity_path(handle), std::string(problem)});
        }
    }
}

template<typename Component>
void add_feature_use(entt::registry& registry, const std::string& document, audit::limited_feature feature, audit::document_scan& out)
{
    audit::feature_use use;
    use.document = document;
    use.feature = feature;
    use.component = get_pretty_name(entt::resolve<Component>());
    for(const auto entity : registry.view<Component>())
    {
        use.entities.push_back(make_audit_entity_path(entt::handle(registry, entity)));
    }
    if(!use.entities.empty())
    {
        std::sort(use.entities.begin(), use.entities.end());
        out.features.push_back(std::move(use));
    }
}

void add_feature_uses(entt::registry& registry, const std::string& document, audit::document_scan& out)
{
    using feature = audit::limited_feature;
    add_feature_use<gi_component>(registry, document, feature::global_illumination, out);
    add_feature_use<ssr_component>(registry, document, feature::screen_space_reflections, out);
    add_feature_use<ssil_component>(registry, document, feature::screen_space_indirect_lighting, out);
    add_feature_use<gtao_component>(registry, document, feature::ambient_occlusion, out);
    add_feature_use<assao_component>(registry, document, feature::ambient_occlusion, out);
    add_feature_use<bloom_component>(registry, document, feature::bloom, out);
    add_feature_use<decal_component>(registry, document, feature::decals, out);
    add_feature_use<post_process_material_component>(registry, document, feature::post_process_materials, out);
    add_feature_use<taa_component>(registry, document, feature::temporal_anti_aliasing, out);
    add_feature_use<fxaa_component>(registry, document, feature::fxaa, out);
}

void add_audio_sources(entt::registry& registry, const std::string& document, audit::document_scan& out)
{
    for(const auto entity : registry.view<audio_source_component>())
    {
        const auto& source = registry.get<audio_source_component>(entity);
        const auto& clip = source.get_clip();
        if(clip)
        {
            out.audio_sources.push_back({document, clip.id(), source.is_spatial()});
        }
    }
}

void add_camera_depth_ranges(entt::registry& registry, const std::string& document, audit::document_scan& out)
{
    for(const auto entity : registry.view<camera_component>())
    {
        const entt::handle handle(registry, entity);
        const auto& component = handle.get<camera_component>();
        // An orthographic camera's depth is linear: its precision does not depend on the near clip.
        if(component.get_camera().get_projection_mode() != projection_mode::perspective)
        {
            continue;
        }
        out.cameras.push_back({document,
                               audit_entity_uid(handle),
                               make_audit_entity_path(handle),
                               component.get_near_clip(),
                               component.get_far_clip()});
    }
}

auto get_compiled_file(const std::string& key, const fs::path& project_root) -> fs::path
{
    const auto files = deployment::get_compiled_paths(key, project_root);
    return files.empty() ? fs::path{} : files.front();
}

} // namespace

void audit_document_scanner::scan(rtti::context& ctx,
                                  const audit_document& document,
                                  const fs::path& project_root,
                                  audit::document_scan& out)
{
    ++out.documents;
    switch(document.type)
    {
        case audit_document::kind::scene:
        case audit_document::kind::prefab:
            scan_entities(ctx, document, out);
            break;
        case audit_document::kind::material:
            scan_material(document, project_root, out);
            break;
        case audit_document::kind::controller:
            scan_controller(document, project_root, out);
            break;
    }
}

void audit_document_scanner::scan_entities(rtti::context& ctx, const audit_document& document, audit::document_scan& out)
{
    auto& am = ctx.get_cached<asset_manager>();
    const bool is_prefab = document.type == audit_document::kind::prefab;
    const auto problem = is_prefab ? find_document_problem(am.get_asset<prefab>(document.key))
                                   : find_document_problem(am.get_asset<scene_prefab>(document.key));
    if(!problem.empty())
    {
        out.unreadable.push_back({document.key, problem});
        return;
    }
    // A scene of its own for this document alone: it exists only during this call, so no frame, play start or
    // checkpoint ever sees it.
    scene scratch("project_audit");
    missing_asset_recorder recorder;
    entt::handle root;
    if(!load_prefab_document(am, scratch, document.key, is_prefab, root))
    {
        out.unreadable.push_back({document.key, "The document did not load; the log says why."});
        scratch.unload();
        return;
    }
    auto& registry = *scratch.registry;
    add_missing(document.key, recorder.get_missing(), collect_entity_paths(registry), out);
    add_joint_problems(registry, document.key, out);
    add_feature_uses(registry, document.key, out);
    add_audio_sources(registry, document.key, out);
    add_camera_depth_ranges(registry, document.key, out);
    scratch.unload();
}

void audit_document_scanner::scan_material(const audit_document& document, const fs::path& project_root, audit::document_scan& out)
{
    const auto file = get_compiled_file(document.key, project_root);
    if(file.empty())
    {
        return;
    }
    missing_asset_recorder recorder;
    std::shared_ptr<material> loaded;
    try
    {
        load_from_file_bin(file.string(), loaded);
    }
    catch(const std::exception& e)
    {
        out.unreadable.push_back({document.key, std::string("The material did not load: ") + e.what()});
        return;
    }
    add_missing(document.key, recorder.get_missing(), {}, out);
    const auto* graph = loaded ? loaded->safe_cast<graph_material>() : nullptr;
    if(graph != nullptr && graph->is_forward() && (graph->get_scene_textures() & material_scene_texture_color) != 0)
    {
        out.scene_color_materials.push_back(document.key);
    }
}

void audit_document_scanner::scan_controller(const audit_document& document,
                                             const fs::path& project_root,
                                             audit::document_scan& out)
{
    const auto file = get_compiled_file(document.key, project_root);
    if(file.empty())
    {
        return;
    }
    missing_asset_recorder recorder;
    auto controller = std::make_shared<animation_controller>();
    try
    {
        load_from_file_bin(file.string(), controller);
    }
    catch(const std::exception& e)
    {
        out.unreadable.push_back({document.key, std::string("The controller did not load: ") + e.what()});
        return;
    }
    add_missing(document.key, recorder.get_missing(), {}, out);
    if(!controller)
    {
        return;
    }
    auto problems = validate_animation_controller(*controller);
    if(!problems.empty())
    {
        out.controllers.emplace_back(document.key, std::move(problems));
    }
}

} // namespace unravel
