#include "entity_clipboard.h"

#include <editor/editing/actions/entity_actions.h>
#include <editor/editing/editing_manager.h>
#include <editor/editing/clipboard_document.h>

#include <engine/assets/asset_dependency_graph.h>
#include <engine/assets/asset_manager.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/scene.h>
#include <engine/engine.h>
#include <engine/meta/ecs/entity.hpp>

#include <logging/logging.h>
#include <ser20/external/simdjson/simdjson.h>

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace unravel
{
namespace
{


/// Where one copied root came from: what a paste places it by.
struct root_origin
{
    hpp::uuid source{};
    hpp::uuid parent{};
    math::transform world{};
    /// Whether the copy gives the world pose; a root without one keeps the transform it was copied with.
    bool has_world{};
};

/// A copy of entities, read from its document.
struct entities_copy
{
    /// What save_clone_to_stream wrote, for scene::load_copy.
    std::string document;
    std::vector<root_origin> origins;
    /// The prefab assets the copy holds instances of; empty in a copy that does not list them.
    std::vector<hpp::uuid> prefabs;
    std::string project;
};

auto uid_of(entt::handle entity) -> hpp::uuid
{
    const auto* id = entity ? entity.try_get<id_component>() : nullptr;
    return id != nullptr ? id->id : hpp::uuid{};
}

/// A path of sibling places from the scene root, for ordering entities as the hierarchy shows them.
auto get_hierarchy_path(entt::handle entity) -> std::vector<size_t>
{
    std::vector<size_t> path;
    while(entity)
    {
        const auto& transform = entity.get<transform_component>();
        path.push_back(transform.get_sibling_index());
        entity = transform.get_parent();
    }
    std::reverse(path.begin(), path.end());
    return path;
}

/// The top-level entities among @p entities, as the hierarchy orders them.
auto collect_copy_roots(const std::vector<entt::handle>& entities) -> std::vector<entt::handle>
{
    std::vector<entt::handle> valid;
    std::copy_if(entities.begin(),
                 entities.end(),
                 std::back_inserter(valid),
                 [](const entt::handle& entity)
                 {
                     return entity && entity.all_of<transform_component>();
                 });
    auto roots = transform_component::get_top_level_entities(valid);
    std::vector<std::pair<std::vector<size_t>, entt::handle>> ordered;
    ordered.reserve(roots.size());
    for(const auto& root : roots)
    {
        ordered.emplace_back(get_hierarchy_path(root), root);
    }
    std::sort(ordered.begin(),
              ordered.end(),
              [](const auto& lhs, const auto& rhs)
              {
                  return lhs.first < rhs.first;
              });
    roots.clear();
    for(const auto& item : ordered)
    {
        roots.push_back(item.second);
    }
    return roots;
}

/// The prefab assets instanced at or under @p entity.
void collect_prefab_sources(entt::handle entity, std::set<hpp::uuid>& out)
{
    if(const auto* prefab_comp = entity.try_get<prefab_component>())
    {
        if(!prefab_comp->source.uid().is_nil())
        {
            out.insert(prefab_comp->source.uid());
        }
    }
    for(const auto& child : entity.get<transform_component>().get_children())
    {
        collect_prefab_sources(child, out);
    }
}

auto origin_vec3_to_json(const math::vec3& v) -> std::string
{
    return fmt::format("[{},{},{}]", v.x, v.y, v.z);
}

auto origin_to_json(entt::handle root) -> std::string
{
    const auto& transform = root.get<transform_component>();
    const auto world = transform.get_transform_global();
    const auto rotation = world.get_rotation();
    return fmt::format(R"({{"source":"{}","parent":"{}","world":{{"position":{},"rotation":[{},{},{},{}],"scale":{}}}}})",
                       uid_of(root).to_string(),
                       uid_of(transform.get_parent()).to_string(),
                       origin_vec3_to_json(world.get_position()),
                       rotation.x,
                       rotation.y,
                       rotation.z,
                       rotation.w,
                       origin_vec3_to_json(world.get_scale()));
}

auto write_copy_document(const std::vector<entt::handle>& roots) -> std::string
{
    std::set<hpp::uuid> prefabs;
    std::string origins;
    std::vector<entt::const_handle> const_roots;
    for(const auto& root : roots)
    {
        collect_prefab_sources(root, prefabs);
        origins += (origins.empty() ? "" : ",") + origin_to_json(root);
        const_roots.emplace_back(root);
    }
    std::string prefab_list;
    for(const auto& uid : prefabs)
    {
        prefab_list += (prefab_list.empty() ? "\"" : ",\"") + uid.to_string() + "\"";
    }
    std::stringstream entities;
    save_clone_to_stream(entities, const_roots, clone_identity::uuids);
    return fmt::format(R"({},"prefabs":[{}],"origins":[{}],"entities":{}}})",
                       clipboard_document::make_head(clipboard_document::ENTITIES_KIND),
                       prefab_list,
                       origins,
                       entities.str());
}

auto read_uuid(const simdjson::dom::object& object, const char* key) -> hpp::uuid
{
    std::string_view text;
    if(object[key].get(text))
    {
        return {};
    }
    return hpp::uuid::from_string(std::string(text)).value_or(hpp::uuid{});
}

auto read_origin_numbers(const simdjson::dom::element& element, float* out, size_t count) -> bool
{
    simdjson::dom::array array;
    if(element.get(array) || array.size() != count)
    {
        return false;
    }
    size_t i = 0;
    for(auto value : array)
    {
        double number = 0.0;
        if(value.get(number))
        {
            return false;
        }
        out[i++] = static_cast<float>(number);
    }
    return true;
}

auto read_origin(const simdjson::dom::object& object) -> root_origin
{
    root_origin origin;
    origin.source = read_uuid(object, "source");
    origin.parent = read_uuid(object, "parent");
    simdjson::dom::object world;
    if(!object["world"].get(world))
    {
        math::vec3 position{0.0f};
        math::vec3 scale{1.0f};
        float rotation[4]{0.0f, 0.0f, 0.0f, 1.0f};
        read_origin_numbers(world["position"], &position.x, 3);
        read_origin_numbers(world["scale"], &scale.x, 3);
        read_origin_numbers(world["rotation"], rotation, 4);
        origin.world.set_position(position);
        origin.world.set_rotation(math::quat(rotation[3], rotation[0], rotation[1], rotation[2]));
        origin.world.set_scale(scale);
        origin.has_world = true;
    }
    return origin;
}

/// Reads a copy of entities; false, with the reason as a sentence, when it is none or this build cannot read it.
auto read_copy_document(hpp::string_view text, entities_copy& out, std::string& problem) -> bool
{
    simdjson::dom::parser parser;
    simdjson::dom::object document;
    int64_t version = 0;
    std::string_view kind;
    if(text.empty() || parser.parse(text.data(), text.size()).get(document) || document[clipboard_document::VERSION_KEY].get(version) ||
       document["kind"].get(kind) || kind != clipboard_document::ENTITIES_KIND)
    {
        problem = "The clipboard holds no copy of entities.";
        return false;
    }
    if(version > clipboard_document::VERSION)
    {
        std::string_view engine_version;
        (void)document["engine_version"].get(engine_version);
        problem = clipboard_document::describe_newer(engine_version);
        return false;
    }
    simdjson::dom::object entities;
    int64_t layout = 0;
    if(document["entities"].get(entities) || entities["version"].get(layout))
    {
        problem = "The copy of entities is damaged: it holds no entities.";
        return false;
    }
    if(!is_readable_prefab_version(static_cast<uint32_t>(layout)))
    {
        problem = "The copy of entities was made by a newer editor, in a layout this editor cannot read.";
        return false;
    }
    out.document = simdjson::minify(entities);
    std::string_view project;
    if(!document["project"].get(project))
    {
        out.project = std::string(project);
    }
    simdjson::dom::array origins;
    if(!document["origins"].get(origins))
    {
        for(auto element : origins)
        {
            simdjson::dom::object origin;
            out.origins.push_back(element.get(origin) ? root_origin{} : read_origin(origin));
        }
    }
    simdjson::dom::array prefabs;
    if(!document["prefabs"].get(prefabs))
    {
        for(auto element : prefabs)
        {
            std::string_view uid;
            if(!element.get(uid))
            {
                out.prefabs.push_back(hpp::uuid::from_string(std::string(uid)).value_or(hpp::uuid{}));
            }
        }
    }
    return true;
}

/// Whether any of @p prefabs is @p target or holds an instance of it, at any depth.
auto nests_prefab(asset_manager& am, const std::vector<hpp::uuid>& prefabs, const hpp::uuid& target) -> bool
{
    const auto resolve = asset_deps::make_prefab_dependency_resolver(am);
    std::vector<hpp::uuid> pending(prefabs.begin(), prefabs.end());
    std::set<hpp::uuid> visited;
    while(!pending.empty())
    {
        const auto uid = pending.back();
        pending.pop_back();
        if(uid == target)
        {
            return true;
        }
        if(uid.is_nil() || !visited.insert(uid).second)
        {
            continue;
        }
        const auto dependencies = resolve(uid);
        pending.insert(pending.end(), dependencies.begin(), dependencies.end());
    }
    return false;
}

/// Why a copy cannot go into the scene being edited, or empty.
auto find_copy_problem(rtti::context& ctx, const entities_copy& copy) -> std::string
{
    auto& em = ctx.get_cached<editing_manager>();
    if(em.get_active_scene(ctx) == nullptr)
    {
        return "No scene is open to paste into.";
    }
    if(em.is_prefab_mode() && nests_prefab(ctx.get_cached<asset_manager>(), copy.prefabs, em.edited_prefab.uid()))
    {
        return "A prefab cannot hold an instance of itself, and the copy holds one of the prefab being edited.";
    }
    return {};
}

/// The name a pasted root takes among the siblings it lands with: its own, or with the next free number when one of
/// them has it already ("Crate" becomes "Crate (1)").
auto make_sibling_name(const std::string& name, const std::vector<std::string>& sibling_names) -> std::string
{
    if(std::find(sibling_names.begin(), sibling_names.end(), name) == sibling_names.end())
    {
        return name;
    }
    std::string base = name;
    if(!base.empty() && base.back() == ')')
    {
        const auto open = base.rfind(" (");
        const bool is_number = open != std::string::npos && open + 2 < base.size() - 1 &&
                               std::all_of(base.begin() + static_cast<std::ptrdiff_t>(open) + 2,
                                           base.end() - 1,
                                           [](char c)
                                           {
                                               return c >= '0' && c <= '9';
                                           });
        if(is_number)
        {
            base = base.substr(0, open);
        }
    }
    for(int number = 1;; ++number)
    {
        auto candidate = fmt::format("{} ({})", base, number);
        if(std::find(sibling_names.begin(), sibling_names.end(), candidate) == sibling_names.end())
        {
            return candidate;
        }
    }
}

/// The names of the entities @p entity stands among, itself left out.
auto collect_sibling_names(entt::handle entity) -> std::vector<std::string>
{
    std::vector<std::string> names;
    const auto add = [&](entt::handle sibling)
    {
        const auto* tag = sibling.try_get<tag_component>();
        if(sibling != entity && tag != nullptr)
        {
            names.push_back(tag->name);
        }
    };
    if(auto parent = entity.get<transform_component>().get_parent())
    {
        for(const auto& sibling : parent.get<transform_component>().get_children())
        {
            add(sibling);
        }
        return names;
    }
    auto& registry = *entity.registry();
    registry.view<root_component>().each(
        [&](entt::entity e, const root_component&)
        {
            add(entt::handle(registry, e));
        });
    return names;
}

void give_sibling_name(entt::handle entity)
{
    if(auto* tag = entity.try_get<tag_component>())
    {
        tag->name = make_sibling_name(tag->name, collect_sibling_names(entity));
    }
}

/// Where one pasted root goes: under which parent, at which place among its siblings, and whether it keeps its world
/// pose or its transform relative to its parent.
struct root_place
{
    entt::handle parent{};
    /// Right after this entity; empty for last.
    entt::handle after{};
    bool keeps_world{true};
};

auto decide_place(scene& scn, const root_origin& origin, paste_placement placement, entt::handle anchor) -> root_place
{
    const auto place_next_to = [](entt::handle entity) -> root_place
    {
        return {entity.get<transform_component>().get_parent(), entity, true};
    };
    switch(placement)
    {
        case paste_placement::child_keep_local:
            return {anchor, {}, false};
        case paste_placement::child_keep_world:
            return {anchor, {}, true};
        case paste_placement::scene_root:
            return {};
        case paste_placement::next_to:
            if(anchor)
            {
                return place_next_to(anchor);
            }
            break;
        case paste_placement::next_to_source:
            break;
    }
    if(auto source = scn.find_entity_by_uuid(origin.source))
    {
        return place_next_to(source);
    }
    return {scn.find_entity_by_uuid(origin.parent), {}, true};
}

/// Keeps a place inside @p content_root, the root of the prefab being edited: the prefab's file holds only what is
/// under it, so a paste that would land beside it or at the scene root lands under it.
auto keep_inside(root_place place, entt::handle content_root) -> root_place
{
    if(content_root && !place.parent)
    {
        place.parent = content_root;
        place.after = {};
    }
    return place;
}

/// Puts the pasted roots where @p placement says, in their order: several roots that land after one entity follow it
/// one after another.
void place_roots(scene& scn,
                 const std::vector<entt::handle>& roots,
                 const std::vector<root_origin>& origins,
                 paste_placement placement,
                 entt::handle anchor,
                 entt::handle content_root)
{
    std::map<entt::entity, entt::handle> last_after;
    for(size_t i = 0; i < roots.size(); ++i)
    {
        const auto origin = i < origins.size() ? origins[i] : root_origin{};
        auto& transform = roots[i].get<transform_component>();
        const auto place = keep_inside(decide_place(scn, origin, placement, anchor), content_root);
        if(place.parent)
        {
            transform.set_parent(place.parent, false);
        }
        if(place.keeps_world && origin.has_world)
        {
            transform.set_transform_global(origin.world);
        }
        if(place.after)
        {
            // After the entity, or after the copy pasted after it before this one.
            auto& after = last_after[place.after.entity()];
            const auto previous = after ? after : place.after;
            transform.set_sibling_index(previous.get<transform_component>().get_sibling_index() + 1);
            after = roots[i];
        }
        give_sibling_name(roots[i]);
    }
}

/// Selects the pasted roots as part of the paste's own step: a selection step of its own would make the first undo
/// after a paste take back only the selection.
void select_roots(editing_manager& em, const std::vector<entt::handle>& roots)
{
    if(roots.empty())
    {
        return;
    }
    std::vector<entt::meta_any> selection;
    selection.reserve(roots.size());
    for(const auto& root : roots)
    {
        selection.emplace_back(root);
    }
    em.restore_selection_impl(selection);
    em.focus(roots.back());
}

/// "Lamp" for one entity, "3 Entities" for several.
auto describe_entities(const std::vector<entt::handle>& roots) -> std::string
{
    if(roots.size() == 1)
    {
        const auto* tag = roots.front().try_get<tag_component>();
        return tag != nullptr && !tag->name.empty() ? tag->name : std::string("Entity");
    }
    return fmt::format("{} Entities", roots.size());
}

auto make_paste_step(rtti::context& ctx,
                     entities_copy copy,
                     paste_placement placement,
                     entt::handle anchor,
                     const std::shared_ptr<paste_result>& result) -> std::shared_ptr<editing_action_t>
{
    const auto anchor_id = entt::make_uhandle(anchor);
    return std::make_shared<create_entities_action_t>(
        [&ctx, copy = std::move(copy), placement, anchor_id, result]() -> std::vector<entt::handle>
        {
            auto& em = ctx.get_cached<editing_manager>();
            auto* scn = em.get_active_scene(ctx);
            if(scn == nullptr)
            {
                return {};
            }
            auto roots = scn->load_copy(copy.document);
            const auto content_root = em.is_prefab_mode() ? em.prefab_entity : entt::handle{};
            place_roots(*scn, roots, copy.origins, placement, anchor_id.resolve(), content_root);
            select_roots(em, roots);
            if(result)
            {
                result->roots = roots;
            }
            return roots;
        });
}

} // namespace

namespace entity_clipboard
{

auto copy(const std::vector<entt::handle>& entities) -> std::string
{
    const auto roots = collect_copy_roots(entities);
    return roots.empty() ? std::string{} : write_copy_document(roots);
}

auto holds_entities(hpp::string_view text) -> bool
{
    return clipboard_document::is_kind(text, clipboard_document::ENTITIES_KIND);
}

auto make_paste_action(rtti::context& ctx,
                       std::string text,
                       paste_placement placement,
                       entt::handle anchor,
                       const std::shared_ptr<paste_result>& result) -> std::shared_ptr<editing_action_t>
{
    entities_copy copy;
    std::string problem;
    if(read_copy_document(text, copy, problem))
    {
        problem = find_copy_problem(ctx, copy);
    }
    if(!problem.empty())
    {
        if(result)
        {
            result->problem = problem;
        }
        return nullptr;
    }
    const auto count = copy.origins.size();
    auto action = make_paste_step(ctx, std::move(copy), placement, anchor, result);
    action->name = count == 1 ? "Paste Entity" : fmt::format("Paste {} Entities", count);
    return action;
}

auto make_duplicate_action(rtti::context& ctx,
                           const std::vector<entt::handle>& entities,
                           const std::shared_ptr<paste_result>& result) -> std::shared_ptr<editing_action_t>
{
    const auto roots = collect_copy_roots(entities);
    if(roots.empty())
    {
        return nullptr;
    }
    auto action = make_paste_action(ctx, write_copy_document(roots), paste_placement::next_to_source, {}, result);
    if(action)
    {
        action->name = "Duplicate " + describe_entities(roots);
    }
    return action;
}

auto clipboard_holds_entities() -> bool
{
    const char* text = ImGui::GetClipboardText();
    return text != nullptr && holds_entities(text);
}

void copy_to_clipboard(const std::vector<entt::handle>& entities)
{
    const auto text = copy(entities);
    if(!text.empty())
    {
        ImGui::SetClipboardText(text.c_str());
    }
}

void cut_to_clipboard(rtti::context& ctx, const std::vector<entt::handle>& entities)
{
    const auto roots = collect_copy_roots(entities);
    if(roots.empty())
    {
        return;
    }
    ImGui::SetClipboardText(write_copy_document(roots).c_str());
    auto& em = ctx.get_cached<editing_manager>();
    editing_manager::undo_scope undo(&em);
    em.queue_action("Cut " + describe_entities(roots), std::make_shared<delete_entities_action_t>(roots));
}

void paste_from_clipboard(rtti::context& ctx, paste_placement placement, entt::handle anchor)
{
    const char* text = ImGui::GetClipboardText();
    auto& em = ctx.get_cached<editing_manager>();
    auto result = std::make_shared<paste_result>();
    auto action = make_paste_action(ctx, text != nullptr ? text : "", placement, anchor, result);
    if(!action)
    {
        APPLOG_WARNING("{}", result->problem);
        return;
    }
    editing_manager::undo_scope undo(&em);
    em.queue_action({}, action);
}

void duplicate(rtti::context& ctx, const std::vector<entt::handle>& entities)
{
    auto& em = ctx.get_cached<editing_manager>();
    auto result = std::make_shared<paste_result>();
    auto action = make_duplicate_action(ctx, entities, result);
    if(!action)
    {
        if(!result->problem.empty())
        {
            APPLOG_WARNING("{}", result->problem);
        }
        return;
    }
    editing_manager::undo_scope undo(&em);
    em.queue_action({}, action);
}

} // namespace entity_clipboard
} // namespace unravel
