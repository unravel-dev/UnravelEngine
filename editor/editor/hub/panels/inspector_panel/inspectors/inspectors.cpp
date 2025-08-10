#include "inspectors.h"
#include "editor/hub/panels/entity_panel.h"
#include "editor/imgui/integration/fonts/icons/icons_material_design_icons.h"
#include "editor/imgui/integration/imgui.h"
#include "property_path_generator.h"


#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "string_utils/utils.h"
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/engine.h>
#include <engine/meta/ecs/entity.hpp>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/text_component.h>
#include <engine/scripting/ecs/components/script_component.h>
#include <functional>
#include <unordered_map>
#include <vector>


namespace unravel
{
namespace
{
int debug_view{0};
}
inspector_registry::inspector_registry()
{
    auto inspector_types = rttr::type::get<inspector>().get_derived_classes();
    for(auto& inspector_type : inspector_types)
    {
        auto inspected_type_var = inspector_type.get_metadata("inspected_type");
        if(inspected_type_var)
        {
            auto inspected_type = inspected_type_var.get_value<rttr::type>();
            auto inspector_var = inspector_type.create();
            if(inspector_var)
            {
                type_map[inspected_type] = inspector_var.get_value<std::shared_ptr<inspector>>();
            }
        }
    }
}

void push_debug_view()
{
    debug_view++;
}
void pop_debug_view()
{
    debug_view--;
}
auto is_debug_view() -> bool
{
    return debug_view > 0;
}

auto prefab_override_context::begin_prefab_inspection(entt::handle entity) -> bool
{
    end_prefab_inspection();

    auto prefab_root = find_prefab_root_entity(entity);

    if(prefab_root)
    {
        auto prefab_comp = prefab_root.try_get<prefab_component>();
        if(prefab_comp)
        {
            // Initialize override tracking for this prefab instance
            prefab_root_entity = prefab_root;
            is_active = true;

            is_path_overridden_callback =
                [comp_copy = *prefab_comp, entity_copy = entity](const hpp::uuid& entity_uuid,
                                                                 const std::string& component_path)
            {
                return comp_copy.has_override(entity_uuid, component_path);
            };

            // Get and set the entity UUID for both path contexts
            auto entity_uuid = get_entity_prefab_uuid(entity);

            set_entity_uuid(entity_uuid);

            return true;
        }
    }

    return false;
}

void prefab_override_context::end_prefab_inspection()
{
    is_active = false;
    prefab_root_entity = {};
    path_context = {};
    pretty_path_context = {};
}

void prefab_override_context::record_override()
{
    if(!is_active || !prefab_root_entity)
    {
        return;
    }

    auto* prefab_comp = prefab_root_entity.try_get<prefab_component>();
    if(prefab_comp)
    {
        // Get the entity UUID and component paths
        auto entity_uuid = path_context.get_entity_uuid();

        if(exists_in_prefab(prefab_scene,
                            prefab_comp->source,
                            entity_uuid,
                            path_context.get_component_type_name(),
                            path_context.get_current_path()))
        {
            // Build technical component path: "component_type/property_path"
            std::string component_path = path_context.get_current_path_with_component_type();
            // Build pretty component path: "PrettyComponentType/PrettyPropertyPath"
            std::string pretty_component_path = pretty_path_context.get_current_path_with_component_type();
            // Add the new override with both technical and pretty paths
            prefab_comp->add_override(entity_uuid, component_path, pretty_component_path);
            prefab_comp->changed = true;
        }
    }
}

void prefab_override_context::set_entity_uuid(const hpp::uuid& uuid)
{
    if(!is_active)
    {
        return;
    }
    path_context.set_entity_uuid(uuid);
    pretty_path_context.set_entity_uuid(uuid);
}

void prefab_override_context::set_component_type(const std::string& type, const std::string& pretty_type)
{
    if(!is_active)
    {
        return;
    }

    path_context.set_component_type(type);
    pretty_path_context.set_component_type(pretty_type);
}

void prefab_override_context::push_segment(const std::string& segment, const std::string& pretty_segment)
{
    if(!is_active)
    {
        return;
    }

    path_context.push_segment(segment);
    pretty_path_context.push_segment(pretty_segment);

    if(is_path_overridden())
    {
        ImGui::PushFont(ImGui::Font::Bold);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    }
}

void prefab_override_context::pop_segment()
{
    if(!is_active)
    {
        return;
    }

    if(is_path_overridden())
    {
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    path_context.pop_segment();
    pretty_path_context.pop_segment();
}

/**
 * @brief Finds the prefab root entity by traversing up the parent hierarchy
 * @param entity The entity to start searching from
 * @return A handle to the entity with prefab_component, or empty handle if not found
 */
auto prefab_override_context::find_prefab_root_entity(entt::handle entity) -> entt::handle
{
    if(!entity)
    {
        return {};
    }

    auto current_entity = entity;

    // Traverse up the parent hierarchy looking for a prefab_component
    while(current_entity)
    {
        if(current_entity.try_get<prefab_component>())
        {
            return current_entity;
        }

        // Move to parent entity
        auto* transform = current_entity.try_get<transform_component>();
        if(!transform)
        {
            break;
        }

        current_entity = transform->get_parent();
    }

    return {}; // No prefab component found in hierarchy
}
/**
 * @brief Get the UUID of an entity from its id_component
 * @param entity The entity to get the UUID from
 * @return The UUID as a string, or empty string if entity has no id_component
 */
auto prefab_override_context::get_entity_prefab_uuid(entt::handle entity) -> hpp::uuid
{
    if(!entity)
    {
        return hpp::uuid{};
    }

    auto* id_comp = entity.try_get<prefab_id_component>();
    if(!id_comp)
    {
        return hpp::uuid{};
    }

    return id_comp->id;
}
void prefab_override_context::mark_transform_as_changed(entt::handle entity,
                                                        bool position,
                                                        bool rotation,
                                                        bool scale,
                                                        bool skew)
{
    if(position)
    {
        mark_property_as_changed(entity, rttr::type::get<transform_component>(), "local_transform/position");
    }
    if(rotation)
    {
        mark_property_as_changed(entity, rttr::type::get<transform_component>(), "local_transform/rotation");
    }
    if(scale)
    {
        mark_property_as_changed(entity, rttr::type::get<transform_component>(), "local_transform/scale");
    }
    if(skew)
    {
        mark_property_as_changed(entity, rttr::type::get<transform_component>(), "local_transform/skew");
    }
}

void prefab_override_context::mark_active_as_changed(entt::handle entity)
{
    mark_property_as_changed(entity, rttr::type::get<transform_component>(), "active");
}

void prefab_override_context::mark_text_area_as_changed(entt::handle entity)
{
    mark_property_as_changed(entity, rttr::type::get<text_component>(), "area");
}

void prefab_override_context::mark_material_as_changed(entt::handle entity)
{
    mark_property_as_changed(entity, rttr::type::get<model_component>(), "model/materials");
}

void prefab_override_context::mark_property_as_changed(entt::handle entity,
                                                       const rttr::type& component_type,
                                                       const std::string& property_path)
{
    auto prefab_root = find_prefab_root_entity(entity);
    if(prefab_root)
    {
        auto prefab_comp = prefab_root.try_get<prefab_component>();
        auto entity_uuid = get_entity_prefab_uuid(entity);
        auto component_type_name = component_type.get_name().to_string();
        auto component_pretty_type_name = rttr::get_pretty_name(component_type);

        auto& ctx = engine::context();
        auto& prefab_override_ctx = ctx.get_cached<prefab_override_context>();
        if(exists_in_prefab(prefab_override_ctx.prefab_scene,
                            prefab_comp->source,
                            entity_uuid,
                            component_type_name,
                            property_path))
        {
            auto tokens = string_utils::tokenize(property_path, "/");
            std::string segment;

            auto type = component_type;
            std::string current_path = component_type_name;
            std::string current_pretty_path = component_pretty_type_name;

            for(auto& token : tokens)
            {
                auto prop = type.get_property(token);
                auto prop_type = prop.get_type();

                current_path += "/" + token;
                current_pretty_path += "/" + rttr::get_pretty_name(prop);

                type = prop_type;

            }

            prefab_comp->add_override(entity_uuid, current_path, current_pretty_path);
        }
    }
}

void prefab_override_context::mark_entity_as_removed(entt::handle entity)
{
    auto prefab_root = find_prefab_root_entity(entity);
    if(prefab_root)
    {
        auto prefab_comp = prefab_root.try_get<prefab_component>();
        if(prefab_comp)
        {
            auto entity_uuid = get_entity_prefab_uuid(entity);
            if(entity_uuid.is_nil())
            {
                return;
            }

            prefab_comp->remove_entity(entity_uuid);
        }
    }
}

auto prefab_override_context::exists_in_prefab(scene& cache_scene,
                                               const asset_handle<prefab>& prefab,
                                               hpp::uuid entity_uuid,
                                               const std::string& component_type,
                                               const std::string& property_path) -> bool
{
    if(!prefab)
    {
        return false;
    }

    if(entity_uuid.is_nil())
    {
        return false;
    }

    // return true;

    rttr::type type = rttr::type::get_by_name(component_type);
    if(!type.is_valid())
    {
        return false;
    }

    auto method = type.get_method("component_exists");
    if(!method.is_valid())
    {
        return false;
    }

    entt::handle instance;
    auto view = cache_scene.registry->view<prefab_component>();
    view.each(
        [&](auto e, auto&& comp)
        {
            if(comp.source == prefab)
            {
                instance = cache_scene.create_handle(e);
            }
        });
    if(!instance)
    {
        cache_scene.unload();
        instance = cache_scene.instantiate(prefab);
    }

    auto entity = scene::find_entity_by_prefab_uuid(instance, entity_uuid);
    if(!entity)
    {
        return false;
    }

    auto result = method.invoke({}, entity);

    bool exists = result.get_value<bool>();
    if(!exists)
    {
        return false;
    }

    if(type == rttr::type::get<script_component>())
    {
        auto script_comp = entity.try_get<script_component>();
        if(script_comp)
        {
            auto tokens = string_utils::tokenize(property_path, "/");
            if(tokens.size() > 1 && tokens[0] == "script_components")
            {
                auto script_name = tokens[1];
                return script_comp->has_script_components(script_name);
            }
        }
    }

    return true;
}

auto get_inspector(rtti::context& ctx, const rttr::type& type) -> std::shared_ptr<inspector>
{
    auto& registry = ctx.get_cached<inspector_registry>();
    return registry.type_map[type];
}

auto are_property_values_equal(const rttr::variant& val1, const rttr::variant& val2) -> bool
{
    if(!val1.is_valid() || !val2.is_valid())
    {
        return val1.is_valid() == val2.is_valid();
    }

    if(val1.get_type() != val2.get_type())
    {
        return false;
    }

    return val1 == val2;
}

auto is_property_visible(rttr::instance& object, const rttr::property& prop) -> bool
{
    auto predicate_meta = prop.get_metadata("predicate");
    if(predicate_meta.can_convert<std::function<bool(rttr::instance&)>>())
    {
        auto pred = predicate_meta.get_value<std::function<bool(rttr::instance&)>>();
        return pred(object);
    }

    return true;
}

auto is_property_readonly(rttr::instance& object, const rttr::property& prop) -> bool
{
    auto predicate_meta = prop.get_metadata("readonly_predicate");
    if(predicate_meta.can_convert<std::function<bool(rttr::instance&)>>())
    {
        auto pred = predicate_meta.get_value<std::function<bool(rttr::instance&)>>();
        return pred(object);
    }

    return false;
}

auto is_property_flattable(rttr::instance& object, const rttr::property& prop) -> bool
{
    auto predicate_meta = prop.get_metadata("flattable");
    if(predicate_meta.can_convert<bool>())
    {
        auto pred = predicate_meta.get_value<bool>();
        return pred;
    }

    return false;
}

auto inspect_property(rtti::context& ctx, rttr::instance& object, const rttr::property& prop) -> inspect_result
{
    if(!is_property_visible(object, prop))
    {
        return {};
    }

    inspect_result result{};
    auto prop_var = prop.get_value(object);
    bool is_readonly = prop.is_readonly() || is_property_readonly(object, prop);
    bool is_flattable = is_property_flattable(object, prop);
    bool is_array = prop_var.is_sequential_container();
    bool is_associative_container = prop_var.is_associative_container();
    bool is_enum = prop.is_enumeration();
    rttr::instance prop_object = prop_var;
    auto prop_type = prop_object.get_derived_type();
    auto prop_inspector = get_inspector(ctx, prop_type);
    auto pretty_name = rttr::get_pretty_name(prop);

    is_readonly |= ImGui::IsReadonly();

    var_info info;
    info.read_only = is_readonly;
    info.is_property = true;

    // Push property name to path context for flattable properties too
    auto& override_ctx = ctx.get_cached<prefab_override_context>();
    override_ctx.push_segment(prop.get_name().to_string(), pretty_name);

    if(prop_inspector)
    {
        prop_inspector->before_inspect(prop);
    }

    ImGui::PushReadonly(is_readonly);

    {
        auto get_meta = [&prop](const rttr::variant& name) -> rttr::variant
        {
            return prop.get_metadata(name);
        };
        if(is_array)
        {
            result |= inspect_array(ctx, prop_var, prop, info, get_meta);
        }
        else if(is_associative_container)
        {
            result |= inspect_associative_container(ctx, prop_var, prop, info, get_meta);
        }
        else if(is_enum)
        {
            auto enumeration = prop.get_enumeration();
            property_layout layout(prop);
            result |= inspect_enum(ctx, prop_var, enumeration, info);
        }
        else
        {
            if(prop_inspector)
            {
                result |= inspect_var(ctx, prop_var, info, get_meta);
            }
            else if(!is_flattable)
            {
                ImGui::AlignTextToFramePadding();
                ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);

                bool open = ImGui::TreeNode(pretty_name.c_str());

                if(open)
                {
                    ImGui::TreePush(pretty_name.c_str());

                    result |= inspect_var(ctx, prop_var, info, get_meta);

                    ImGui::TreePop();
                    ImGui::TreePop();
                }
            }
            else
            {
                ImGui::PushID(pretty_name.c_str());

                result |= inspect_var(ctx, prop_var, info, get_meta);

                ImGui::PopID();
            }
        }
    }

    if(result.changed && !is_readonly)
    {
        prop.set_value(object, prop_var);

        override_ctx.record_override();
    }

    // ImGui::PopEnabled();
    ImGui::PopReadonly();

    if(prop_inspector)
    {
        prop_inspector->after_inspect(prop);
    }

    override_ctx.pop_segment();

    return result;
}

auto inspect_array(rtti::context& ctx,
                   rttr::variant& var,
                   const rttr::property& prop,
                   const var_info& info,
                   const inspector::meta_getter& get_metadata) -> inspect_result
{
    auto name = rttr::get_pretty_name(prop);

    std::string tooltip{};
    auto meta_tooltip = prop.get_metadata("tooltip");
    if(meta_tooltip)
    {
        tooltip = meta_tooltip.get_value<std::string>();
    }

    return inspect_array(ctx, var, name, tooltip, info, get_metadata);
}

auto inspect_array(rtti::context& ctx,
                   rttr::variant& var,
                   const std::string& name,
                   const std::string& tooltip,
                   const var_info& info,
                   const inspector::meta_getter& get_metadata) -> inspect_result
{
    auto view = var.create_sequential_view();
    auto size = view.get_size();
    inspect_result result{};
    auto int_size = static_cast<int>(size);

    ImGui::BeginGroup();
    property_layout layout;
    layout.set_data(name, tooltip);

    bool open = layout.push_tree_layout();

    auto readonly_count_var = get_metadata("readonly_count");
    int readonly_count = 0;
    if(readonly_count_var && readonly_count_var.can_convert<int>())
    {
        readonly_count = readonly_count_var.convert<int>();
    }

    {
        ImGuiInputTextFlags flags = 0;
        int step = 1;
        int step_fast = 100;
        bool readonly = info.read_only || !view.is_dynamic();
        if(readonly)
        {
            step = 0;
            step_fast = 0;
            flags |= ImGuiInputTextFlags_ReadOnly;
        }

        ImGui::PushReadonly(readonly);

        if(ImGui::InputInt("##array", &int_size, step, step_fast, flags))
        {
            int_size = std::max(readonly_count, int_size);
            size = static_cast<std::size_t>(int_size);
            result.changed |= view.set_size(size);
            result.edit_finished = true;
        }
        ImGui::PopReadonly();

        ImGui::DrawItemActivityOutline();
    }

    if(open)
    {
        layout.pop_layout();

        ImGui::TreePush("array");

        int index_to_remove = -1;
        for(std::size_t i = 0; i < size; ++i)
        {
            auto value = view.get_value(i).extract_wrapped_value();
            std::string element = "Element ";
            element += std::to_string(i);

            ImGui::Separator();

            auto item_info = info;
            item_info.read_only |= readonly_count > 0;
            readonly_count--;

            // ImGui::SameLine();
            auto pos_before = ImGui::GetCursorPos();
            {
                property_layout layout;
                layout.set_data(element, {}, true);
                layout.push_tree_layout(ImGuiTreeNodeFlags_Leaf);
                ImGui::PushReadonly(item_info.read_only);

                // Track array index in property path
                // auto& override_ctx = ctx.get_cached<prefab_override_context>();
                // if(override_ctx.is_active)
                // {
                //     auto array_index_segment = "[" + std::to_string(i) + "]";
                //     override_ctx.path_context.push_segment(array_index_segment);
                //     override_ctx.pretty_path_context.push_segment(array_index_segment);
                // }

                result |= inspect_var(ctx, value, item_info, get_metadata);

                // Pop array index from property path
                // if(override_ctx.is_active)
                // {
                //     override_ctx.path_context.pop_segment();
                //     override_ctx.pretty_path_context.pop_segment();
                // }

                ImGui::PopReadonly();
            }
            auto pos_after = ImGui::GetCursorPos();

            if(result.changed)
            {
                view.set_value(i, value);
            }

            if(!item_info.read_only && view.is_dynamic())
            {
                ImGui::SetCursorPos(pos_before);

                ImGui::PushID(i);
                ImGui::AlignTextToFramePadding();
                if(ImGui::Button(ICON_MDI_DELETE, ImVec2(0.0f, ImGui::GetFrameHeightWithSpacing())))
                {
                    index_to_remove = i;
                }
                ImGui::SetItemTooltipEx("Remove element.");
                ImGui::PopID();
                ImGui::SetCursorPos(pos_after);
                ImGui::Dummy({});
            }
        }

        if(index_to_remove != -1)
        {
            view.erase(view.begin() + index_to_remove);
            result.changed = true;
            result.edit_finished = true;
        }

        ImGui::TreePop();
    }
    ImGui::EndGroup();
    ImGui::RenderFrameEx(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

    return result;
}

auto inspect_associative_container(rtti::context& ctx,
                                   rttr::variant& var,
                                   const rttr::property& prop,
                                   const var_info& info,
                                   const inspector::meta_getter& get_metadata) -> inspect_result
{
    auto view = var.create_associative_view();
    auto size = view.get_size();
    auto int_size = static_cast<int>(size);

    inspect_result result{};

    // property_layout layout;
    // layout.set_data(prop);

    // bool open = true;
    // {
    //     open = layout.push_tree_layout();
    //     {
    //         ImGuiInputTextFlags flags = 0;

    //         if(info.read_only)
    //         {
    //             flags |= ImGuiInputTextFlags_ReadOnly;
    //         }

    //         if(ImGui::InputInt("##assoc", &int_size, 1, 100, flags))
    //         {
    //             if(int_size < 0)
    //                 int_size = 0;
    //             size = static_cast<std::size_t>(int_size);
    //             result.changed |= view.insert(view.get_key_type().create()).second;
    //             result.edit_finished = true;
    //         }

    //         ImGui::DrawItemActivityOutline();
    //     }
    // }

    // if(open)
    // {
    //     layout.pop_layout();

    //     int i = 0;
    //     int index_to_remove = -1;
    //     rttr::argument key_to_remove{};
    //     for(const auto& item : view)
    //     {
    //         auto key = item.first.extract_wrapped_value();
    //         auto value = item.second.extract_wrapped_value();

    //         ImGui::Separator();

    //         // ImGui::SameLine();
    //         auto pos_before = ImGui::GetCursorPos();
    //         {
    //             property_layout layout;
    //             layout.set_data(key.to_string(), {}, true);
    //             layout.push_tree_layout(ImGuiTreeNodeFlags_Leaf);

    //             result |= inspect_var(ctx, value, info, get_metadata);
    //         }
    //         auto pos_after = ImGui::GetCursorPos();

    //         // if(result.changed)
    //         //     view.set_value(i, value);

    //         if(!info.read_only)
    //         {
    //             ImGui::SetCursorPos(pos_before);

    //             ImGui::PushID(i);
    //             ImGui::AlignTextToFramePadding();
    //             if(ImGui::Button(ICON_MDI_DELETE, ImVec2(0.0f, ImGui::GetFrameHeightWithSpacing())))
    //             {
    //                 key_to_remove = key;
    //                 index_to_remove = i;
    //             }
    //             ImGui::SetItemTooltipCurrentViewport("Remove element.");
    //             ImGui::PopID();
    //             ImGui::SetCursorPos(pos_after);
    //             ImGui::Dummy({});

    //         }

    //         i++;
    //     }

    //     if(index_to_remove != -1)
    //     {
    //         view.erase(key_to_remove);
    //         result.changed = true;
    //         result.edit_finished = true;
    //     }
    // }

    return result;
}

auto inspect_enum(rtti::context& ctx,
                  rttr::variant& var,
                  rttr::enumeration& data,
                  const var_info& info) -> inspect_result
{
    auto current_name = data.value_to_name(var);

    auto strings = data.get_names();
    std::vector<const char*> cstrings{};
    cstrings.reserve(strings.size());

    int current_idx = 0;
    int i = 0;
    for(const auto& string : strings)
    {
        cstrings.push_back(string.data());

        if(current_name == string)
        {
            current_idx = i;
        }
        i++;
    }

    inspect_result result{};

    if(info.read_only)
    {
        ImGui::LabelText("##enum", "%s", cstrings[current_idx]);
    }
    else
    {
        int listbox_item_size = static_cast<int>(cstrings.size());

        ImGuiComboFlags flags = 0;

        if(ImGui::BeginCombo("##enum", cstrings[current_idx], flags))
        {
            for(int n = 0; n < listbox_item_size; n++)
            {
                const bool is_selected = (current_idx == n);

                if(ImGui::Selectable(cstrings[n], is_selected))
                {
                    current_idx = n;
                    result.changed = true;
                    result.edit_finished |= true;
                    var = data.name_to_value(cstrings[current_idx]);
                }

                ImGui::DrawItemActivityOutline();

                if(is_selected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }
        ImGui::DrawItemActivityOutline();
    }

    return result;
}

auto refresh_inspector(rtti::context& ctx, rttr::type type) -> void
{
    auto& registry = ctx.get_cached<inspector_registry>();

    // Find the inspector for the given type
    auto it = registry.type_map.find(type);
    if(it != registry.type_map.end())
    {
        // Call refresh on the inspector
        it->second->refresh(ctx);
    }
}

auto get_meta_empty(const rttr::variant& other) -> rttr::variant
{
    return rttr::variant();
}

auto inspect_var(rtti::context& ctx,
                 rttr::variant& var,
                 const var_info& info,
                 const inspector::meta_getter& get_metadata) -> inspect_result
{
    rttr::instance object = var;
    auto type = object.get_derived_type();

    inspect_result result{};

    ImGui::PushReadonly(info.read_only);

    auto inspector = get_inspector(ctx, type);
    if(inspector)
    {
        result |= inspector->inspect(ctx, var, info, get_metadata);
    }
    else
    {
        result |= inspect_var_properties(ctx, var, info, get_metadata);
    }

    // Record override if this was a property change in a prefab instance
    if(result.changed && info.is_property)
    {
        auto& override_ctx = ctx.get_cached<prefab_override_context>();
        override_ctx.record_override();
    }

    ImGui::PopReadonly();

    return result;
}

auto inspect_var_properties(rtti::context& ctx,
                            rttr::variant& var,
                            const var_info& info,
                            const inspector::meta_getter& get_metadata) -> inspect_result
{
    rttr::instance object = var;
    auto type = object.get_derived_type();
    auto properties = type.get_properties();

    inspect_result result{};
    if(properties.empty())
    {
        if(type.is_enumeration())
        {
            auto enumeration = type.get_enumeration();
            result |= inspect_enum(ctx, var, enumeration, info);
        }

        if(type.is_sequential_container())
        {
            result |= inspect_array(ctx, var, "", "", info, get_metadata);
        }
    }
    else
    {
        std::vector<std::pair<std::string, std::vector<rttr::property>>> grouped_props;
        for(auto& prop : properties)
        {
            // figure out the group name ("" if none)
            auto meta = prop.get_metadata("group");
            std::string group = meta.can_convert<std::string>() ? meta.convert<std::string>() : "";

            // try to find an existing entry for this group
            auto it = std::find_if(grouped_props.begin(),
                                   grouped_props.end(),
                                   [&](auto& kv)
                                   {
                                       return kv.first == group;
                                   });

            if(it == grouped_props.end())
            {
                // first time we see this group: append a new pair
                grouped_props.emplace_back(group, std::vector<rttr::property>{prop});
            }
            else
            {
                // already have this group: just push into its vector
                it->second.emplace_back(prop);
            }
        }
        size_t i = 0;
        for(auto& kvp : grouped_props)
        {
            auto& props = kvp.second;

            const auto& group_name = kvp.first;

            if(group_name.empty())
            {
                for(auto& prop : props)
                {
                    ImGui::PushID(i);
                    result |= inspect_property(ctx, object, prop);
                    ImGui::PopID();
                    i++;
                }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
                if(ImGui::TreeNode(kvp.first.c_str()))
                {
                    ImGui::TreePush(kvp.first.c_str());

                    for(auto& prop : props)
                    {
                        ImGui::PushID(i);
                        result |= inspect_property(ctx, object, prop);
                        ImGui::PopID();
                        i++;
                    }
                    ImGui::TreePop();

                    ImGui::TreePop();
                }
            }
        }
    }

    return result;
}

} // namespace unravel
