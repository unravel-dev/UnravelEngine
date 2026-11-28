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
#include <engine/rendering/ecs/components/line_component.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/rendering/ecs/components/post_process_material_component.h>
#include <engine/rendering/ecs/components/ssil_component.h>
#include <engine/rendering/ecs/components/ssr_component.h>
#include <engine/rendering/ecs/components/taa_component.h>
#include <engine/rendering/ecs/components/trail_component.h>
#include <engine/rendering/material.h>
#include <engine/rendering/material_graph/graph_material.h>
#include <engine/rendering/material_graph/material_graph_compiler.h>
#include <reflection/reflection.h>
#include <ser20/external/simdjson/simdjson.h>
#include <string_utils/utils.h>

#include <algorithm>
#include <cmath>
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

/// Reports @p emitter when its Ambient Light is Global Illumination, the scene lights its Material @p graph (named
/// @p material) and that material leaves the Global Illumination no place in a particle draw.
void add_gi_ambient_misfit(entt::handle handle,
                           const particle_emitter_component& emitter,
                           const graph_material& graph,
                           const std::string& material,
                           const std::string& document,
                           audit::document_scan& out)
{
    const bool asks = emitter.get_ambient_light() == ps_soa::particle_ambient::global_illumination &&
                      emitter.get_lighting() != ps_soa::particle_lighting::unlit &&
                      graph.get_shading_model() != material_shading_model::unlit;
    if(!asks)
    {
        return;
    }
    std::string message = audit::make_gi_ambient_message(material,
                                                         std::uint32_t(graph.get_textures().size()),
                                                         (graph.get_scene_textures() & material_scene_texture_color) != 0,
                                                         MATERIAL_GRAPH_MAX_PARTICLE_GI_TEXTURES);
    if(!message.empty())
    {
        out.gi_ambient_misfits.push_back({document, audit_entity_uid(handle), make_audit_entity_path(handle), std::move(message)});
    }
}

/// The emitters whose sprites, trails and ribbons cannot draw the Material they name: one that is no Surface material
/// with Particle Use, and the ones whose Material cannot take the Ambient Light they ask for. A material that does
/// not load is a missing reference, reported as one.
void add_emitter_material_problems(entt::registry& registry, const std::string& document, audit::document_scan& out)
{
    for(const auto entity : registry.view<particle_emitter_component>())
    {
        const entt::handle handle(registry, entity);
        const auto& emitter = handle.get<particle_emitter_component>();
        const auto& named = emitter.get_render_material();
        if(emitter.get_render_mode() == ps_soa::render_mode::mesh || named.uid().is_nil())
        {
            continue;
        }
        // Waits for the load: the scan reads what its documents name.
        const auto loaded = named.get();
        if(!loaded || asset_handle<material>::is_empty(loaded))
        {
            continue;
        }
        const auto* graph = loaded->safe_cast<graph_material>();
        if(graph == nullptr || !graph->is_particle_use())
        {
            out.emitter_materials.push_back({document, audit_entity_uid(handle), make_audit_entity_path(handle), named.id()});
            continue;
        }
        add_gi_ambient_misfit(handle, emitter, *graph, named.id(), document, out);
    }
}

/// What an emitter whose Collide With is Scene Surfaces asks for that a contact with one does not give. The rows the
/// Inspector hides for such an emitter (its Decal, Send Collision Events) are off with it and are not counted.
auto get_scene_contact_needs(const particle_emitter_component& emitter) -> audit::scene_contact_needs
{
    const auto fires_on = [&](ps_soa::particle_event_type trigger)
    {
        const auto& sub_emitters = emitter.get_sub_emitters();
        return std::any_of(sub_emitters.begin(),
                           sub_emitters.end(),
                           [trigger](const particle_sub_emitter& sub)
                           {
                               return sub.trigger == trigger;
                           });
    };
    audit::scene_contact_needs needs;
    needs.draws_meshes = emitter.get_render_mode() == ps_soa::render_mode::mesh;
    needs.fires_on_collision = fires_on(ps_soa::particle_event_type::collision);
    needs.fires_on_death = fires_on(ps_soa::particle_event_type::death);
    needs.sends_death_events = emitter.get_send_death_events();
    needs.lights_scene = emitter.get_light_count() > 0;
    return needs;
}

/// The emitters whose collision cannot work, costs casts or runs on the graphics processor: the ones that collide
/// while they simulate in Local space, the ones that collide with physics colliders, with the casts their particles
/// need, and the ones that collide with scene surfaces, with what they ask for that such contacts do not give.
void add_emitter_collisions(entt::registry& registry, const std::string& document, audit::document_scan& out)
{
    audit::collider_emitter_use colliders;
    colliders.document = document;
    audit::scene_surface_emitter_use scene_surfaces;
    scene_surfaces.document = document;
    for(const auto entity : registry.view<particle_emitter_component>())
    {
        const entt::handle handle(registry, entity);
        const auto& emitter = handle.get<particle_emitter_component>();
        if(emitter.get_collision() == ps_soa::collision_mode::none)
        {
            continue;
        }
        if(emitter.get_simulation_space() == ps_soa::simulation_space::local)
        {
            out.local_space_collisions.push_back({document, audit_entity_uid(handle), make_audit_entity_path(handle), {}});
            continue;
        }
        if(emitter.get_collide_with() == ps_soa::collision_source::scene_surfaces)
        {
            // The ones whose particles collide there at all: a mesh emitter's do not, and is reported for that.
            if(ps_soa::collides_with_scene_surfaces(emitter.get_desc()))
            {
                scene_surfaces.entities.push_back(make_audit_entity_path(handle));
            }
            std::string unreported = audit::make_scene_contact_message(get_scene_contact_needs(emitter));
            if(!unreported.empty())
            {
                out.unreported_scene_contacts.push_back(
                    {document, audit_entity_uid(handle), make_audit_entity_path(handle), std::move(unreported)});
            }
            continue;
        }
        const float casts = ps_soa::estimate_collision_casts_per_second(emitter.get_desc(), emitter.get_max_particles());
        if(casts > 0.0f)
        {
            colliders.entities.push_back(make_audit_entity_path(handle));
            colliders.casts_per_second += double(casts);
        }
    }
    if(!colliders.entities.empty())
    {
        out.collider_emitters.push_back(std::move(colliders));
    }
    if(!scene_surfaces.entities.empty())
    {
        out.scene_surface_emitters.push_back(std::move(scene_surfaces));
    }
}

/// Whether the materials of a run show the vertex colour of what they draw, by material id.
using vertex_color_reads = std::unordered_map<std::string, bool>;

/// Whether @p graph reads the vertex colour anywhere: in its pixels, the functions it calls included, or in a node
/// that feeds its vertices, such as a mask of its World Position Offset.
auto reads_vertex_color(const material_graph& graph) -> bool
{
    const auto compiled = compile_material_graph(graph);
    if(compiled.reads_vertex_color)
    {
        return true;
    }
    for(size_t i = 0; i < graph.nodes.size() && i < compiled.nodes.size(); ++i)
    {
        if(compiled.nodes[i].is_used && graph.nodes[i].type == "vertex_color")
        {
            return true;
        }
    }
    return false;
}

/// Whether @p named shows the vertex colour of a Line or a Trail that names it. True where there is
/// nothing to report: none named, one that does not load, and one that is no Surface material, in whose place the
/// component draws the engine's own, which shows it.
auto shows_vertex_color(const asset_handle<material>& named, vertex_color_reads& known) -> bool
{
    if(named.uid().is_nil())
    {
        return true;
    }
    const auto found = known.find(named.id());
    if(found != known.end())
    {
        return found->second;
    }
    // Waits for the load: the scan reads what its documents name.
    const auto loaded = named.get();
    const auto* graph = loaded && !asset_handle<material>::is_empty(loaded) ? loaded->safe_cast<graph_material>() : nullptr;
    const bool draws = graph != nullptr && graph->get_domain() == material_domain::surface;
    const bool is_shown = !draws || reads_vertex_color(graph->get_graph());
    known.emplace(named.id(), is_shown);
    return is_shown;
}

/// Whether a Line or a Trail draws @p named: none named, one that does not load, or a Surface material. Any other
/// leaves the component drawing the engine's strip material.
auto draws_on_strips(const asset_handle<material>& named) -> bool
{
    if(named.uid().is_nil())
    {
        return true;
    }
    // Waits for the load: the scan reads what its documents name.
    const auto loaded = named.get();
    if(!loaded || asset_handle<material>::is_empty(loaded))
    {
        return true;
    }
    const auto* graph = loaded->safe_cast<graph_material>();
    return graph != nullptr && graph->get_domain() == material_domain::surface;
}

/// Whether @p strip colours anything: a key of its Color Over Length that is not opaque white.
auto colors_its_length(const strip_style& strip) -> bool
{
    const auto& points = strip.color_over_length.get_points();
    return std::any_of(points.begin(),
                       points.end(),
                       [](const auto& point)
                       {
                           return point.element.value != math::vec4(1.0f);
                       });
}

/// The lines and trails whose material is no Surface material, and the ones whose colour is set and whose material
/// has no Vertex Color read to show it.
void add_strip_material_problems(entt::registry& registry, const std::string& document, vertex_color_reads& known, audit::document_scan& out)
{
    const auto report = [&](entt::entity entity, const dynamic_renderer& renderer, const char* component, const strip_style& strip)
    {
        const auto& named = renderer.get_material();
        const bool is_unsuited = !draws_on_strips(named);
        const bool hides_color = !is_unsuited && colors_its_length(strip) && !shows_vertex_color(named, known);
        if(!is_unsuited && !hides_color)
        {
            return;
        }
        const entt::handle handle(registry, entity);
        auto& problems = is_unsuited ? out.unsuited_strip_materials : out.unshown_colors;
        problems.push_back({document, audit_entity_uid(handle), make_audit_entity_path(handle), component, named.id()});
    };
    for(const auto entity : registry.view<line_component>())
    {
        const auto& line = registry.get<line_component>(entity);
        report(entity, line, "Line", line.strip);
    }
    for(const auto entity : registry.view<trail_component>())
    {
        const auto& trail = registry.get<trail_component>(entity);
        report(entity, trail, "Trail", trail.strip);
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

/// The emitters whose contacts leave decals, which the Decals limit turns off with the Decal components.
void add_collision_decal_use(entt::registry& registry, const std::string& document, audit::document_scan& out)
{
    audit::feature_use use;
    use.document = document;
    use.feature = audit::limited_feature::collision_decals;
    use.component = get_pretty_name(entt::resolve<particle_emitter_component>());
    for(const auto entity : registry.view<particle_emitter_component>())
    {
        const auto& emitter = registry.get<particle_emitter_component>(entity);
        // A contact with a scene surface leaves no decal (add_emitter_collisions reports that).
        const bool has_contacts = emitter.get_collision() != ps_soa::collision_mode::none &&
                                  emitter.get_collide_with() != ps_soa::collision_source::scene_surfaces;
        if(has_contacts && emitter.get_collision_decal().enabled)
        {
            use.entities.push_back(make_audit_entity_path(entt::handle(registry, entity)));
        }
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
    add_collision_decal_use(registry, document, out);
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

/// The document's entity with the largest coordinate: float precision follows the size of each coordinate, not the
/// distance.
void add_far_content(entt::registry& registry, const std::string& document, audit::document_scan& out)
{
    audit::far_content farthest{document};
    for(const auto entity : registry.view<transform_component>())
    {
        const entt::handle handle(registry, entity);
        const auto& position = handle.get<transform_component>().get_position_global();
        const double coordinate =
            std::max({std::abs(double(position.x)), std::abs(double(position.y)), std::abs(double(position.z))});
        if(coordinate > farthest.coordinate)
        {
            farthest.entity_id = audit_entity_uid(handle);
            farthest.entity_path = make_audit_entity_path(handle);
            farthest.coordinate = coordinate;
        }
    }
    if(farthest.coordinate > 0.0)
    {
        out.far_entities.push_back(std::move(farthest));
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

void audit_document_scanner::begin_run()
{
    shown_vertex_colors_.clear();
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
    add_emitter_material_problems(registry, document.key, out);
    add_emitter_collisions(registry, document.key, out);
    add_strip_material_problems(registry, document.key, shown_vertex_colors_, out);
    add_feature_uses(registry, document.key, out);
    add_audio_sources(registry, document.key, out);
    add_camera_depth_ranges(registry, document.key, out);
    add_far_content(registry, document.key, out);
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
