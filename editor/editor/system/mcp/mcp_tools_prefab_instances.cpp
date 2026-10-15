#include "mcp_asset_wait.h"
#include "mcp_project_paths.h"
#include "mcp_tools_common.h"

#include <editor/editing/actions/entity_actions.h>
#include <editor/editing/authoring_root.h>
#include <editor/editing/editing_manager.h>
#include <editor/editing/prefab_instance_changes.h>
#include <editor/system/mcp_manager.h>
#include <engine/ecs/components/prefab_component.h>
#include <uuid/uuid.h>

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <engine/assets/impl/asset_writer.h>

namespace unravel::mcp
{
namespace
{

constexpr int64_t k_apply_wait_default_ms = 5000;
constexpr int64_t k_apply_wait_max_ms = 30000;

auto parent_of(entt::handle entity) -> entt::handle
{
    const auto* trans_comp = entity.try_get<transform_component>();
    return trans_comp != nullptr ? trans_comp->get_parent() : entt::handle{};
}

auto entity_name(entt::handle entity) -> std::string
{
    const auto* tag = entity ? entity.try_get<tag_component>() : nullptr;
    return tag != nullptr ? tag->name : std::string{};
}

/// The instance an entity belongs to: the nearest instance root at or above it. Also an error
/// naming what the entity is instead.
auto find_instance_root(scene& scn, const std::string& entity_id, std::string& error) -> entt::handle
{
    auto entity = resolve_entity(scn, entity_id, error);
    if(!entity)
    {
        return {};
    }
    if(auto root = prefab_override_context::find_prefab_root_entity(entity))
    {
        return root;
    }
    error = is_authoring_root(entity) ? "The root of the prefab open in prefab mode is the prefab itself, not an "
                                        "instance; pass an entity inside one of its nested instances"
                                      : "Not part of a prefab instance: " + entity_id;
    return {};
}

/// What a rewrite of @p instance and the sync after it can change: the outermost instance at or
/// above it, below any authoring root.
auto find_synced_root(entt::handle instance) -> entt::handle
{
    auto outermost = instance;
    for(auto current = parent_of(instance); current && !is_authoring_root(current); current = parent_of(current))
    {
        if(current.all_of<prefab_component>())
        {
            outermost = current;
        }
    }
    return outermost;
}

/**
 * @brief Records a rewrite of what is stated on prefab instances, and the sync after it, as one
 *        undo step.
 *
 * Undo rebuilds the subtree the sync rewrites from a snapshot taken before, after @p undo_files
 * puts back what the rewrite wrote to disk; redo runs the rewrite again.
 */
void do_instance_rewrite(rtti::context& ctx,
                         const std::string& name,
                         entt::handle instance,
                         const std::function<void()>& rewrite,
                         const std::function<void()>& undo_files = {})
{
    auto snapshot =
        std::make_shared<entity_subtree_snapshot>(entity_subtree_snapshot::capture(find_synced_root(instance)));
    ctx.get_cached<editing_manager>().do_action(
        name,
        [&ctx, rewrite, target = entt::make_uhandle(instance)]()
        {
            rewrite();
            if(auto entity = target.resolve())
            {
                ctx.get_cached<editing_manager>().sync_after_override_change(ctx, entity);
            }
        },
        [snapshot, undo_files]()
        {
            if(undo_files)
            {
                undo_files();
            }
            snapshot->restore();
        });
}

auto override_json(const prefab_instance_changes& changes,
                   const prefab_property_override_data& override_data,
                   bool inherited) -> std::string
{
    const auto entity = scene::find_entity_by_prefab_uuid(changes.root, override_data.entity_uuid);
    const auto& label = override_data.pretty_component_path.empty() ? override_data.component_path
                                                                     : override_data.pretty_component_path;
    return fmt::format(R"({{"entity_id":{},"entity":{},"path":{},"label":{},"inherited":{}}})",
                       entity ? make_json_string(entity_id_string(entity)) : std::string("null"),
                       make_json_string(entity_name(entity)),
                       make_json_string(override_data.component_path),
                       make_json_string(label),
                       bool_to_json(inherited));
}

auto instance_changes_json(const prefab_instance_changes& changes) -> std::string
{
    std::string overrides;
    const auto add_overrides = [&](const std::set<prefab_property_override_data>& list, bool inherited)
    {
        for(const auto& override_data : list)
        {
            overrides += (overrides.empty() ? "" : ",") + override_json(changes, override_data, inherited);
        }
    };
    add_overrides(changes.about.local.overrides, false);
    add_overrides(changes.about.stated.overrides, true);
    std::string removed;
    for(const auto& removal : changes.removals)
    {
        removed += fmt::format(R"({}{{"id":{},"kind":{},"inherited":{}}})",
                               removed.empty() ? "" : ",",
                               make_json_string(hpp::to_string(removal.id)),
                               removal.is_instance ? R"("nested_instance")" : R"("entity")",
                               bool_to_json(removal.inherited));
    }
    std::string added;
    for(const auto& addition : changes.additions)
    {
        added += fmt::format(R"({}{{"entity_id":{},"name":{},"prefab_instance":{},"inherited":{}}})",
                             added.empty() ? "" : ",",
                             make_json_string(entity_id_string(addition.handle)),
                             make_json_string(entity_name(addition.handle)),
                             bool_to_json(addition.is_instance),
                             bool_to_json(addition.inherited));
    }
    const auto* prefab_comp = changes.root.try_get<prefab_component>();
    return fmt::format(R"({{"instance_id":{},"name":{},"prefab":{},"nested":{},"overrides":[{}],"removed":[{}],)"
                       R"("added":[{}]}})",
                       make_json_string(entity_id_string(changes.root)),
                       make_json_string(entity_name(changes.root)),
                       prefab_comp != nullptr ? asset_handle_key_json(prefab_comp->source) : std::string("null"),
                       bool_to_json(is_nested_instance(changes.root)),
                       overrides,
                       removed,
                       added);
}

/// What a revert drops: whole lists, single overrides, removals brought back. Owners are named
/// by id so a redo after an undo finds the rebuilt instances.
struct revert_plan
{
    std::vector<entt::uhandle> cleared;
    std::vector<std::pair<entt::uhandle, prefab_property_override_data>> overrides;
    std::vector<std::pair<entt::uhandle, prefab_removal_entry>> restores;
};

void apply_revert_plan(const revert_plan& plan)
{
    const auto prefab_of = [](const entt::uhandle& owner) -> prefab_component*
    {
        auto entity = owner.resolve();
        return entity ? entity.try_get<prefab_component>() : nullptr;
    };
    for(const auto& owner : plan.cleared)
    {
        if(auto* prefab_comp = prefab_of(owner))
        {
            revert_local_changes(*prefab_comp);
        }
    }
    for(const auto& [owner, override_data] : plan.overrides)
    {
        if(auto* prefab_comp = prefab_of(owner))
        {
            prefab_comp->remove_override(override_data.entity_uuid, override_data.component_path);
            prefab_comp->changed = true;
        }
    }
    for(const auto& [owner, removal] : plan.restores)
    {
        auto* prefab_comp = prefab_of(owner);
        if(prefab_comp == nullptr)
        {
            continue;
        }
        if(removal.is_instance)
        {
            prefab_comp->local.restore_instance({}, removal.id);
        }
        else
        {
            prefab_comp->local.restore_entity({}, removal.id);
        }
        prefab_comp->changed = true;
    }
}

/// Finds the override an item names - by entity id and path, or label - among the changes.
auto plan_override_revert(const std::vector<prefab_instance_changes>& all_changes,
                          const std::string& entity_id,
                          const std::string& path,
                          revert_plan& plan,
                          std::string& error) -> bool
{
    const auto matches = [&](const prefab_instance_changes& changes, const prefab_property_override_data& override_data)
    {
        const auto entity = scene::find_entity_by_prefab_uuid(changes.root, override_data.entity_uuid);
        return entity_id_string(entity) == entity_id &&
               (override_data.component_path == path || override_data.pretty_component_path == path);
    };
    for(const auto& changes : all_changes)
    {
        for(const auto& override_data : changes.about.local.overrides)
        {
            if(matches(changes, override_data))
            {
                plan.overrides.emplace_back(entt::make_uhandle(changes.root), override_data);
                return true;
            }
        }
        for(const auto& override_data : changes.about.stated.overrides)
        {
            if(matches(changes, override_data))
            {
                error = fmt::format("{} on {} is stated by the prefab that contains this instance; change it there "
                                    "(prefab_open)",
                                    path,
                                    entity_id);
                return false;
            }
        }
    }
    error = fmt::format("No override {} on {} (prefab_get_overrides lists them)", path, entity_id);
    return false;
}

auto plan_removal_restore(const std::vector<prefab_instance_changes>& all_changes,
                          const std::string& removed_id,
                          revert_plan& plan,
                          std::string& error) -> bool
{
    for(const auto& changes : all_changes)
    {
        for(const auto& removal : changes.removals)
        {
            if(hpp::to_string(removal.id) != removed_id)
            {
                continue;
            }
            if(removal.inherited)
            {
                error = "Removal " + removed_id + " is stated by the prefab that contains this instance; restore it there";
                return false;
            }
            plan.restores.emplace_back(entt::make_uhandle(changes.root), removal);
            return true;
        }
    }
    error = "No removal " + removed_id + " (prefab_get_overrides lists them under removed)";
    return false;
}

auto plan_listed_overrides(const std::vector<prefab_instance_changes>& all_changes,
                           const simdjson::dom::array& overrides,
                           revert_plan& plan,
                           std::string& error) -> bool
{
    for(auto element : overrides)
    {
        simdjson::dom::object item;
        std::string entity_id;
        std::string path;
        if(!read_object(element, item, error) || !read_string(item, "entity_id", entity_id) ||
           !read_string(item, "path", path))
        {
            error = "Each overrides item needs entity_id and path";
            return false;
        }
        if(!plan_override_revert(all_changes, entity_id, path, plan, error))
        {
            return false;
        }
    }
    return true;
}

/// The plan for a revert call: the named overrides and removals, or else every list from the
/// instance down (only the instance's own with nested false).
auto build_revert_plan(const std::vector<prefab_instance_changes>& all_changes,
                       const simdjson::dom::object& args,
                       revert_plan& plan,
                       std::string& error) -> bool
{
    simdjson::dom::array overrides;
    const bool has_overrides = !args["overrides"].get(overrides);
    std::vector<std::string> restore_ids;
    if(!read_string_array(args, "restore", restore_ids, error, false))
    {
        return false;
    }
    if(has_overrides && !plan_listed_overrides(all_changes, overrides, plan, error))
    {
        return false;
    }
    for(const auto& removed_id : restore_ids)
    {
        if(!plan_removal_restore(all_changes, removed_id, plan, error))
        {
            return false;
        }
    }
    if(has_overrides || !restore_ids.empty())
    {
        return true;
    }
    bool nested = true;
    read_bool(args, "nested", nested);
    for(const auto& changes : all_changes)
    {
        if(changes.local_count > 0 && (nested || changes.is_self))
        {
            plan.cleared.push_back(entt::make_uhandle(changes.root));
        }
    }
    return true;
}

auto read_file_bytes(const fs::path& path, std::string& out) -> bool
{
    std::ifstream stream(path, std::ios::binary);
    if(!stream)
    {
        return false;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    out = buffer.str();
    return true;
}

void write_file_bytes(const fs::path& path, const std::string& bytes)
{
    fs::error_code ec;
    asset_writer::atomic_write_file(
        path,
        [&bytes](const fs::path& temp)
        {
            std::ofstream stream(temp, std::ios::binary);
            stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        },
        ec);
    if(ec)
    {
        APPLOG_ERROR("Could not restore {}: {}", path.generic_string(), ec.message());
    }
}

/// How many other instances of @p source the scene holds: they follow an apply.
auto count_other_instances(scene& scn, entt::handle instance, const asset_handle<prefab>& source) -> size_t
{
    size_t count = 0;
    scn.registry->view<prefab_component>().each(
        [&](auto entity, const prefab_component& prefab_comp)
        {
            count += entity != instance.entity() && prefab_comp.source == source ? 1 : 0;
        });
    return count;
}

struct apply_outcome
{
    std::string key;
    fs::path file;
    size_t other_instances{};
    std::string error;
};

/// Resolves and checks an apply on the main thread: the instance root, or an empty handle and the
/// reason in @p outcome.
auto prepare_apply(rtti::context& ctx, const std::string& entity_id, apply_outcome& outcome) -> entt::handle
{
    scene* scn = nullptr;
    if(!require_edit_scene(ctx, scn, outcome.error))
    {
        return {};
    }
    auto root = find_instance_root(*scn, entity_id, outcome.error);
    if(!root)
    {
        return {};
    }
    const auto& source = root.get<prefab_component>().source;
    if(!source)
    {
        outcome.error = "The instance's prefab is missing; nothing to apply to";
        return {};
    }
    outcome.key = source.id();
    outcome.file = fs::absolute(fs::resolve_protocol(outcome.key));
    outcome.other_instances = count_other_instances(*scn, root, source);
    return require_project_data_asset(outcome.key, outcome.error) ? root : entt::handle{};
}

} // namespace

void register_prefab_instance_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "prefab_get_overrides",
         .description =
             "What a prefab instance changes from its prefab, for the instance an entity belongs to and every "
             "instance nested in it: overrides (entity_id, path, label), removed entities or nested instances "
             "(id), and entities added under it. inherited: stated by a prefab that contains the instance - "
             "change those in that prefab. On the root of the prefab open in prefab mode: the changes of the "
             "instances nested in it.",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_id":{"type":"string","description":"Any entity of the instance"}},"required":["entity_id"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             scene* scn = nullptr;
             if(!require_active_scene(ctx, scn, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string entity_id;
             read_string(args, "entity_id", entity_id);
             auto entity = resolve_entity(*scn, entity_id, error);
             if(!entity)
             {
                 return {.text = error, .is_error = true};
             }
             const bool is_authoring = is_authoring_root(entity);
             auto root = is_authoring ? entity : find_instance_root(*scn, entity_id, error);
             if(!root)
             {
                 return {.text = error, .is_error = true};
             }
             std::string instances;
             size_t local_count = 0;
             size_t inherited_count = 0;
             size_t added_count = 0;
             for(const auto& changes : collect_prefab_changes(root))
             {
                 const bool is_empty = changes.local_count + changes.inherited_count + changes.added_count == 0;
                 if((changes.is_self && is_authoring) || (!changes.is_self && is_empty))
                 {
                     continue;
                 }
                 local_count += changes.local_count;
                 inherited_count += changes.inherited_count;
                 added_count += changes.added_count;
                 instances += (instances.empty() ? "" : ",") + instance_changes_json(changes);
             }
             return {.text = fmt::format(R"({{"root_id":{},"instances":[{}],"local_count":{},"inherited_count":{},)"
                                         R"("added_count":{}}})",
                                         make_json_string(entity_id_string(root)),
                                         instances,
                                         local_count,
                                         inherited_count,
                                         added_count),
                     .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "prefab_revert_overrides",
         .description =
             "Put a prefab instance back to what its prefab says, as the Inspector's Revert does. Without "
             "overrides or restore: every change made here on the instance and on the instances nested in "
             "it (nested false: the instance's own only). With them: just those overrides "
             "(entity_id + path from prefab_get_overrides) and those removals (ids) brought back. Entities "
             "added here are kept. One undo step (edit_undo).",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_id":{"type":"string","description":"Any entity of the instance"},"overrides":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"path":{"type":"string","description":"path (or label) from prefab_get_overrides"}},"required":["entity_id","path"]}},"restore":{"type":"array","items":{"type":"string"},"description":"Ids of removed entities or nested instances to bring back"},"nested":{"type":"boolean","description":"Without overrides/restore: also revert the nested instances (default true)"}},"required":["entity_id"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             scene* scn = nullptr;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string entity_id;
             read_string(args, "entity_id", entity_id);
             auto root = find_instance_root(*scn, entity_id, error);
             if(!root)
             {
                 return {.text = error, .is_error = true};
             }
             revert_plan plan;
             if(!build_revert_plan(collect_prefab_changes(root), args, plan, error))
             {
                 return {.text = error, .is_error = true};
             }
             const auto count = plan.cleared.size() + plan.overrides.size() + plan.restores.size();
             if(count > 0)
             {
                 do_instance_rewrite(ctx,
                                     "MCP Revert Prefab Overrides",
                                     root,
                                     [plan]()
                                     {
                                         apply_revert_plan(plan);
                                     });
             }
             return {.text = fmt::format(R"({{"ok":true,"instance_id":{},"instances_cleared":{},"overrides_reverted":{},)"
                                         R"("removals_restored":{}}})",
                                         make_json_string(entity_id_string(root)),
                                         plan.cleared.size(),
                                         plan.overrides.size(),
                                         plan.restores.size()),
                     .is_error = false};
         },
         .mutates_scene = true});

    registry.add(
        {.name = "prefab_apply_overrides",
         .description =
             "Write a prefab instance - its overrides, added and removed entities - into its prefab file; every "
             "other instance of that prefab follows (other_instances counts those in this scene). On an instance "
             "nested in another, writes the nested prefab. One undo step: edit_undo puts the file and the "
             "instance back. Waits up to wait_ms (default 5000) for the prefab to reload.",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_id":{"type":"string","description":"Any entity of the instance"},"wait_ms":{"type":"integer","minimum":0,"maximum":30000}},"required":["entity_id"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             std::string entity_id;
             read_string(args, "entity_id", entity_id);
             const auto wait_ms = read_timeout_ms(args, k_apply_wait_default_ms, k_apply_wait_max_ms);
             auto checked = mcp.invoke_on_main(
                 [&ctx, &entity_id]() -> apply_outcome
                 {
                     apply_outcome outcome;
                     prepare_apply(ctx, entity_id, outcome);
                     return outcome;
                 });
             if(!checked || !checked->error.empty())
             {
                 return {.text = checked ? checked->error : "Timed out on the main thread", .is_error = true};
             }
             std::string bytes_before;
             if(!read_file_bytes(checked->file, bytes_before))
             {
                 return {.text = "Could not read " + checked->key, .is_error = true};
             }
             const auto previous = take_asset_snapshot(ctx, {checked->key});
             auto applied = mcp.invoke_on_main(
                 [&ctx, &entity_id, &bytes_before]() -> apply_outcome
                 {
                     apply_outcome outcome;
                     auto root = prepare_apply(ctx, entity_id, outcome);
                     if(!root)
                     {
                         return outcome;
                     }
                     do_instance_rewrite(
                         ctx,
                         "MCP Apply Prefab Overrides",
                         root,
                         [target = entt::make_uhandle(root)]()
                         {
                             apply_instance_to_prefab(target.resolve());
                         },
                         [file = outcome.file, bytes_before]()
                         {
                             write_file_bytes(file, bytes_before);
                         });
                     return outcome;
                 });
             if(!applied || !applied->error.empty())
             {
                 return {.text = applied ? applied->error : "Timed out on the main thread", .is_error = true};
             }
             const auto report = wait_for_assets(ctx, {applied->key}, {true}, wait_ms, previous);
             return {.text = fmt::format(R"({{"ok":true,"prefab":{},"other_instances":{}{}}})",
                                         make_json_string(applied->key),
                                         applied->other_instances,
                                         report.state_fields.front()),
                     .is_error = false};
         },
         .mutates_scene = true,
         .requires_main_thread = false});

    registry.add(
        {.name = "prefab_unlink_batch",
         .description =
             "Unlink prefab instances: each stays as plain entities with every value it has now, no longer "
             "following its prefab (the hierarchy's Unlink from Prefab). Instance roots only; an instance "
             "nested in another is unlinked inside that prefab (prefab_open). One undo step.",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_ids":{"type":"array","items":{"type":"string"},"description":"Instance roots"}},"required":["entity_ids"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             scene* scn = nullptr;
             std::vector<entt::handle> entities;
             if(!require_edit_scene(ctx, scn, error) || !read_entity_ids(args, *scn, entities, error))
             {
                 return {.text = error, .is_error = true};
             }
             for(auto entity : entities)
             {
                 const auto id = entity_id_string(entity);
                 if(!entity.all_of<prefab_component>() || is_authoring_root(entity))
                 {
                     return {.text = "Not a prefab instance root: " + id, .is_error = true};
                 }
                 if(prefab_override_context::find_prefab_root_entity(parent_of(entity)))
                 {
                     return {.text = id + " is nested in another instance; unlink it inside that prefab (prefab_open)",
                             .is_error = true};
                 }
             }
             auto snapshots = std::make_shared<std::vector<entity_subtree_snapshot>>();
             std::vector<entt::uhandle> targets;
             for(auto entity : entities)
             {
                 snapshots->push_back(entity_subtree_snapshot::capture(entity));
                 targets.push_back(entt::make_uhandle(entity));
             }
             ctx.get_cached<editing_manager>().do_action(
                 "MCP Unlink Prefab Instances",
                 [targets]()
                 {
                     for(const auto& target : targets)
                     {
                         if(auto entity = target.resolve())
                         {
                             unlink_from_prefab(entity);
                         }
                     }
                 },
                 [snapshots]()
                 {
                     for(const auto& snapshot : *snapshots)
                     {
                         snapshot.restore();
                     }
                 });
             return {.text = fmt::format(R"({{"ok":true,"unlinked":{}}})", entities.size()), .is_error = false};
         },
         .mutates_scene = true});
}

} // namespace unravel::mcp
