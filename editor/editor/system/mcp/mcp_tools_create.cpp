#include "mcp_asset_lookup.h"
#include "mcp_meta_properties.h"
#include "mcp_project_paths.h"
#include "mcp_tools_common.h"

#include <editor/editing/actions/actions.h>
#include <editor/editing/entity_inspect.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/prefab.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/light.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/reflection_probe.h>

#include <array>
#include <string>
#include <vector>

namespace unravel::mcp
{
namespace
{

/// Material slots a create item may address; a model never has this many, and the bound keeps a
/// typo from growing the slot list by millions.
constexpr int64_t k_max_material_slots = 64;

/// One validated item of scene_create_batch: everything is resolved before anything is created.
struct create_item
{
    std::string name;
    entt::handle parent;
    transform_snapshot pose;
    bool is_local{};
    std::string primitive;
    std::string asset_key;
    light_type light{light_type::point};
    probe_type probe{probe_type::sphere};
    asset_handle<material> material;
    uint32_t material_index{};
};

using create_fn = entt::handle (*)(rtti::context&, scene&, const create_item&);

/// What an item of a kind needs besides the common fields, so stray fields are reported.
enum class create_input : uint8_t
{
    none,
    primitive,
    asset,
    light_type,
    probe_type,
};

/// One entry of the hierarchy's Create menu: the same defaults function makes it, so an entity
/// made here is the entity the menu makes.
struct create_kind
{
    const char* name;
    create_input input;
    create_fn create;
};

auto enum_pretty_name(const entt::meta_any& value) -> std::string
{
    for(auto [id, candidate] : value.type().data())
    {
        if(candidate.get({}) == value)
        {
            return entt::get_pretty_name(candidate);
        }
    }
    return {};
}

const std::array<create_kind, 12> k_create_kinds{{
    {"empty",
     create_input::none,
     [](rtti::context&, scene& scn, const create_item&)
     {
         return scn.create_entity();
     }},
    {"primitive",
     create_input::primitive,
     [](rtti::context& ctx, scene& scn, const create_item& item)
     {
         return defaults::create_embedded_mesh_entity(ctx, scn, item.primitive);
     }},
    {"mesh",
     create_input::asset,
     [](rtti::context& ctx, scene& scn, const create_item& item)
     {
         return defaults::create_mesh_entity_at(ctx, scn, item.asset_key);
     }},
    {"prefab",
     create_input::asset,
     [](rtti::context& ctx, scene& scn, const create_item& item)
     {
         return defaults::create_prefab_at(ctx, scn, item.asset_key);
     }},
    {"light",
     create_input::light_type,
     [](rtti::context& ctx, scene& scn, const create_item& item)
     {
         return defaults::create_light_entity(ctx, scn, item.light, enum_pretty_name(entt::meta_any{item.light}));
     }},
    {"reflection_probe",
     create_input::probe_type,
     [](rtti::context& ctx, scene& scn, const create_item& item)
     {
         return defaults::create_reflection_probe_entity(ctx,
                                                         scn,
                                                         item.probe,
                                                         enum_pretty_name(entt::meta_any{item.probe}));
     }},
    {"camera",
     create_input::none,
     [](rtti::context& ctx, scene& scn, const create_item&)
     {
         return defaults::create_camera_entity(ctx, scn, "Camera");
     }},
    {"volume",
     create_input::none,
     [](rtti::context& ctx, scene& scn, const create_item&)
     {
         return defaults::create_volume_entity(ctx, scn, "Volume");
     }},
    {"audio_source",
     create_input::none,
     [](rtti::context& ctx, scene& scn, const create_item&)
     {
         return defaults::create_audio_source_entity(ctx, scn, "Audio Source");
     }},
    {"particle_emitter",
     create_input::none,
     [](rtti::context& ctx, scene& scn, const create_item&)
     {
         return defaults::create_particle_emitter_entity(ctx, scn, "Particle Emitter");
     }},
    {"text",
     create_input::none,
     [](rtti::context& ctx, scene& scn, const create_item&)
     {
         return defaults::create_text_entity(ctx, scn, "Text");
     }},
    {"ui_document",
     create_input::none,
     [](rtti::context& ctx, scene& scn, const create_item&)
     {
         return defaults::create_ui_document_entity(ctx, scn, "UI Document");
     }},
}};

auto find_create_kind(const std::string& name) -> const create_kind*
{
    const auto found = std::find_if(k_create_kinds.begin(),
                                    k_create_kinds.end(),
                                    [&](const create_kind& kind)
                                    {
                                        return name == kind.name;
                                    });
    return found == k_create_kinds.end() ? nullptr : &*found;
}

auto create_kind_names() -> std::vector<std::string>
{
    std::vector<std::string> names;
    for(const auto& kind : k_create_kinds)
    {
        names.emplace_back(kind.name);
    }
    return names;
}

/// A reflected enum value an item names, or its default when the field is absent.
template<typename Enum>
auto read_enum_field(const simdjson::dom::object& obj, const char* key, Enum& out, std::string& error) -> bool
{
    std::string name;
    if(!read_string(obj, key, name))
    {
        return true;
    }
    entt::meta_any value;
    if(!find_meta_enum_value(entt::resolve<Enum>(), name, value, error))
    {
        error = std::string(key) + ": " + error;
        return false;
    }
    out = value.cast<Enum>();
    return true;
}

/// Fields only some kinds take; any of them on another kind is a mistake worth reporting.
auto check_kind_fields(const simdjson::dom::object& obj, const create_kind& kind, std::string& error) -> bool
{
    const std::array<std::pair<const char*, create_input>, 4> owned{{{"primitive", create_input::primitive},
                                                                     {"asset", create_input::asset},
                                                                     {"light_type", create_input::light_type},
                                                                     {"probe_type", create_input::probe_type}}};
    for(const auto& [field, input] : owned)
    {
        simdjson::dom::element present;
        if(!obj[field].get(present) && kind.input != input)
        {
            error = std::string(field) + " does not apply to kind " + kind.name;
            return false;
        }
    }
    simdjson::dom::element material;
    if(!obj["material"].get(material) && kind.input != create_input::primitive &&
       std::string(kind.name) != "mesh")
    {
        error = std::string("material applies to kinds primitive and mesh, not ") + kind.name;
        return false;
    }
    return true;
}

/// The kind-specific inputs of an item, each checked against what exists.
auto read_kind_inputs(rtti::context& ctx,
                      const simdjson::dom::object& obj,
                      const create_kind& kind,
                      create_item& item,
                      std::string& error) -> bool
{
    switch(kind.input)
    {
        case create_input::primitive:
            if(!read_string(obj, "primitive", item.primitive) || !defaults::has_embedded_mesh(ctx, item.primitive))
            {
                error = "primitive must name an embedded mesh (assets_list_embedded_primitives): " + item.primitive;
                return false;
            }
            return true;
        case create_input::asset:
        {
            std::string asset;
            if(!read_string(obj, "asset", asset) || asset.empty())
            {
                error = std::string("kind ") + kind.name + " needs asset (a key or uid)";
                return false;
            }
            const bool is_mesh = std::string(kind.name) == "mesh";
            item.asset_key = is_mesh ? find_existing_asset<mesh>(ctx, to_asset_key(asset), error).id()
                                     : find_existing_asset<prefab>(ctx, to_asset_key(asset), error).id();
            return !item.asset_key.empty();
        }
        case create_input::light_type:
            return read_enum_field(obj, "light_type", item.light, error);
        case create_input::probe_type:
            return read_enum_field(obj, "probe_type", item.probe, error);
        case create_input::none:
            return true;
    }
    return true;
}

auto read_material(rtti::context& ctx, const simdjson::dom::object& obj, create_item& item, std::string& error) -> bool
{
    std::string material_key;
    if(!read_string(obj, "material", material_key))
    {
        return true;
    }
    item.material = find_existing_asset<material>(ctx, to_asset_key(material_key), error);
    if(!item.material)
    {
        return false;
    }
    const int64_t index = read_int64_or(obj, "material_index", 0);
    if(index < 0 || index >= k_max_material_slots)
    {
        error = fmt::format("material_index must be 0..{}", k_max_material_slots - 1);
        return false;
    }
    item.material_index = static_cast<uint32_t>(index);
    return true;
}

/// Reads and checks one item; false with `error` naming the item's problem.
auto read_create_item(rtti::context& ctx,
                      scene& scn,
                      const simdjson::dom::object& obj,
                      const create_kind*& kind,
                      create_item& item,
                      std::string& error) -> bool
{
    std::string kind_name;
    read_string(obj, "kind", kind_name);
    kind = find_create_kind(kind_name);
    if(kind == nullptr)
    {
        error = "kind must be one of " + fmt::format("{}", fmt::join(create_kind_names(), ", "));
        return false;
    }
    std::string parent_id;
    if(read_string(obj, "parent_id", parent_id) && !parent_id.empty())
    {
        item.parent = find_entity(scn, parent_id);
        if(!item.parent)
        {
            error = "parent_id not found: " + parent_id;
            return false;
        }
    }
    read_string(obj, "name", item.name);
    read_transform_snapshot(obj, item.pose);
    return read_transform_space(obj, item.is_local, error) && check_kind_fields(obj, *kind, error) &&
           read_kind_inputs(ctx, obj, *kind, item, error) && read_material(ctx, obj, item, error);
}

/// Names, parents, places and paints a new entity as its item asks. The parent keeps the entity's
/// local pose, as the hierarchy's create-under does; the item's pose is then applied in its space.
void place_created(entt::handle entity, const create_item& item)
{
    if(!item.name.empty())
    {
        entity.get_or_emplace<tag_component>().name = item.name;
        prefab_override_context::mark_property_as_changed(entity, entt::resolve<tag_component>(), "name");
    }
    if(item.parent)
    {
        entity.get<transform_component>().set_parent(item.parent, false);
    }
    apply_pose_direct(entity, item.pose, item.is_local);
    if(item.material && entity.all_of<model_component>())
    {
        auto& model_comp = entity.get<model_component>();
        auto model = model_comp.get_model();
        model.set_shared_material(item.material, item.material_index);
        model_comp.set_model(model);
        prefab_override_context::mark_material_as_changed(entity);
    }
}

auto created_to_json(const std::vector<entt::handle>& created) -> std::string
{
    std::string json = "[";
    for(size_t i = 0; i < created.size(); ++i)
    {
        json += (i == 0 ? "" : ",") + entity_to_lean_json(created[i], true);
    }
    return json + "]";
}

auto create_schema() -> std::string
{
    const auto enum_json = [](const std::vector<std::string>& names)
    {
        return strings_to_json_array(names);
    };
    const std::string vec3 = R"({"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3})";
    return fmt::format(
        R"json({{"type":"object","properties":{{"items":{{"type":"array","items":{{"type":"object","properties":{{)json"
        R"json("kind":{{"type":"string","enum":{}}},)json"
        R"json("name":{{"type":"string","description":"Entity name; default the name the Create menu gives"}},)json"
        R"json("parent_id":{{"type":"string"}},)json"
        R"json("space":{{"type":"string","enum":["world","local"],"description":"Space of position/rotation_euler/scale; default world"}},)json"
        R"json("position":{},"rotation_euler":{},"scale":{},)json"
        R"json("primitive":{{"type":"string","description":"kind primitive: embedded mesh name (assets_list_embedded_primitives)"}},)json"
        R"json("asset":{{"type":"string","description":"kind mesh / prefab: asset key or uid"}},)json"
        R"json("light_type":{{"type":"string","enum":{},"description":"kind light; default point"}},)json"
        R"json("probe_type":{{"type":"string","enum":{},"description":"kind reflection_probe; default sphere"}},)json"
        R"json("material":{{"type":"string","description":"kinds primitive / mesh: material key or uid for material_index"}},)json"
        R"json("material_index":{{"type":"integer","minimum":0,"maximum":{}}})json"
        R"json(}},"required":["kind"]}}}}}},"required":["items"]}})json",
        enum_json(create_kind_names()),
        vec3,
        vec3,
        vec3,
        enum_json(meta_enum_names(entt::resolve<light_type>())),
        enum_json(meta_enum_names(entt::resolve<probe_type>())),
        k_max_material_slots - 1);
}

} // namespace

void register_create_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "scene_create_batch",
         .description =
             "Create entities of any Create-menu kind in one undo step. Every item is checked first: one bad item "
             "creates nothing. Returns created [{id, name, parent_id}] in item order; configure components "
             "afterwards with scene_set_component_properties_batch.",
         .input_schema_json = create_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             simdjson::dom::array items_arr;
             if(!require_edit_scene(ctx, scn, error) || !read_required_array(args, "items", items_arr, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::vector<std::pair<const create_kind*, create_item>> items;
             for(auto element : items_arr)
             {
                 simdjson::dom::object obj;
                 const create_kind* kind = nullptr;
                 create_item item;
                 if(!read_object(element, obj, error) || !read_create_item(ctx, *scn, obj, kind, item, error))
                 {
                     return {.text = fmt::format("Item {}: {}; nothing was created", items.size(), error),
                             .is_error = true};
                 }
                 items.emplace_back(kind, std::move(item));
             }
             if(items.empty())
             {
                 return {.text = "items array is empty", .is_error = true};
             }
             std::vector<entt::handle> created;
             ctx.get_cached<editing_manager>().do_action<create_entities_action_t>(
                 "MCP Create",
                 [&]()
                 {
                     created.clear();
                     for(const auto& [kind, item] : items)
                     {
                         if(auto entity = kind->create(ctx, *scn, item))
                         {
                             place_created(entity, item);
                             created.push_back(entity);
                         }
                     }
                     return created;
                 });
             return {.text = fmt::format(R"({{"created":{},"count":{},"requested":{}}})",
                                         created_to_json(created),
                                         created.size(),
                                         items.size()),
                     .is_error = created.size() != items.size()};
         },
         .mutates_scene = true});
}

} // namespace unravel::mcp
