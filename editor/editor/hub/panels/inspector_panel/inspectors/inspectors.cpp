#include "inspectors.h"
#include "editor/editing/editing_manager.h"
#include "editor/hub/panels/entity_panel.h"
#include "editor/imgui/integration/fonts/icons/icons_material_design_icons.h"
#include "editor/imgui/integration/imgui.h"
#include "entt/core/any.hpp"
#include "imgui_widgets/utils.h"
#include "inspector.h"
#include "property_path_generator.h"


#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "reflection/reflection.h"
#include "string_utils/utils.h"
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/engine.h>
#include <engine/meta/ecs/entity.hpp>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/text_component.h>
#include <engine/rendering/ecs/components/light_component.h>
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
    auto base_inspector_type = entt::resolve<inspector>();
    auto inspector_types = entt::get_derived_types(base_inspector_type);
    for(auto& inspector_type : inspector_types)
    {
        auto inspected_type_var = entt::get_attribute(inspector_type, "inspected_type");
        if(inspected_type_var)
        {
            // auto inspected_type = inspected_type_var.cast<entt::meta_type>();
            inspector_type.invoke("create_and_register"_hs, {}, inspected_type_var, entt::forward_as_meta(type_map));
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


void add_property_action(rtti::context& ctx,
                         prefab_override_context& override_ctx,
                         inspect_result& result,
                         const meta_any_proxy& var_proxy,
                         const entt::meta_any& old_var,
                         const entt::meta_any& new_var,
                         const entt::meta_custom& custom)
{
    std::function<void()> on_success = nullptr;
    if(override_ctx.record_override())
    {
        auto component_type_name = override_ctx.path_context.get_component_type_name();
        auto component_type_pretty_name = override_ctx.pretty_path_context.get_component_type_name();
        auto prop_path = override_ctx.path_context.get_current_path();
        auto prop_pretty_path = override_ctx.pretty_path_context.get_current_path();
        on_success = [entity = override_ctx.entity, component_type_name, component_type_pretty_name, prop_path]()
        {
            prefab_override_context::mark_property_as_changed(entity, component_type_name, component_type_pretty_name, prop_path);
        };
    }


    //mark_property_as_changed(entity, component_type, property_path);
    if(!result.change_recorded)
    {
        auto& em = ctx.get_cached<editing_manager>();
        em.do_action<property_action_t>({},
                                         var_proxy,
                                         old_var,
                                         new_var,
                                         custom,
                                         on_success);

        result.change_recorded = true;
    }
}

auto prefab_override_context::begin_prefab_inspection(entt::handle e) -> bool
{
    end_prefab_inspection();

    auto prefab_root = find_prefab_root_entity(e);

    if(prefab_root)
    {
        auto prefab_comp = prefab_root.try_get<prefab_component>();
        if(prefab_comp)
        {
            // Initialize override tracking for this prefab instance
            prefab_root_entity = prefab_root;
            entity = e;
            is_active = true;

            is_path_overridden_callback =
                [comp_copy = *prefab_comp, entity_copy = e](const hpp::uuid& entity_uuid,
                                                                 const std::string& component_path)
            {
                return comp_copy.has_override(entity_uuid, component_path);
            };

            // Get and set the entity UUID for both path contexts
            auto entity_uuid = get_entity_prefab_uuid(e);

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
    entity = {};
    path_context = {};
    pretty_path_context = {};
}

auto prefab_override_context::record_override() -> bool
{
    if(!is_active || !prefab_root_entity)
    {
        return false;
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
            return true;
        }
    }
    return true;
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
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
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
        ImGui::PopStyleColor(2);
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
        mark_property_as_changed(entity, entt::resolve<transform_component>(), "local_transform/position");
    }
    if(rotation)
    {
        mark_property_as_changed(entity, entt::resolve<transform_component>(), "local_transform/rotation");
    }
    if(scale)
    {
        mark_property_as_changed(entity, entt::resolve<transform_component>(), "local_transform/scale");
    }
    if(skew)
    {
        mark_property_as_changed(entity, entt::resolve<transform_component>(), "local_transform/skew");
    }
}


void prefab_override_context::mark_transform_global_as_changed(entt::handle entity,
                                                        bool position,
                                                        bool rotation,
                                                        bool scale,
                                                        bool skew)
{
    // also changes local transform
    mark_transform_as_changed(entity, position, rotation, scale, skew);


    if(position)
    {
        mark_property_as_changed(entity, entt::resolve<transform_component>(), "global_transform/position");
    }
    if(rotation)
    {
        mark_property_as_changed(entity, entt::resolve<transform_component>(), "global_transform/rotation");
    }
    if(scale)
    {
        mark_property_as_changed(entity, entt::resolve<transform_component>(), "global_transform/scale");
    }
    if(skew)
    {
        mark_property_as_changed(entity, entt::resolve<transform_component>(), "global_transform/skew");
    }
}

void prefab_override_context::mark_active_as_changed(entt::handle entity)
{
    mark_property_as_changed(entity, entt::resolve<transform_component>(), "active");
}

void prefab_override_context::mark_text_area_as_changed(entt::handle entity)
{
    mark_property_as_changed(entity, entt::resolve<text_component>(), "area");
}

void prefab_override_context::mark_material_as_changed(entt::handle entity)
{
    mark_property_as_changed(entity, entt::resolve<model_component>(), "model/materials");
}

void prefab_override_context::mark_property_as_changed(entt::handle entity,
                                                       const entt::meta_type& component_type,
                                                       const std::string& property_path)
{
    auto component_type_name = entt::get_name(component_type);
    auto component_pretty_type_name = entt::get_pretty_name(component_type);

    mark_property_as_changed(entity, component_type_name, component_pretty_type_name, property_path);
}

void prefab_override_context::mark_property_as_changed(entt::handle entity,
                                                       const std::string& component_type_name,
                                                       const std::string& component_pretty_type_name,
                                                       const std::string& property_path)
{
    auto prefab_root = find_prefab_root_entity(entity);
    if(prefab_root)
    {
        auto prefab_comp = prefab_root.try_get<prefab_component>();
        auto entity_uuid = get_entity_prefab_uuid(entity);

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

            auto type = entt::resolve(entt::hashed_string(component_type_name.c_str()));
            std::string current_path = component_type_name;
            std::string current_pretty_path = component_pretty_type_name;

            for(auto& token : tokens)
            {
                auto prop = type.data(entt::hashed_string(token.c_str()));
                auto prop_type = prop.type();

                current_path += "/" + token;
                current_pretty_path += "/" + entt::get_pretty_name(prop);

                type = prop_type;
            }

            prefab_comp->add_override(entity_uuid, current_path, current_pretty_path);
        }
    }


    if(component_type_name == "transform_component")
    {
        auto property_path_global =string_utils::replace(property_path, "global_transform", "local_transform");

        if(property_path_global != property_path)
        {
            mark_property_as_changed(entity, component_type_name, component_pretty_type_name, property_path_global);
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

    auto etype = entt::resolve(entt::hashed_string(component_type.c_str()));
    if(!etype)
    {
        return false;
    }
    auto method = etype.func("component_exists"_hs);
    if(!method)
    {
        return false;
    }

    struct prefab_version_t
    {
        uintptr_t version{};
    };

    entt::handle instance;
    auto view = cache_scene.registry->view<prefab_component, prefab_version_t>();
    view.each(
        [&](auto e, auto&& comp, auto&& version)
        {
            if(comp.source == prefab && version.version == prefab.version())
            {
                instance = cache_scene.create_handle(e);
            }
        });
    if(!instance)
    {
        cache_scene.unload();
        instance = cache_scene.instantiate(prefab);
        instance.emplace<prefab_version_t>(prefab.version());
    }

    auto entity = scene::find_entity_by_prefab_uuid(instance, entity_uuid);
    if(!entity)
    {
        return false;
    }

    auto result = method.invoke({}, entity);

    bool exists = result.cast<bool>();
    if(!exists)
    {
        return false;
    }

    if(etype == entt::resolve<script_component>())
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

auto get_inspector(rtti::context& ctx, const entt::meta_type& type) -> std::shared_ptr<inspector>
{
    auto& registry = ctx.get_cached<inspector_registry>();
    auto it = registry.type_map.find(type.info().index());
    if(it == registry.type_map.end())
    {
        return nullptr;
    }

    return it->second;
}

auto is_property_visible(const entt::meta_any& object, const entt::meta_data& prop) -> bool
{
    auto predicate_meta = entt::get_attribute(prop, "predicate");
    if(predicate_meta.try_cast<entt::property_predicate_t>())
    {
        auto pred = predicate_meta.cast<entt::property_predicate_t>();
        return pred(object);
    }

    return true;
}

auto is_property_readonly(const entt::meta_any& object, const entt::meta_data& prop) -> bool
{
    auto predicate_meta = entt::get_attribute(prop, "readonly_predicate");
    if(predicate_meta.try_cast<entt::property_predicate_t>())
    {
        auto pred = predicate_meta.cast<entt::property_predicate_t>();
        return pred(object);
    }

    return false;
}

auto is_property_flattable(const entt::meta_any& object, const entt::meta_data& prop) -> bool
{
    auto predicate_meta = entt::get_attribute(prop, "flattable");
    if(predicate_meta.try_cast<bool>())
    {
        auto pred = predicate_meta.cast<bool>();
        return pred;
    }

    return false;
}

auto inspect_property(rtti::context& ctx, entt::meta_any& object, const meta_any_proxy& var_proxy, const entt::meta_data& prop) -> inspect_result
{
    if(!is_property_visible(object, prop))
    {
        return {};
    }

    inspect_result result{};
    auto prop_var = prop.get(object);
    entt::as_derived(prop_var);

    auto prop_old_var = prop.get(object);
    bool is_readonly = prop.is_const() || is_property_readonly(object, prop);

    auto prop_type = prop_var.type();

    bool is_flattable = is_property_flattable(object, prop);
    bool is_array = prop_type.is_sequence_container();
    bool is_associative_container = prop_type.is_associative_container();
    bool is_container = is_array || is_associative_container;
    bool is_enum = prop_type.is_enum();
    auto prop_inspector = get_inspector(ctx, prop_type);

    auto prop_name = entt::get_name(prop);
    auto pretty_name = entt::get_pretty_name(prop);

    is_readonly |= ImGui::IsReadonly();

    var_info info;
    info.read_only = is_readonly;
    info.is_property = true;

    // Push property name to path context for flattable properties too
    auto& override_ctx = ctx.get_cached<prefab_override_context>();
    override_ctx.push_segment(prop_name, pretty_name);

    if(prop_inspector)
    {
        prop_inspector->before_inspect(prop);
    }

    ImGui::PushReadonly(is_readonly);


    auto prop_proxy = make_property_proxy(var_proxy, prop);


    {
        if(is_array)
        {
            result |= inspect_array(ctx, prop_var, prop_proxy, prop, info, prop.custom());
        }
        else if(is_associative_container)
        {
            result |= inspect_associative_container(ctx, prop_var, prop_proxy, prop, info, prop.custom());
        }
        else if(is_enum)
        {
            property_layout layout(prop);
            result |= inspect_enum(ctx, prop_var, prop_proxy, info);
        }
        else
        {
            if(prop_inspector)
            {
                result |= inspect_var(ctx, prop_var, prop_proxy, info, prop.custom());
            }
            else if(!is_flattable)
            {
                ImGui::AlignTextToFramePadding();
                ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);

                ImGui::BeginGroup();

                bool open = ImGui::TreeNodeEx(pretty_name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);

                if(open)
                {
                    ImGui::TreePush(pretty_name.c_str());

                    result |= inspect_var(ctx, prop_var, prop_proxy, info, prop.custom());

                    ImGui::TreePop();
                    ImGui::TreePop();

                    ImGui::EndGroup();
                    ImGui::SetItemFocusFrame(ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
                }
                else
                {
                    ImGui::EndGroup();
                }
                
            }
            else
            {
                ImGui::PushID(pretty_name.c_str());

                result |= inspect_var(ctx, prop_var, prop_proxy, info, prop.custom());

                ImGui::PopID();
            }
        }
    }

    if(result.changed && !is_readonly)
    {
        prop.set(object, prop_var);
        add_property_action(ctx, override_ctx, result, prop_proxy, prop_old_var, prop_var, prop.custom());
  
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
                   entt::meta_any& var,
                   const meta_any_proxy& var_proxy,
                   const entt::meta_data& prop,
                   const var_info& info,
                   const entt::meta_custom& custom) -> inspect_result
{
    auto name = entt::get_pretty_name(prop);

    auto tooltip = entt::get_attribute_as<std::string>(prop, "tooltip");

    return inspect_array(ctx, var, var_proxy, name, tooltip, info, custom);
}


auto inspect_array(rtti::context& ctx,
                   entt::meta_any& var,
                   const meta_any_proxy& var_proxy,
                   const std::string& name,
                   const std::string& tooltip,
                   const var_info& info,
                   const entt::meta_custom& custom) -> inspect_result
{
    auto view = var.as_sequence_container();
    auto size = view.size();
    inspect_result result{};
    auto int_size = static_cast<int>(size);

    ImGui::BeginGroup();
    property_layout layout;
    layout.set_data(name, tooltip);

    bool open = layout.push_tree_layout();

    int readonly_count = entt::get_attribute_as<int>(custom, "readonly_count");
    bool is_fixed_size_array = entt::get_attribute_as<bool>(custom, "is_fixed_size_array");

    bool resizeable = !is_fixed_size_array && view.resize(size);
    {

        ImGuiInputTextFlags flags = 0;
        int step = 1;
        int step_fast = 100;
        bool readonly = info.read_only || !resizeable;
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

            if(view.resize(static_cast<std::size_t>(int_size)))
            {
                size = static_cast<std::size_t>(int_size);
                result.changed = true;
            }
            result.edit_finished = true;
        }
        ImGui::PopReadonly();

        ImGui::DrawItemActivityOutline();
    }

    if(open)
    {
        layout.pop_layout();

        // struct element_t
        // {
        //     entt::meta_any value;
        //     std::string name;
        //     meta_any_proxy proxy;
        //     var_info info;
        // };
        // std::vector<element_t> elements;

        ImGui::TreePush("array");

        int index_to_remove = -1;
        for(std::size_t i = 0; i < size; ++i)
        {
            auto value = view[i];
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

                meta_any_proxy value_proxy;
                value_proxy.impl->type_name = entt::get_pretty_name(value.type());
                value_proxy.impl->get_name = [var_proxy, element]()
                {
                    auto name = var_proxy.impl->get_name();
                    if(name.empty())
                    {
                        return element;
                    }
                    return fmt::format("{}/{}", name, element);
                };
                value_proxy.impl->getter = [parent_proxy = var_proxy, i](entt::meta_any& result)
                {
                    entt::meta_any var;
                    if(parent_proxy.impl->getter(var) && var)
                    {
                        auto view = var.as_sequence_container();
                        if(view.size() > static_cast<std::size_t>(i))
                        {
                            auto value = view[i];   
                            result = value;
                            return true;
                        }
                    }
                    return false;
                };
                value_proxy.impl->setter = [parent_proxy = var_proxy, i](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
                {
                    entt::meta_any var;
                    if(parent_proxy.impl->getter(var) && var)
                    {
                        auto view = var.as_sequence_container();
                        if(view.size() > static_cast<std::size_t>(i))
                        {
                             // get iterator to i
                            auto it = view.begin();
                            std::advance(it, static_cast<std::ptrdiff_t>(i));

                            // remove old element
                            it = view.erase(it);

                            // insert new element at position i
                            view.insert(it, value);

                            // If the getter returned a copy, write back; if it was a ref, this is harmless.
                            return parent_proxy.impl->setter(parent_proxy, var, execution_count);
                
                        }
                    }
                    return false;
                };

                // elements.emplace_back(element_t{value, element, value_proxy, item_info});
                result |= inspect_var(ctx, value, value_proxy, item_info, custom);

                // Pop array index from property path
                // if(override_ctx.is_active)
                // {
                //     override_ctx.path_context.pop_segment();
                //     override_ctx.pretty_path_context.pop_segment();
                // }

                ImGui::PopReadonly();
            }
            auto pos_after = ImGui::GetCursorPos();

            // if(result.changed)
            // {
            //     view[i] = value;
            // }

            if(!item_info.read_only && resizeable)
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

        // ImGui::ReorderableList(name.c_str(), static_cast<int>(elements.size()), [&](int index) {
        //     auto& element = elements[index];
        //     property_layout layout;
        //     layout.set_data(element.name, {}, true);
        //     layout.push_tree_layout(ImGuiTreeNodeFlags_Leaf);
        //     ImGui::PushReadonly(element.info.read_only);
        //     result |= inspect_var(ctx, element.value, element.proxy, element.info, custom);
        //     ImGui::PopReadonly();
        // }, [&](int from, int insert_before) {
        //     ImGui::VectorMoveInsert(view, from, insert_before);
        //     result.changed = true;
        //     result.edit_finished = true;
        // });

        if(index_to_remove != -1)
        {
            auto it = view.begin();
            std::advance(it, index_to_remove);
            view.erase(it);
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
                                   entt::meta_any& var,
                                   const meta_any_proxy& var_proxy,
                                   const entt::meta_data& prop,
                                   const var_info& info,
                                   const entt::meta_custom& custom) -> inspect_result
{
    auto view = var.as_associative_container();
    auto size = view.size();
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

auto inspect_enum(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info) -> inspect_result
{
    auto edited = var;
    if(!edited.allow_cast<int64_t>())
    {
        return {};
    }
    auto current_value = edited.cast<int64_t>();

    int current_idx = 0;
    int i = 0;

    auto type = var.type();

    struct enum_value
    {
        std::string name;
        std::string pretty_name;
        int64_t value{};
    };

    std::vector<enum_value> names;
    for(auto kvp : type.data())
    {
        const auto& data = kvp.second;
        auto name = entt::get_name(data);
        auto pretty_name = entt::get_pretty_name(data);
        auto value = data.get(var);

        int64_t value64 = 0;
        if(value.allow_cast<int64_t>())
        {
            value64 = value.cast<int64_t>();
        }
        else
        {
            continue;
        }
        names.emplace_back(enum_value{name, pretty_name, value64});

        if(value64 == current_value)
        {
            current_idx = i;
        }
        ++i;
    }

    if(current_idx >= static_cast<int>(names.size()))
    {
        return {};
    }

    inspect_result result{};

    if(info.read_only)
    {
        ImGui::LabelText("##enum", "%s", names[current_idx].pretty_name.c_str());
    }
    else
    {
        int listbox_item_size = static_cast<int>(names.size());

        ImGuiComboFlags flags = 0;

        if(ImGui::BeginCombo("##enum", names[current_idx].pretty_name.c_str(), flags))
        {
            for(int n = 0; n < listbox_item_size; n++)
            {
                const bool is_selected = (current_idx == n);

                if(ImGui::Selectable(names[n].pretty_name.c_str(), is_selected))
                {
                    current_idx = n;
                    result.changed = true;
                    result.edit_finished |= true;

                    edited = names[n].value;

                    if(edited.allow_cast(var.type()))
                    {
                        var = std::move(edited);
                    }
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

auto inspect_var(rtti::context& ctx,
                 entt::meta_any& var,
                 const meta_any_proxy& var_proxy,
                 const var_info& info,
                 const entt::meta_custom& custom) -> inspect_result
{

    entt::as_derived(var);
    auto type = var.type();

    meta_any_proxy derived_var_proxy;
    derived_var_proxy.impl->type_name = entt::get_pretty_name(var.type());
    derived_var_proxy.impl->get_name = [parent_proxy = var_proxy]()
    {
        return parent_proxy.impl->get_name();
    };
    derived_var_proxy.impl->getter = [parent_proxy = var_proxy](entt::meta_any& result)
    {      
        if(parent_proxy.impl->getter(result) && result)
        {
            entt::as_derived(result);
            return true;
        }
        return false;
    };
    derived_var_proxy.impl->setter = [parent_proxy = var_proxy](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
    {
        entt::meta_any var;
        if(proxy.impl->getter(var) && var)
        {
            var.assign(value);
            return parent_proxy.impl->setter(parent_proxy, var, execution_count);
        }
        return false;
    };

    entt::meta_any old_var;
    if(info.is_copyable)
    {
        old_var = var;
    }

    inspect_result result{};

    ImGui::PushReadonly(info.read_only);

    auto inspector = get_inspector(ctx, type);
    if(inspector)
    {
        result |= inspector->inspect(ctx, var, derived_var_proxy, info, custom);
    }
    else if(type.is_enum())
    {
        result |= inspect_enum(ctx, var, derived_var_proxy, info);
    }
    else
    {
        result |= inspect_var_properties(ctx, var, derived_var_proxy, info, custom);
    }

    // Record override if this was a property change in a prefab instance
    if(result.changed && info.is_property)
    {
        auto& override_ctx = ctx.get_cached<prefab_override_context>();

        add_property_action(ctx, override_ctx, result, derived_var_proxy, old_var, var, custom);
    }

    ImGui::PopReadonly();

    return result;
}

auto inspect_var_properties_impl(rtti::context& ctx,
                                 entt::meta_any& var,
                                 const meta_any_proxy& var_proxy,
                                 const entt::meta_type& type,
                                 const var_info& info,
                                 const entt::meta_custom& custom) -> inspect_result
{
    auto properties = type.data();

    inspect_result result{};
    if(properties.begin() == properties.end())
    {
        if(type.is_enum())
        {
            result |= inspect_enum(ctx, var, var_proxy, info);
        }

        if(type.is_sequence_container())
        {
            auto name = entt::get_pretty_name(custom);
            auto tooltip = entt::get_attribute_as<std::string>(custom, "tooltip");
            result |= inspect_array(ctx, var, var_proxy, name, tooltip, info, custom);
        }
    }
    else
    {
        std::vector<std::pair<std::string, std::vector<entt::meta_data>>> grouped_props;
        for(auto kvp : properties)
        {
            const auto& prop = kvp.second;
            // figure out the group name ("" if none)
            auto group = entt::get_attribute_as<std::string>(prop, "group");

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
                grouped_props.emplace_back(group, std::vector<entt::meta_data>{prop});
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
                for(auto&& prop : props)
                {
                    ImGui::PushID(i);
                    result |= inspect_property(ctx, var, var_proxy, prop);
                    ImGui::PopID();
                    i++;
                }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
                ImGui::BeginGroup();
                if(ImGui::TreeNodeEx(kvp.first.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth))
                {
                    ImGui::TreePush(kvp.first.c_str());
                    ImGui::PushID(kvp.first.c_str());

                    for(auto& prop : props)
                    {
                        ImGui::PushID(i);
                        result |= inspect_property(ctx, var, var_proxy, prop);
                        ImGui::PopID();
                        i++;
                    }
                    ImGui::PopID();
                    ImGui::TreePop();

                    ImGui::TreePop();

                    ImGui::EndGroup();
                    ImGui::SetItemFocusFrame(ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
                }
                else
                {
                    ImGui::EndGroup();
                }
            }
        }
    }

    return result;
}

auto inspect_var_properties(rtti::context& ctx,
                            entt::meta_any& var,
                            const meta_any_proxy& var_proxy,
                            const var_info& info,
                            const entt::meta_custom& custom) -> inspect_result
{
    inspect_result result{};
    for(auto base : var.type().base())
    {
        result |= inspect_var_properties_impl(ctx, var, var_proxy, base.second, info, custom);
    }

    entt::as_derived(var);
    result |= inspect_var_properties_impl(ctx, var, var_proxy, var.type(), info, custom);

    return result;
}

} // namespace unravel
