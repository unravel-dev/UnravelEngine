#include "component_clipboard.h"

#include <editor/editing/actions/composite_action.h>
#include <editor/editing/actions/entity_actions.h>
#include <editor/editing/clipboard_document.h>
#include <editor/editing/editing_manager.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>

#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/layer_component.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/meta/ecs/components/all_components.h>
#include <engine/meta/ecs/components/script_component.hpp>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/scripting/ecs/components/script_component.h>

#include <hpp/utility.hpp>
#include <logging/logging.h>
#include <ser20/external/simdjson/simdjson.h>
#include <string_utils/utils.h>

#include <sstream>

namespace unravel
{
namespace
{

using component_clipboard::copied_component;
using component_clipboard::is_copyable;

/// The members a copy of a component keeps; empty for all of them. A transform keeps its local transform only: its
/// parent and children are where it stands, not values to give another entity.
auto get_copied_members(const entt::meta_type& type) -> std::vector<std::string>
{
    if(type == entt::resolve<transform_component>())
    {
        return {"local_transform"};
    }
    return {};
}

/// @p state, a component's save, with only @p members kept.
auto keep_members(const std::string& state, const std::vector<std::string>& members) -> std::string
{
    if(members.empty())
    {
        return state;
    }
    simdjson::dom::parser parser;
    simdjson::dom::object component;
    if(parser.parse(state).get_object()["component"].get(component))
    {
        return state;
    }
    std::string kept;
    for(const auto& member : members)
    {
        simdjson::dom::element value;
        if(!component[member].get(value))
        {
            kept += (kept.empty() ? "" : ",") + fmt::format("\"{}\":{}", member, simdjson::minify(value));
        }
    }
    return fmt::format(R"({{"component":{{{}}}}})", kept);
}

auto component_to_json(const entt::meta_type& type, const std::string& state) -> std::string
{
    return fmt::format(R"({{"type":"{}","name":"{}","state":{}}})",
                       string_utils::escape_json(std::string(entt::get_name(type))),
                       string_utils::escape_json(entt::get_pretty_name(type)),
                       state);
}

auto copied_script_to_json(const script_component::script_object& script, const std::string& state) -> std::string
{
    const auto type = script.pinned->get_object().get_type();
    return fmt::format(R"({{"script":"{}","name":"{}","state":{}}})",
                       string_utils::escape_json(type.get_fullname()),
                       string_utils::escape_json(type.get_name()),
                       state);
}

auto save_native(entt::handle entity, const entt::meta_type& type) -> std::string
{
    return keep_members(prefab_override_context::save_component_state(entity, type), get_copied_members(type));
}

auto save_script(entt::handle entity, const script_component::script_object& script) -> std::string
{
    std::stringstream stream;
    return save_to_stream(stream, entity, script) ? stream.str() : std::string{};
}

auto make_components_document(const std::vector<std::string>& components) -> std::string
{
    std::string list;
    for(const auto& component : components)
    {
        list += (list.empty() ? "" : ",") + component;
    }
    return fmt::format(R"({},"components":[{}]}})", clipboard_document::make_head(clipboard_document::COMPONENTS_KIND), list);
}

auto read_component(const simdjson::dom::object& object) -> hpp::optional<copied_component>
{
    copied_component item;
    simdjson::dom::element state;
    std::string_view text;
    if(object["state"].get(state))
    {
        return hpp::nullopt;
    }
    item.state = simdjson::minify(state);
    if(!object["name"].get(text))
    {
        item.name = std::string(text);
    }
    if(!object["script"].get(text))
    {
        item.script_type = std::string(text);
        return item;
    }
    if(object["type"].get(text))
    {
        return hpp::nullopt;
    }
    item.type = entt::resolve(entt::hashed_string{std::string(text).c_str()});
    if(!item.type || !is_copyable(item.type))
    {
        return hpp::nullopt;
    }
    return item;
}

/// The slot of @p entity's first script of @p type_name; nil when it has none.
auto find_script_slot(entt::handle entity, const std::string& type_name) -> hpp::uuid
{
    const auto* scripts = entity.try_get<script_component>();
    if(scripts == nullptr)
    {
        return {};
    }
    for(const auto& script : scripts->get_script_components())
    {
        if(script.pinned && script.pinned->get_object().get_type().get_fullname() == type_name)
        {
            return script.slot;
        }
    }
    return {};
}

/// The step that loads @p state into @p entity's component of @p type, made after the load: the step finds it done.
auto make_load_step(entt::handle entity, const entt::meta_type& type, const std::string& state) -> std::shared_ptr<editing_action_t>
{
    const auto state_before = prefab_override_context::save_component_state(entity, type);
    if(state_before.empty())
    {
        return nullptr;
    }
    load_component_state(entity, type, state);
    return std::make_shared<entity_component_state_action_t>(entity, type, state_before);
}

void add_native_as_new(sequence_action_t& sequence, entt::handle target, const copied_component& item, const std::shared_ptr<size_t>& added)
{
    const auto target_id = entt::make_uhandle(target);
    auto is_added = std::make_shared<bool>(false);
    sequence.add_step(
        [target_id, type = item.type, is_added]() -> std::shared_ptr<editing_action_t>
        {
            auto entity = target_id.resolve();
            if(!entity || has_component(entity, type))
            {
                return nullptr;
            }
            *is_added = true;
            return std::make_shared<entity_add_component_action_t>(entity, type);
        });
    sequence.add_step(
        [target_id, type = item.type, state = item.state, is_added, added]() -> std::shared_ptr<editing_action_t>
        {
            auto entity = target_id.resolve();
            if(!entity || !*is_added)
            {
                return nullptr;
            }
            ++*added;
            return make_load_step(entity, type, state);
        });
}

void add_script_as_new(sequence_action_t& sequence, entt::handle target, const copied_component& item, const std::shared_ptr<size_t>& added)
{
    const auto target_id = entt::make_uhandle(target);
    auto add = std::make_shared<entity_add_script_component_action_t>(target, item.script_type);
    sequence.add_step(
        [add]() -> std::shared_ptr<editing_action_t>
        {
            return add;
        });
    sequence.add_step(
        [target_id, add, state = item.state, added]() -> std::shared_ptr<editing_action_t>
        {
            auto entity = target_id.resolve();
            if(!entity || !add->do_was_successful)
            {
                return nullptr;
            }
            ++*added;
            return std::make_shared<entity_load_script_values_action_t>(entity, add->added_slot, state);
        });
}

} // namespace

namespace component_clipboard
{

auto is_copyable(const entt::meta_type& type) -> bool
{
    return type && type != entt::resolve<id_component>() && type != entt::resolve<tag_component>() &&
           type != entt::resolve<layer_component>() && type != entt::resolve<prefab_component>() &&
           type != entt::resolve<prefab_id_component>() && type != entt::resolve<bone_component>() &&
           type != entt::resolve<submesh_component>() && type != entt::resolve<script_component>();
}

auto copy_component(entt::handle entity, const entt::meta_type& type) -> std::string
{
    if(!is_copyable(type) || !has_component(entity, type))
    {
        return {};
    }
    const auto state = save_native(entity, type);
    return state.empty() ? std::string{} : make_components_document({component_to_json(type, state)});
}

auto copy_script(entt::handle entity, const hpp::uuid& slot) -> std::string
{
    const auto* scripts = entity ? entity.try_get<script_component>() : nullptr;
    const auto script = scripts != nullptr ? scripts->get_script_component_by_slot(slot) : script_component::script_object{};
    if(!script.pinned)
    {
        return {};
    }
    const auto state = save_script(entity, script);
    return state.empty() ? std::string{} : make_components_document({copied_script_to_json(script, state)});
}

auto copy_all_components(entt::handle entity) -> std::string
{
    if(!entity)
    {
        return {};
    }
    std::vector<std::string> components;
    hpp::for_each_tuple_type<all_inspectable_components>(
        [&](auto index)
        {
            using ctype = std::tuple_element_t<decltype(index)::value, all_inspectable_components>;
            const auto type = entt::resolve<ctype>();
            if(entity.all_of<ctype>() && is_copyable(type))
            {
                if(auto state = save_native(entity, type); !state.empty())
                {
                    components.push_back(component_to_json(type, state));
                }
            }
        });
    if(const auto* scripts = entity.try_get<script_component>())
    {
        for(const auto& script : scripts->get_script_components())
        {
            if(script.pinned)
            {
                if(auto state = save_script(entity, script); !state.empty())
                {
                    components.push_back(copied_script_to_json(script, state));
                }
            }
        }
    }
    return components.empty() ? std::string{} : make_components_document(components);
}

auto read_components(hpp::string_view text) -> std::vector<copied_component>
{
    std::vector<copied_component> items;
    if(!clipboard_document::is_kind(text, clipboard_document::COMPONENTS_KIND))
    {
        return items;
    }
    simdjson::dom::parser parser;
    simdjson::dom::object document;
    int64_t version = 0;
    simdjson::dom::array components;
    if(parser.parse(text.data(), text.size()).get(document) || document[clipboard_document::VERSION_KEY].get(version) ||
       version > clipboard_document::VERSION || document["components"].get(components))
    {
        return items;
    }
    for(auto element : components)
    {
        simdjson::dom::object object;
        if(element.get(object))
        {
            continue;
        }
        if(auto item = read_component(object))
        {
            items.push_back(std::move(*item));
        }
    }
    return items;
}

auto clipboard_components() -> std::vector<copied_component>
{
    const char* text = ImGui::GetClipboardText();
    return text != nullptr ? read_components(text) : std::vector<copied_component>{};
}

auto find_clipboard_component(const entt::meta_type& type, const std::string& script_type) -> hpp::optional<copied_component>
{
    for(auto& item : clipboard_components())
    {
        const bool matches = script_type.empty() ? (!item.is_script() && item.type == type) : item.script_type == script_type;
        if(matches)
        {
            return item;
        }
    }
    return hpp::nullopt;
}

void add_paste_values_steps(composite_action_t& edits,
                           const std::vector<entt::handle>& targets,
                           const copied_component& item,
                           const hpp::uuid& script_slot)
{
    for(const auto& target : targets)
    {
        if(!target)
        {
            continue;
        }
        if(item.is_script())
        {
            const auto slot = script_slot.is_nil() || target != targets.front() ? find_script_slot(target, item.script_type)
                                                                                 : script_slot;
            if(!slot.is_nil())
            {
                edits.add_sub_action(std::make_shared<entity_load_script_values_action_t>(target, slot, item.state));
            }
            continue;
        }
        if(has_component(target, item.type))
        {
            if(auto step = make_load_step(target, item.type, item.state))
            {
                edits.add_sub_action(step);
            }
        }
    }
}

auto paste_values(rtti::context& ctx, const std::vector<entt::handle>& targets, const copied_component& item, const hpp::uuid& script_slot)
    -> size_t
{
    auto edits = std::make_shared<composite_action_t>();
    add_paste_values_steps(*edits, targets, item, script_slot);
    const auto count = edits->sub_actions.size();
    if(count > 0)
    {
        ctx.get_cached<editing_manager>().do_action(fmt::format("Paste {} Values", item.name), edits);
    }
    return count;
}

auto paste_as_new(rtti::context& ctx, const std::vector<entt::handle>& targets, const std::vector<copied_component>& items)
    -> size_t
{
    auto sequence = std::make_shared<sequence_action_t>();
    auto added = std::make_shared<size_t>(0);
    for(const auto& target : targets)
    {
        for(const auto& item : items)
        {
            if(!target)
            {
                continue;
            }
            if(item.is_script())
            {
                add_script_as_new(*sequence, target, item, added);
            }
            else
            {
                add_native_as_new(*sequence, target, item, added);
            }
        }
    }
    const auto name = items.size() == 1 ? fmt::format("Paste {} as New", items.front().name) : std::string("Paste Components as New");
    ctx.get_cached<editing_manager>().do_action(name, sequence);
    return *added;
}

void set_clipboard(const std::string& document)
{
    if(!document.empty())
    {
        ImGui::SetClipboardText(document.c_str());
    }
}

auto make_value_document(const std::string& value_json, hpp::string_view type_name, hpp::string_view label) -> std::string
{
    return fmt::format(R"({},"type":"{}","label":"{}","value":{}}})",
                       clipboard_document::make_head(clipboard_document::VALUE_KIND),
                       string_utils::escape_json(std::string(type_name)),
                       string_utils::escape_json(std::string(label)),
                       value_json);
}

auto clipboard_value_json(hpp::string_view type_name) -> hpp::optional<std::string>
{
    const char* clipboard = ImGui::GetClipboardText();
    if(clipboard == nullptr || clipboard[0] == '\0')
    {
        return hpp::nullopt;
    }
    const std::string text(clipboard);
    simdjson::dom::parser parser;
    simdjson::dom::element parsed;
    const bool is_json = !parser.parse(text).get(parsed);
    if(clipboard_document::is_kind(text, clipboard_document::VALUE_KIND))
    {
        std::string_view copied_type;
        simdjson::dom::element value;
        if(!is_json || parsed["type"].get(copied_type) || copied_type != type_name || parsed["value"].get(value))
        {
            return hpp::nullopt;
        }
        return simdjson::minify(value);
    }
    if(text.find(clipboard_document::VERSION_KEY) != std::string::npos)
    {
        // Entities or components: no value for a row.
        return hpp::nullopt;
    }
    if(is_json)
    {
        return simdjson::minify(parsed);
    }
    return "\"" + string_utils::escape_json(text) + "\"";
}

} // namespace component_clipboard
} // namespace unravel
