// UI inspection over MCP: query the elements of the documents on screen, click one by selector, and
// read the data models scripts bound. Element geometry and text come from the engine's own UI queries
// (ui_element_queries.h, which the script API uses too); clicks go through the same virtual pointer
// and press / release helpers as input_send_mouse.
#include "mcp_input_utils.h"
#include "mcp_tools_common.h"

#include <editor/editing/editor_actions.h>
#include <editor/hub/hub.h>
#include <editor/hub/panels/panel.h>
#include <editor/system/mcp_manager.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <engine/ui/ecs/components/ui_document_component.h>
#include <engine/ui/ui_element_queries.h>

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Property.h>

#include <algorithm>
#include <limits>
#include <hpp/optional.hpp>
#include <string>
#include <vector>

namespace unravel::mcp
{
namespace
{

constexpr int64_t k_default_query_limit = 25;
constexpr int64_t k_max_query_limit = 200;
constexpr size_t k_max_text_length = 160;
constexpr int64_t k_default_model_depth = 4;
constexpr int64_t k_max_model_depth = 8;
constexpr int64_t k_default_model_items = 20;
constexpr int64_t k_max_model_items = 200;
/// Frames between moving the pointer and pressing: the UI updates hover from the pointer once a frame.
constexpr uint64_t k_hover_frames = 2;

/// Pseudo-classes worth reporting: what a test asks about a control's state.
constexpr const char* k_reported_pseudo_classes[] = {"hover", "active", "focus", "checked", "disabled", "selected"};

/// How one document maps between its own pixels and the game view's.
struct document_view
{
    entt::handle entity;
    ui_document_component* component{};
    bool is_world_space{false};
    math::transform quad;
    const camera* view_camera{};
};

/// A document pixel in game pixels; false when a world-space document's point is behind the camera.
auto to_game_point(const document_view& view, const math::vec2& document_point, math::vec2& out) -> bool
{
    if(!view.is_world_space)
    {
        out = document_point;
        return true;
    }
    if(view.view_camera == nullptr || view.component->size.width == 0 || view.component->size.height == 0)
    {
        return false;
    }
    // The inverse of camera::project_to_quad: pixel (0, 0) is the quad's local (-0.5, 0.5).
    const math::vec3 local{document_point.x / static_cast<float>(view.component->size.width) - 0.5f,
                           0.5f - document_point.y / static_cast<float>(view.component->size.height),
                           0.0f};
    const auto world = view.quad.transform_coord(local);
    if(view.view_camera->world_to_view_depth(world) <= 0.0f)
    {
        return false;
    }
    const auto screen = view.view_camera->world_to_viewport(world);
    out = {screen.x, screen.y};
    return true;
}

/// A game pixel in the document's pixels; false when it misses a world-space document's quad.
auto to_document_point(const document_view& view, const math::vec2& game_point, math::vec2& out) -> bool
{
    if(!view.is_world_space)
    {
        out = game_point;
        return true;
    }
    return view.view_camera != nullptr && view.view_camera->project_to_quad(game_point,
                                                                             view.quad,
                                                                             view.component->size.width,
                                                                             view.component->size.height,
                                                                             out);
}

/// The loaded documents of the active scene, or the one on @p entity_id; screen-space ones first,
/// since they draw over the world.
auto collect_document_views(rtti::context& ctx, const std::string& entity_id, std::vector<document_view>& out, std::string& error)
    -> bool
{
    scene* scn = nullptr;
    if(!require_active_scene(ctx, scn, error))
    {
        return false;
    }
    const camera* view_camera = nullptr;
    if(auto camera_entity = ctx.get_cached<hub>().get_panels().get_game_panel().get_displayed_camera(ctx))
    {
        view_camera = &camera_entity.get<camera_component>().get_camera();
    }
    auto add = [&](entt::handle entity)
    {
        auto* comp = entity.try_get<ui_document_component>();
        if(comp == nullptr || comp->document == nullptr)
        {
            return;
        }
        document_view view;
        view.entity = entity;
        view.component = comp;
        view.is_world_space = comp->render_mode == ui_render_mode::world_space;
        view.view_camera = view_camera;
        if(view.is_world_space)
        {
            view.quad = comp->get_world_space_quad(entity.get<transform_component>().get_transform_global());
        }
        out.push_back(view);
    };
    if(!entity_id.empty())
    {
        auto entity = resolve_entity(*scn, entity_id, error);
        if(!entity)
        {
            return false;
        }
        if(!entity.all_of<ui_document_component>())
        {
            error = "Entity has no UI Document: " + entity_id;
            return false;
        }
        add(entity);
        if(out.empty())
        {
            error = "The UI Document of " + entity_id + " has no loaded document (no asset, closed, or still loading)";
            return false;
        }
        return true;
    }
    scn->registry->view<ui_document_component>().each(
        [&](entt::entity entity, ui_document_component&)
        {
            add(scn->create_handle(entity));
        });
    std::stable_sort(out.begin(),
                     out.end(),
                     [](const document_view& a, const document_view& b)
                     {
                         return !a.is_world_space && b.is_world_space;
                     });
    return true;
}

/// tag#id.class1.class2 - how a selector would name the element.
auto describe_element(const Rml::Element& element) -> std::string
{
    std::string text = element.GetTagName();
    if(!element.GetId().empty())
    {
        text += "#" + element.GetId();
    }
    const auto classes = element.GetClassNames();
    size_t start = 0;
    while(start < classes.size())
    {
        const auto end = classes.find(' ', start);
        const auto name = classes.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if(!name.empty())
        {
            text += "." + name;
        }
        if(end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
    return text;
}

/// The chain from the document body down to the element.
auto element_path(const Rml::Element& element) -> std::string
{
    std::vector<std::string> parts;
    for(const auto* node = &element; node != nullptr && node != element.GetOwnerDocument(); node = node->GetParentNode())
    {
        parts.push_back(describe_element(*node));
    }
    std::string path = "body";
    for(auto it = parts.rbegin(); it != parts.rend(); ++it)
    {
        path += " > " + *it;
    }
    return path;
}

/// The text the element shows: hidden parts left out, blocks apart, runs of white space collapsed,
/// cut to a readable length.
auto element_text(const Rml::Element& element) -> std::string
{
    std::string text;
    bool in_space = false;
    for(const char c : get_ui_element_text(element, {.block_separator = " ", .visible_only = true}))
    {
        const bool is_space = c == ' ' || c == '\n' || c == '\r' || c == '\t';
        if(is_space)
        {
            in_space = !text.empty();
            continue;
        }
        if(in_space)
        {
            text += ' ';
            in_space = false;
        }
        text += c;
    }
    if(text.size() > k_max_text_length)
    {
        text = text.substr(0, k_max_text_length) + "...";
    }
    return text;
}

auto rect_json(const math::vec4& rect) -> std::string
{
    return fmt::format("[{:.1f},{:.1f},{:.1f},{:.1f}]", rect.x, rect.y, rect.z, rect.w);
}

/// The element's border box in game pixels: the bounds of its projected corners for a world-space document.
auto game_rect(const document_view& view, const math::vec4& rect) -> hpp::optional<math::vec4>
{
    const math::vec2 corners[] = {{rect.x, rect.y}, {rect.x + rect.z, rect.y}, {rect.x, rect.y + rect.w}, {rect.x + rect.z, rect.y + rect.w}};
    math::vec2 low{std::numeric_limits<float>::max()};
    math::vec2 high{std::numeric_limits<float>::lowest()};
    for(const auto& corner : corners)
    {
        math::vec2 point;
        if(!to_game_point(view, corner, point))
        {
            return std::nullopt;
        }
        low = math::min(low, point);
        high = math::max(high, point);
    }
    return math::vec4{low.x, low.y, high.x - low.x, high.y - low.y};
}

auto element_json(const document_view& view, Rml::Element& element, const std::vector<std::string>& properties) -> std::string
{
    const auto rect = get_ui_element_rect(element);
    const math::vec2 center{rect.x + rect.z * 0.5f, rect.y + rect.w * 0.5f};
    math::vec2 game_center;
    const bool has_center = to_game_point(view, center, game_center);
    const auto in_game = game_rect(view, rect);
    std::vector<std::string> active;
    for(const char* pseudo : k_reported_pseudo_classes)
    {
        if(element.IsPseudoClassSet(pseudo))
        {
            active.emplace_back(pseudo);
        }
    }
    std::string value_json = "null";
    if(auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(&element))
    {
        value_json = make_json_string(control->GetValue());
    }
    std::string properties_json = "{";
    bool first = true;
    for(const auto& name : properties)
    {
        const auto* property = element.GetProperty(name);
        properties_json += fmt::format("{}{}:{}",
                                       first ? "" : ",",
                                       make_json_string(name),
                                       property ? make_json_string(property->ToString()) : "null");
        first = false;
    }
    properties_json += "}";
    return fmt::format(
        R"({{"entity_id":{},"entity":{},"document":{},"render_mode":{},"element":{},"path":{},"text":{},)"
        R"("visible":{},"pseudo_classes":{},"value":{},"rect_document":{},"rect_game":{},"center_game":{},"properties":{}}})",
        make_json_string(entity_id_string(view.entity)),
        make_json_string(view.entity.all_of<tag_component>() ? view.entity.get<tag_component>().name : std::string()),
        make_json_string(view.component->asset.id()),
        make_json_string(view.is_world_space ? "world_space" : "screen_space_overlay"),
        make_json_string(describe_element(element)),
        make_json_string(element_path(element)),
        make_json_string(element_text(element)),
        bool_to_json(element.IsVisible(true) && is_ui_element_styled_visible(element)),
        strings_to_json_array(active),
        value_json,
        rect_json(rect),
        in_game ? rect_json(*in_game) : "null",
        has_center ? fmt::format("[{:.1f},{:.1f}]", game_center.x, game_center.y) : "null",
        properties_json);
}

/// One element a query found, with the document it is in.
struct element_hit
{
    const document_view* view{};
    Rml::Element* element{};
};

auto query_selector(const std::vector<document_view>& views, const std::string& selector, size_t limit, bool& truncated)
    -> std::vector<element_hit>
{
    std::vector<element_hit> hits;
    for(const auto& view : views)
    {
        update_ui_layout_if_stale(*view.component->document);
        Rml::ElementList found;
        view.component->document->QuerySelectorAll(found, selector);
        for(auto* element : found)
        {
            if(hits.size() == limit)
            {
                truncated = true;
                return hits;
            }
            hits.push_back({&view, element});
        }
    }
    return hits;
}

/// The topmost element under a game pixel in each document the point falls on.
auto query_point(const std::vector<document_view>& views, const math::vec2& game_point) -> std::vector<element_hit>
{
    std::vector<element_hit> hits;
    for(const auto& view : views)
    {
        math::vec2 document_point;
        if(!to_document_point(view, game_point, document_point))
        {
            continue;
        }
        if(auto* element = get_ui_element_at(*view.component->document, document_point))
        {
            hits.push_back({&view, element});
        }
    }
    return hits;
}

auto read_point(const simdjson::dom::object& args, math::vec2& out) -> bool
{
    simdjson::dom::array point;
    if(args["point"].get_array().get(point) != simdjson::SUCCESS || point.size() != 2)
    {
        return false;
    }
    double x = 0.0;
    double y = 0.0;
    if(point.at(0).get_double().get(x) != simdjson::SUCCESS || point.at(1).get_double().get(y) != simdjson::SUCCESS)
    {
        return false;
    }
    out = {static_cast<float>(x), static_cast<float>(y)};
    return true;
}

/// What a click needs, read on the main thread before any input is sent.
struct click_target
{
    std::string element_json;
    std::string covered_by;
    input::coord game_point{};
};

auto find_click_target(rtti::context& ctx, const simdjson::dom::object& args, click_target& out, std::string& error) -> bool
{
    std::string entity_id;
    read_string(args, "entity_id", entity_id);
    std::string selector;
    if(!read_string(args, "selector", selector) || selector.empty())
    {
        error = "Pass selector (an RCSS selector such as \"#start\" or \".demo\")";
        return false;
    }
    const auto index = read_int64_or(args, "index", 0);
    std::vector<document_view> views;
    if(!collect_document_views(ctx, entity_id, views, error))
    {
        return false;
    }
    bool truncated = false;
    const auto hits = query_selector(views, selector, static_cast<size_t>(std::max<int64_t>(index, 0) + 1), truncated);
    if(index < 0 || static_cast<size_t>(index) >= hits.size())
    {
        error = fmt::format("No element {} for selector \"{}\" ({} found) - see ui_query_elements", index, selector, hits.size());
        return false;
    }
    const auto& hit = hits[static_cast<size_t>(index)];
    auto& element = *hit.element;
    if(!element.IsVisible(true) || !is_ui_element_styled_visible(element))
    {
        error = "The element is hidden: " + element_path(element);
        return false;
    }
    const auto rect = get_ui_element_rect(element);
    const math::vec2 center{rect.x + rect.z * 0.5f, rect.y + rect.w * 0.5f};
    math::vec2 game_center;
    if(!to_game_point(*hit.view, center, game_center))
    {
        error = "The element's document is behind the camera: " + element_path(element);
        return false;
    }
    // Another element drawn over the centre takes the click instead: say which.
    if(auto* top = get_ui_element_at(*hit.view->component->document, center); top != nullptr && top != &element &&
                                                                             !element.Contains(top))
    {
        out.covered_by = element_path(*top);
    }
    out.element_json = element_json(*hit.view, element, {});
    out.game_point = {static_cast<int>(game_center.x), static_cast<int>(game_center.y)};
    return true;
}

} // namespace

void register_ui_inspect_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "ui_query_elements",
         .description =
             "Find elements of the UI documents on screen by RCSS selector (\"#start\", \"button\"), or the "
             "topmost element under a point [x, y] in game pixels - the one a click there reaches. Without "
             "entity_id every document of the active scene is searched. Each element reports its path, text, "
             "visibility, state (hover, focus, checked, disabled, ...), form value, rect_game / center_game in "
             "the game pixels input_send_mouse uses, and the computed values of the properties you list. Edit and "
             "play mode.",
         .input_schema_json =
             R"json({"type":"object","properties":{"selector":{"type":"string"},"point":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2,"description":"Game pixels"},"entity_id":{"type":"string","description":"An entity with a UI Document; default all"},"properties":{"type":"array","items":{"type":"string"},"description":"RCSS properties to read, e.g. decorator, display, image-color"},"limit":{"type":"integer","minimum":1,"maximum":200,"description":"Default 25"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             std::string entity_id;
             read_string(args, "entity_id", entity_id);
             std::string selector;
             read_string(args, "selector", selector);
             math::vec2 point;
             const bool has_point = read_point(args, point);
             if(selector.empty() == !has_point)
             {
                 return {.text = "Pass selector or point [x, y] (game pixels), not both", .is_error = true};
             }
             std::vector<std::string> properties;
             if(!read_string_array(args, "properties", properties, error, false))
             {
                 return {.text = error, .is_error = true};
             }
             const auto limit =
                 static_cast<size_t>(std::min(read_clamped_int64(args, "limit", k_default_query_limit, 1), k_max_query_limit));
             std::vector<document_view> views;
             if(!collect_document_views(ctx, entity_id, views, error))
             {
                 return {.text = error, .is_error = true};
             }
             bool truncated = false;
             const auto hits = has_point ? query_point(views, point) : query_selector(views, selector, limit, truncated);
             std::string elements = "[";
             for(size_t i = 0; i < hits.size(); ++i)
             {
                 elements += (i > 0 ? "," : "") + element_json(*hits[i].view, *hits[i].element, properties);
             }
             elements += "]";
             return {.text = fmt::format(R"({{"elements":{},"count":{},"documents":{},"truncated":{}}})",
                                         elements,
                                         hits.size(),
                                         views.size(),
                                         bool_to_json(truncated))};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "ui_click_element",
         .description =
             "Click a UI element found by RCSS selector (index picks among matches): moves the virtual pointer to "
             "its centre, waits for hover, then presses and releases the button. text types characters after the "
             "click, into the field it focused. Reports the element, the game pixel clicked, and covered_by when "
             "another element sits over the centre (that one gets the click). Play mode only; the pointer stays "
             "there until input_send_mouse clear:true.",
         .input_schema_json =
             R"json({"type":"object","properties":{"selector":{"type":"string"},"entity_id":{"type":"string","description":"An entity with a UI Document; default all"},"index":{"type":"integer","minimum":0,"description":"Default 0"},"button":{"type":"string","enum":["left","right","middle"],"description":"Default left"},"text":{"type":"string","description":"Characters to type after the click"},"focus_game_panel":{"type":"boolean","description":"Default true"}},"required":["selector"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             std::string button_name = "left";
             read_string(args, "button", button_name);
             if(button_name != "left" && button_name != "right" && button_name != "middle")
             {
                 return {.text = "button must be left, right or middle", .is_error = true};
             }
             const auto button = button_name == "right"    ? os::mouse::button::right
                                 : button_name == "middle" ? os::mouse::button::middle
                                                           : os::mouse::button::left;
             std::string text;
             read_string(args, "text", text);
             // Filled on the main thread; invoke_on_main returns once it ran.
             click_target target;
             auto found = mcp.invoke_on_main(
                 [&ctx, &args, &target]() -> tool_result
                 {
                     if(!editor_actions::get_play_state(ctx).is_active)
                     {
                         return {.text = "Play mode only: clicks go through the virtual pointer, which only "
                                         "exists while the game plays",
                                 .is_error = true};
                     }
                     std::string error;
                     if(!find_click_target(ctx, args, target, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     return {.text = "ok"};
                 });
             if(!found)
             {
                 return {.text = "Timed out reaching the editor main thread", .is_error = true};
             }
             if(found->is_error)
             {
                 return *found;
             }
             bool focus_game_panel = true;
             read_bool(args, "focus_game_panel", focus_game_panel);
             uint32_t window = 0;
             std::string error;
             if(!prepare_game_input(mcp, ctx, focus_game_panel, window, error))
             {
                 return {.text = error, .is_error = true};
             }
             const auto moved = mcp.invoke_on_main(
                 [&]() -> bool
                 {
                     return move_game_pointer(ctx, window, target.game_point, error);
                 });
             if(!moved || !*moved)
             {
                 return {.text = error.empty() ? "Timed out reaching the editor main thread" : error, .is_error = true};
             }
             if(!wait_input_frames(ctx, k_hover_frames) || !click_mouse_button(mcp, ctx, window, button, 1, error))
             {
                 return {.text = error.empty() ? "The editor stopped advancing frames before the click" : error,
                         .is_error = true};
             }
             if(!text.empty() && (!push_on_main(mcp,
                                                [&]
                                                {
                                                    push_text_input_event(window, text);
                                                }) ||
                                  !wait_input_frames(ctx, 1)))
             {
                 return {.text = "The click landed but the text could not be typed", .is_error = true};
             }
             return {.text = fmt::format(R"({{"ok":true,"game_point":[{},{}],"element":{},"covered_by":{},"typed":{}}})",
                                         target.game_point.x,
                                         target.game_point.y,
                                         target.element_json,
                                         target.covered_by.empty() ? "null" : make_json_string(target.covered_by),
                                         bool_to_json(!text.empty()))};
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "ui_get_model",
         .description =
             "Read the data models scripts bound for an entity's UI Document: each model's variables with "
             "their current values as the markup sees them (enums by name, colours as RCSS, objects by "
             "their public members, lists as arrays), its event names (data-event-*) and transform names "
             "(| name). Models are bound by scripts, so they exist in play mode. model narrows to one; "
             "depth (default 4) expands nested objects and lists; max_items (default 20) caps each list.",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_id":{"type":"string"},"model":{"type":"string"},"depth":{"type":"integer","minimum":0,"maximum":8},"max_items":{"type":"integer","minimum":1,"maximum":200}},"required":["entity_id"]})json",
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
             if(!entity.all_of<ui_document_component>())
             {
                 return {.text = "Entity has no UI Document: " + entity_id, .is_error = true};
             }
             std::string model;
             read_string(args, "model", model);
             const auto depth = std::min(read_clamped_int64(args, "depth", k_default_model_depth, 0), k_max_model_depth);
             const auto items = std::min(read_clamped_int64(args, "max_items", k_default_model_items, 1), k_max_model_items);
             const auto models = ctx.get_cached<script_system>().describe_ui_models(entity.entity(),
                                                                                    model,
                                                                                    static_cast<int>(depth),
                                                                                    static_cast<int>(items));
             if(models == "null")
             {
                 return {.text = "Scripting is not loaded", .is_error = true};
             }
             return {.text = fmt::format(R"({{"entity_id":{},"play_active":{},"result":{}}})",
                                         make_json_string(entity_id),
                                         bool_to_json(editor_actions::get_play_state(ctx).is_active),
                                         models)};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});
}

} // namespace unravel::mcp
