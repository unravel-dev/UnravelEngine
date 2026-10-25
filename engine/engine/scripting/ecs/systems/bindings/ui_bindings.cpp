// UIDocumentComponent, UIDocument, UIElement, and UI event registries (shared Rml helpers).
#include "script_glue_common.h"
#include "script_bindings.h"
#include "ui_event_payload.h"

#include "../script_interop.h"
#include "../script_system.h"

#include <engine/assets/asset_manager.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ui/ecs/components/ui_document_component.h>
#include <engine/ui/ecs/systems/ui_system.h>
#include <engine/ui/rmlui/RmlUi_SystemInterface.h>
#include <engine/ui/ui_element_handles.h>
#include <engine/ui/ui_element_queries.h>
#include <hpp/finally.hpp>
#include <RmlUi/Core/Box.h>
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Elements/ElementFormControlTextArea.h>
#include <RmlUi/Core/Elements/ElementProgress.h>
#include <RmlUi/Core/Elements/ElementTabSet.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/StyleSheetSpecification.h>
#include <RmlUi/Core/Tween.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace unravel
{
namespace
{

//-------------------------------------------------------------------------
/*

  _    _ _____   _____           _____  _    _ __  __ ______ _   _ _______
 | |  | |_   _| |  __ \   /\    / ____|  |  | |  \/  |  ____| \ | |__   __|
 | |  | | | |   | |  | | /  \  | |    | |  | | \  / | |__  |  \| |  | |
 | |  | | | |   | |  | |/ /\ \ | |    | |  | | |\/| |  __| | . ` |  | |
 | |__| |_| |_  | |__| / ____ \| |____| |__| | |  | | |____| |\  |  | |
  \____/|_____| |_____/_/    \_\\_____|\____/|_|  |_|______|_| \_|  |_|


*/
//-------------------------------------------------------------------------

/// Closes the component's document for good: it stays closed until a script reloads it.
void close_ui_document(ui_document_component& comp)
{
    if(comp.document)
    {
        comp.document->Close();
        comp.document = nullptr;
    }
    comp.closed = true;
}

auto internal_m2n_ui_document_get_asset(entt::entity id) -> hpp::uuid
{
    if(auto comp = safe_get_component<ui_document_component>(id))
    {
        return comp->asset.uid();
    }

    return {};
}

void internal_m2n_ui_document_set_asset(entt::entity id, const hpp::uuid& uid)
{
    if(auto comp = safe_get_component<ui_document_component>(id))
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();

        auto asset = am.get_asset<ui_tree>(uid);
        comp->asset = asset;
    }
}

auto internal_m2n_ui_document_is_loaded(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<ui_document_component>(id))
    {
        return comp->is_loaded();
    }

    return false;
}

auto internal_m2n_ui_document_is_enabled(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<ui_document_component>(id))
    {
        return comp->is_enabled();
    }

    return false;
}

void internal_m2n_ui_document_set_enabled(entt::entity id, bool enabled)
{
    if(auto comp = safe_get_component<ui_document_component>(id))
    {
        comp->set_enabled(enabled);
    }
}
void internal_m2n_ui_document_close(entt::entity id)
{
    if(auto comp = safe_get_component<ui_document_component>(id))
    {
        close_ui_document(*comp);
    }
}

auto internal_m2n_ui_document_reload(entt::entity id) -> bool
{
    auto handle = get_entity_from_id(id);
    auto* comp = handle ? handle.try_get<ui_document_component>() : nullptr;
    if(!comp)
    {
        return false;
    }
    close_ui_document(*comp);
    comp->closed = false;
    mark_ui_layout_stale();
    return engine::context().get_cached<ui_system>().load_ui_document(handle, *comp, true);
}

auto internal_m2n_ui_document_get_title(entt::entity id) -> const std::string&
{
    if(auto comp = safe_get_component<ui_document_component>(id))
    {
        if(comp->document)
        {
            return comp->document->GetTitle();
        }
    }

    static const std::string empty;
    return empty;
}

void internal_m2n_ui_document_set_title(entt::entity id, const std::string& title)
{
    if(auto comp = safe_get_component<ui_document_component>(id))
    {
        if(comp->document)
        {
            comp->document->SetTitle(title);
        }
    }
}

//-------------------------------------------------------------------------
/*

  ______ _    _ ______ _   _ _______    _____          _      _      ____          _____ _  __ _____
 |  ____| |  | |  ____| \ | |__   __|  / ____|   /\   | |    | |    |  _ \   /\   / ____| |/ // ____|
 | |__  | |  | | |__  |  \| |  | |    | |       /  \  | |    | |    | |_) | /  \ | |    | ' /| (___
 |  __| | |  | |  __| | . ` |  | |    | |      / /\ \ | |    | |    |  _ < / /\ \| |    |  <  \___ \
 | |____| |__| | |____| |\  |  | |    | |____ / ____ \| |____| |____| |_) / ____ \ |____| . \ ____) |
 |______|\____/|______|_| \_|  |_|     \_____/_/    \_\______|______|____/_/    \_\_____|_|\_\_____/


*/
//-------------------------------------------------------------------------

using ui_event_payload = script_system::ui_event_payload;

/// Event classes as UIEventManager knows them; the values mirror the managed UIEventKind.
enum class ui_event_kind : int32_t
{
    base,
    pointer,
    click,
    key,
    text_input,
    change,
    slider,
    focus,
    scroll,
    animation,
    submit,
    drag
};

constexpr int32_t MODIFIER_CTRL = 1;
constexpr int32_t MODIFIER_SHIFT = 2;
constexpr int32_t MODIFIER_ALT = 4;
constexpr int32_t MODIFIER_META = 8;

/// Separators of the submitted form values in an event's text: name, unit separator, value,
/// record separator.
constexpr char SUBMIT_NAME_SEPARATOR = '\x1f';
constexpr char SUBMIT_FIELD_SEPARATOR = '\x1e';

auto is_pointer_event(Rml::EventId id) -> bool
{
    return id == Rml::EventId::Mousedown || id == Rml::EventId::Mouseup || id == Rml::EventId::Mousemove ||
           id == Rml::EventId::Mouseover || id == Rml::EventId::Mouseout || id == Rml::EventId::Mousescroll ||
           id == Rml::EventId::Handledrag;
}

auto is_drag_event(Rml::EventId id) -> bool
{
    return id == Rml::EventId::Drag || id == Rml::EventId::Dragstart || id == Rml::EventId::Dragend ||
           id == Rml::EventId::Dragover || id == Rml::EventId::Dragdrop || id == Rml::EventId::Dragout ||
           id == Rml::EventId::Dragmove;
}

auto get_change_event_kind(const Rml::Event& event) -> ui_event_kind
{
    const auto* element = event.GetCurrentElement();
    const bool is_range = element && (element->HasAttribute("min") || element->HasAttribute("max"));
    return is_range ? ui_event_kind::slider : ui_event_kind::change;
}

/// The event class an RmlUi event becomes, from its id.
auto get_ui_event_kind(const Rml::Event& event) -> ui_event_kind
{
    const auto id = event.GetId();
    switch(id)
    {
        case Rml::EventId::Click:
        case Rml::EventId::Dblclick:
            return ui_event_kind::click;
        case Rml::EventId::Keydown:
        case Rml::EventId::Keyup:
            return ui_event_kind::key;
        case Rml::EventId::Textinput:
            return ui_event_kind::text_input;
        case Rml::EventId::Change:
            return get_change_event_kind(event);
        case Rml::EventId::Focus:
        case Rml::EventId::Blur:
            return ui_event_kind::focus;
        case Rml::EventId::Scroll:
            return ui_event_kind::scroll;
        case Rml::EventId::Animationend:
        case Rml::EventId::Transitionend:
            return ui_event_kind::animation;
        case Rml::EventId::Submit:
            return ui_event_kind::submit;
        default:
            break;
    }
    if(is_drag_event(id))
    {
        return ui_event_kind::drag;
    }
    return is_pointer_event(id) ? ui_event_kind::pointer : ui_event_kind::base;
}

auto get_event_modifiers(const Rml::Event& event) -> int32_t
{
    int32_t modifiers = 0;
    modifiers |= event.GetParameter<int>("ctrl_key", 0) > 0 ? MODIFIER_CTRL : 0;
    modifiers |= event.GetParameter<int>("shift_key", 0) > 0 ? MODIFIER_SHIFT : 0;
    modifiers |= event.GetParameter<int>("alt_key", 0) > 0 ? MODIFIER_ALT : 0;
    modifiers |= event.GetParameter<int>("meta_key", 0) > 0 ? MODIFIER_META : 0;
    return modifiers;
}

void fill_pointer_payload(ui_event_payload& payload, const Rml::Event& event)
{
    payload.x = event.GetParameter<float>("mouse_x", 0.0f);
    payload.y = event.GetParameter<float>("mouse_y", 0.0f);
    payload.button = event.GetParameter<int>("button", -1);
    payload.delta_x = event.GetParameter<float>("wheel_delta_x", 0.0f);
    payload.delta_y = event.GetParameter<float>("wheel_delta_y", 0.0f);
}

void fill_slider_payload(ui_event_payload& payload, const Rml::Event& event)
{
    payload.value = event.GetParameter<float>("value", 0.0f);
    if(const auto* slider = event.GetCurrentElement())
    {
        payload.min_value = slider->GetAttribute<float>("min", 0.0f);
        payload.max_value = slider->GetAttribute<float>("max", 0.0f);
        payload.step = slider->GetAttribute<float>("step", 0.0f);
    }
}

/// The submitted form values as "name US value RS" records.
auto encode_submit_values(const Rml::Event& event) -> std::string
{
    std::string text;
    for(const auto& [name, value] : event.GetParameters())
    {
        text += name;
        text += SUBMIT_NAME_SEPARATOR;
        text += value.Get<Rml::String>();
        text += SUBMIT_FIELD_SEPARATOR;
    }
    return text;
}

/// Fills the class-specific payload fields.
/// @return The event's text: typed text, a changed value, an animated property or form values.
auto fill_kind_payload(ui_event_payload& payload, ui_event_kind kind, const Rml::Event& event) -> std::string
{
    switch(kind)
    {
        case ui_event_kind::pointer:
            fill_pointer_payload(payload, event);
            return {};
        case ui_event_kind::click:
            fill_pointer_payload(payload, event);
            payload.click_count = event.GetId() == Rml::EventId::Dblclick ? 2 : 1;
            return {};
        case ui_event_kind::drag:
            fill_pointer_payload(payload, event);
            payload.related_handle =
                get_ui_element_handle(static_cast<Rml::Element*>(event.GetParameter<void*>("drag_element", nullptr)));
            return {};
        case ui_event_kind::key:
            payload.key_code = static_cast<int32_t>(RmlEngine::convert_rml_key_to_input(
                static_cast<Rml::Input::KeyIdentifier>(event.GetParameter<int>("key_identifier", 0))));
            return {};
        case ui_event_kind::text_input:
            return event.GetParameter<Rml::String>("text", "");
        case ui_event_kind::change:
            return event.GetParameter<Rml::String>("value", "");
        case ui_event_kind::slider:
            fill_slider_payload(payload, event);
            return event.GetParameter<Rml::String>("value", "");
        case ui_event_kind::animation:
            return event.GetParameter<Rml::String>("property", "");
        case ui_event_kind::submit:
            return encode_submit_values(event);
        default:
            return {};
    }
}

void dispatch_ui_event(const Rml::Event& event, Rml::Element& target, Rml::Element& current, bool is_capture_listener)
{
    // Copied: invoking is not const, and the cache is shared.
    auto invoker = engine::context().get_cached<script_system>().get_cache().ui_dispatch_invoker;
    if(!invoker.valid())
    {
        return;
    }
    std::string text;
    const auto payload = make_ui_event_payload(event, is_capture_listener, text);
    try
    {
        invoker(payload, event.GetType(), target.GetId(), current.GetId(), std::move(text));
    }
    catch(const dotnet::exception& e)
    {
        APPLOG_ERROR("C# exception dispatching UI event: {}", e.what());
    }
}

/// Events being dispatched to scripts, innermost last: a callback can dispatch another event (Click()).
std::vector<Rml::Event*> g_dispatching_events;

/// A control a script sets without notifying: its "change" events do not reach scripts meanwhile.
Rml::Element* g_silenced_change_target = nullptr;

auto is_silenced_change(const Rml::Event& event) -> bool
{
    return g_silenced_change_target && event.GetId() == Rml::EventId::Change &&
           event.GetTargetElement() == g_silenced_change_target;
}

/// Forwards RmlUi events to UIEventManager. One instance listens in the bubble phase, one in the
/// capture phase, so scripts can subscribe to either.
class ui_global_event_listener : public Rml::EventListener
{
    bool is_capture_ = false;
public:
    explicit ui_global_event_listener(bool is_capture) : is_capture_(is_capture)
    {
    }

    void ProcessEvent(Rml::Event& event) override
    {
        auto* target = event.GetTargetElement();
        auto* current = event.GetCurrentElement();
        if(!target || !current || is_silenced_change(event))
        {
            return;
        }
        g_dispatching_events.push_back(&event);
        auto pop_event = hpp::finally([]() { g_dispatching_events.pop_back(); });
        dispatch_ui_event(event, *target, *current, is_capture_);
    }
};

ui_global_event_listener g_ui_bubble_listener(false);
ui_global_event_listener g_ui_capture_listener(true);

/// The event @p native_ptr names while it is being dispatched, or null once its dispatch ended.
auto find_dispatching_event(std::intptr_t native_ptr) -> Rml::Event*
{
    auto* event = reinterpret_cast<Rml::Event*>(native_ptr);
    auto it = std::find(g_dispatching_events.begin(), g_dispatching_events.end(), event);
    return it != g_dispatching_events.end() ? *it : nullptr;
}

auto get_listener(bool capture) -> ui_global_event_listener*
{
    return capture ? &g_ui_capture_listener : &g_ui_bubble_listener;
}

// Ensure a native event listener is attached to the element for the given event type
void internal_m2n_ui_ensure_native_event_listener(std::intptr_t element_handle, const std::string& event_type, bool capture)
{
    auto* element = resolve_ui_element_handle(element_handle);
    if (!element)
    {
        return;
    }
    element->AddEventListener(event_type, get_listener(capture), capture);
    if(is_ui_pointer_press_event(event_type))
    {
        add_ui_script_pointer_listener(element);
    }
    APPLOG_TRACE("Ensured native UI event listener: element='{}', event='{}'", element->GetId(), event_type);
    // RmlUi sends a click to the nearest element that can take focus: under 'focus: none'
    // (an inherited property) that is an ancestor, and the event never reaches this element.
    if(event_type == "click" && element->GetComputedValues().focus() == Rml::Style::Focus::None)
    {
        APPLOG_WARNING("UI: the click listener on <{} id='{}'> never fires: its computed 'focus' is "
                       "'none', so clicks go to an ancestor. Remove 'focus: none' or listen for 'mouseup'.",
                       element->GetTagName(),
                       element->GetId());
    }
}

void internal_m2n_ui_remove_native_event_listener(std::intptr_t element_handle, const std::string& event_type, bool capture)
{
    // A destroyed element took its listeners with it.
    if(auto* element = resolve_ui_element_handle(element_handle))
    {
        element->RemoveEventListener(event_type, get_listener(capture), capture);
        if(is_ui_pointer_press_event(event_type))
        {
            remove_ui_script_pointer_listener(element);
        }
        APPLOG_TRACE("Removed native UI event listener: element='{}', event='{}'", element->GetId(), event_type);
    }
}

auto internal_m2n_ui_take_destroyed_handle() -> std::intptr_t
{
    return take_destroyed_ui_element_handle();
}

// Stop event propagation - called from C# UIEventBase.StopPropagation()
void internal_m2n_ui_stop_propagation(std::intptr_t native_ptr)
{
    if(auto* event = find_dispatching_event(native_ptr))
    {
        event->StopPropagation();
    }
    else
    {
        APPLOG_WARNING("UI: StopPropagation called after the event's dispatch ended");
    }
}

// Stop immediate event propagation - called from C# UIEventBase.StopImmediatePropagation()
void internal_m2n_ui_stop_immediate_propagation(std::intptr_t native_ptr)
{
    if(auto* event = find_dispatching_event(native_ptr))
    {
        event->StopImmediatePropagation();
    }
    else
    {
        APPLOG_WARNING("UI: StopImmediatePropagation called after the event's dispatch ended");
    }
}

//-------------------------------------------------------------------------
/*

  _    _ _____  __          _______             _____  _____  ______ _____    _____
 | |  | |_   _| \ \        / /  __ \     /\    |  __ \|  __ \|  ____|  __ \  / ____|
 | |  | | | |    \ \  /\  / /| |__) |   /  \   | |__) | |__) | |__  | |__) || (___
 | |  | | | |     \ \/  \/ / |  _  /   / /\ \  |  ___/|  ___/|  __| |  _  /  \___ \
 | |__| |_| |_     \  /\  /  | | \ \  / ____ \ | |    | |    | |____| | \ \  ____) |
  \____/|_____|     \/  \/   |_|  \_\/_/    \_\|_|    |_|    |______|_|  \_\|_____/


*/
//-------------------------------------------------------------------------

// Wrappers hold element handles: every call resolves its handle first, and a handle whose element
// was destroyed resolves to null, so the call does nothing and returns a default.

using handle_list = std::vector<int64_t>;

/// Resolves an element a call is about to change: layout read afterwards must update first.
auto resolve_for_change(std::intptr_t element_handle) -> Rml::Element*
{
    auto* element = resolve_ui_element_handle(element_handle);
    if(element)
    {
        mark_ui_layout_stale();
    }
    return element;
}

/// The document @p document_handle names while it is still @p owner_entity's open document.
auto resolve_ui_document(std::intptr_t document_handle, entt::entity owner_entity) -> Rml::ElementDocument*
{
    auto* element = resolve_ui_element_handle(document_handle);
    if(!element)
    {
        return nullptr;
    }
    // A closed document lives on until its context releases it; to scripts it is gone at once.
    auto entity = get_entity_from_id(owner_entity);
    auto* comp = entity ? entity.try_get<ui_document_component>() : nullptr;
    if(!comp || comp->document != element)
    {
        return nullptr;
    }
    return comp->document;
}

/// Resolves an element whose layout a call reads, with that layout current.
auto resolve_for_layout(std::intptr_t element_handle) -> Rml::Element*
{
    auto* element = resolve_ui_element_handle(element_handle);
    if(element)
    {
        if(auto* document = element->GetOwnerDocument())
        {
            update_ui_layout_if_stale(*document);
        }
    }
    return element;
}

auto to_handles(const Rml::ElementList& elements) -> handle_list
{
    handle_list handles;
    handles.reserve(elements.size());
    for(auto* element : elements)
    {
        handles.push_back(get_ui_element_handle(element));
    }
    return handles;
}

auto is_document(Rml::Element& element) -> bool
{
    return rmlui_dynamic_cast<Rml::ElementDocument*>(&element) != nullptr;
}

//-------------------------------------------------------------------------
// UI Document Wrapper Functions
//-------------------------------------------------------------------------

auto internal_m2n_ui_document_get_wrapper(entt::entity entity_id) -> std::intptr_t
{
    if (auto comp = safe_get_component<ui_document_component>(entity_id))
    {
        return get_ui_element_handle(comp->document);
    }
    return 0;
}

auto internal_m2n_ui_document_get_element_wrapper_by_id(std::intptr_t document_handle, entt::entity owner_entity, const std::string& element_id) -> std::intptr_t
{
    auto* document = resolve_ui_document(document_handle, owner_entity);
    return document ? get_ui_element_handle(document->GetElementById(element_id)) : 0;
}

auto internal_m2n_ui_document_query_selector_wrapper(std::intptr_t document_handle, entt::entity owner_entity, const std::string& selector) -> std::intptr_t
{
    auto* document = resolve_ui_document(document_handle, owner_entity);
    return document ? get_ui_element_handle(document->QuerySelector(selector)) : 0;
}

//-------------------------------------------------------------------------
// UI Document Wrapper Methods
//-------------------------------------------------------------------------

auto internal_m2n_ui_document_wrapper_is_valid(std::intptr_t document_handle, entt::entity owner_entity) -> bool
{
    return resolve_ui_document(document_handle, owner_entity) != nullptr;
}

auto internal_m2n_ui_document_wrapper_get_title(std::intptr_t document_handle, entt::entity owner_entity) -> std::string
{
    auto* document = resolve_ui_document(document_handle, owner_entity);
    return document ? document->GetTitle() : std::string();
}

void internal_m2n_ui_document_wrapper_set_title(std::intptr_t document_handle, entt::entity owner_entity, const std::string& title)
{
    if(auto* document = resolve_ui_document(document_handle, owner_entity))
    {
        document->SetTitle(title);
    }
}

auto internal_m2n_ui_document_wrapper_is_visible(std::intptr_t document_handle, entt::entity owner_entity) -> bool
{
    auto* document = resolve_ui_document(document_handle, owner_entity);
    return document && document->IsVisible();
}

void internal_m2n_ui_document_wrapper_show(std::intptr_t document_handle, entt::entity owner_entity)
{
    if(auto* document = resolve_ui_document(document_handle, owner_entity))
    {
        document->Show();
    }
}

void internal_m2n_ui_document_wrapper_hide(std::intptr_t document_handle, entt::entity owner_entity)
{
    if(auto* document = resolve_ui_document(document_handle, owner_entity))
    {
        document->Hide();
    }
}

void internal_m2n_ui_document_wrapper_close(std::intptr_t document_handle, entt::entity owner_entity)
{
    if(resolve_ui_document(document_handle, owner_entity))
    {
        close_ui_document(*safe_get_component<ui_document_component>(owner_entity));
    }
}

auto internal_m2n_ui_document_wrapper_query_selector_all(std::intptr_t document_handle, entt::entity owner_entity, const std::string& selector) -> handle_list
{
    Rml::ElementList elements;
    if(auto* document = resolve_ui_document(document_handle, owner_entity))
    {
        document->QuerySelectorAll(elements, selector);
    }
    return to_handles(elements);
}

auto internal_m2n_ui_document_wrapper_create_element(std::intptr_t document_handle, entt::entity owner_entity, const std::string& tag) -> std::intptr_t
{
    auto* document = resolve_ui_document(document_handle, owner_entity);
    if(!document)
    {
        return 0;
    }
    return get_ui_element_handle(adopt_orphan_ui_element(document, document->CreateElement(tag)));
}

auto internal_m2n_ui_document_wrapper_element_at(std::intptr_t document_handle, entt::entity owner_entity, const math::vec2& point) -> std::intptr_t
{
    auto* document = resolve_ui_document(document_handle, owner_entity);
    return document ? get_ui_element_handle(get_ui_element_at(*document, point)) : 0;
}

auto internal_m2n_ui_document_wrapper_get_dp_ratio(std::intptr_t document_handle, entt::entity owner_entity) -> float
{
    auto* document = resolve_ui_document(document_handle, owner_entity);
    auto* context = document ? document->GetContext() : nullptr;
    return context ? context->GetDensityIndependentPixelRatio() : 1.0f;
}

void internal_m2n_ui_document_wrapper_update_layout(std::intptr_t document_handle, entt::entity owner_entity)
{
    if(auto* document = resolve_ui_document(document_handle, owner_entity))
    {
        document->UpdateDocument();
    }
}

//-------------------------------------------------------------------------
// UI Element Wrapper Methods
//-------------------------------------------------------------------------

auto internal_m2n_ui_element_wrapper_is_valid(std::intptr_t element_handle) -> bool
{
    return resolve_ui_element_handle(element_handle) != nullptr;
}

auto internal_m2n_ui_element_wrapper_get_id(std::intptr_t element_handle) -> std::string
{
    auto* element = resolve_ui_element_handle(element_handle);
    return element ? element->GetId() : std::string();
}

auto internal_m2n_ui_element_wrapper_get_tag_name(std::intptr_t element_handle) -> std::string
{
    auto* element = resolve_ui_element_handle(element_handle);
    return element ? element->GetTagName() : std::string();
}

auto internal_m2n_ui_element_wrapper_get_inner_rml(std::intptr_t element_handle) -> std::string
{
    auto* element = resolve_ui_element_handle(element_handle);
    return element ? element->GetInnerRML() : std::string();
}

void internal_m2n_ui_element_wrapper_set_inner_rml(std::intptr_t element_handle, const std::string& rml)
{
    auto* element = resolve_ui_element_handle(element_handle);
    // Rebuilding identical content would destroy the children and every handle to them for nothing.
    if(element && element->GetInnerRML() != rml)
    {
        mark_ui_layout_stale();
        element->SetInnerRML(rml);
    }
}

void internal_m2n_ui_element_wrapper_set_text(std::intptr_t element_handle, const std::string& text)
{
    internal_m2n_ui_element_wrapper_set_inner_rml(element_handle, Rml::StringUtilities::EncodeRml(text));
}

auto internal_m2n_ui_element_wrapper_get_text(std::intptr_t element_handle) -> std::string
{
    auto* element = resolve_ui_element_handle(element_handle);
    return element ? get_ui_element_text(*element) : std::string();
}

auto internal_m2n_ui_element_wrapper_is_visible(std::intptr_t element_handle) -> bool
{
    auto* element = resolve_ui_element_handle(element_handle);
    return element && is_ui_element_styled_visible(*element);
}

void internal_m2n_ui_element_wrapper_set_visible(std::intptr_t element_handle, bool visible)
{
    auto* element = resolve_for_change(element_handle);
    if(!element)
    {
        return;
    }
    if(!visible)
    {
        element->SetProperty(Rml::PropertyId::Display, Rml::Property(Rml::Style::Display::None));
        return;
    }
    // Dropping the inline value gives the element its stylesheet display back (flex, inline-block);
    // only an element its stylesheet hides needs one written.
    element->RemoveProperty(Rml::PropertyId::Display);
    if(const auto* display = element->GetProperty(Rml::PropertyId::Display);
       display && display->Get<int>() == static_cast<int>(Rml::Style::Display::None))
    {
        element->SetProperty(Rml::PropertyId::Display, Rml::Property(Rml::Style::Display::Block));
    }
}

auto internal_m2n_ui_element_wrapper_get_attribute(std::intptr_t element_handle, const std::string& attribute_name) -> std::string
{
    auto* element = resolve_ui_element_handle(element_handle);
    return element ? element->GetAttribute<Rml::String>(attribute_name, "") : std::string();
}

void internal_m2n_ui_element_wrapper_set_attribute(std::intptr_t element_handle, const std::string& attribute_name, const std::string& value)
{
    if(auto* element = resolve_for_change(element_handle))
    {
        element->SetAttribute(attribute_name, value);
    }
}

void internal_m2n_ui_element_wrapper_remove_attribute(std::intptr_t element_handle, const std::string& attribute_name)
{
    if(auto* element = resolve_for_change(element_handle))
    {
        element->RemoveAttribute(attribute_name);
    }
}

auto internal_m2n_ui_element_wrapper_has_attribute(std::intptr_t element_handle, const std::string& attribute_name) -> bool
{
    auto* element = resolve_ui_element_handle(element_handle);
    return element && element->HasAttribute(attribute_name);
}

void internal_m2n_ui_element_wrapper_set_class(std::intptr_t element_handle, const std::string& class_name, bool activate)
{
    if(auto* element = resolve_for_change(element_handle))
    {
        element->SetClass(class_name, activate);
    }
}

auto internal_m2n_ui_element_wrapper_is_class_set(std::intptr_t element_handle, const std::string& class_name) -> bool
{
    auto* element = resolve_ui_element_handle(element_handle);
    return element && element->IsClassSet(class_name);
}

void internal_m2n_ui_element_wrapper_set_pseudo_class(std::intptr_t element_handle, const std::string& pseudo_class, bool activate)
{
    if(auto* element = resolve_for_change(element_handle))
    {
        element->SetPseudoClass(pseudo_class, activate);
    }
}

auto internal_m2n_ui_element_wrapper_is_pseudo_class_set(std::intptr_t element_handle, const std::string& pseudo_class) -> bool
{
    auto* element = resolve_ui_element_handle(element_handle);
    return element && element->IsPseudoClassSet(pseudo_class);
}

void internal_m2n_ui_element_wrapper_sync_transform_to_entity(std::intptr_t element_handle, entt::entity transform_entity)
{
    auto transform = get_entity_from_id(transform_entity);
    if (!transform)
    {
        return;
    }

    auto* transform_comp = transform.try_get<transform_component>();
    if (!transform_comp)
    {
        return;
    }

    auto* element = resolve_for_change(element_handle);
    if (!element)
    {
        return;
    }

    const auto& matrix = transform_comp->get_transform_global().get_matrix();
    std::stringstream s;

    float perspective = transform_comp->get_perspective_global().w;
    if (perspective > 0)
    {
        s << "perspective(" << perspective << "dp) ";
    }
    s << "matrix3d(" << matrix[0][0] << ", " << matrix[0][1] << ", " << matrix[0][2] << ", " << matrix[0][3] << ", " << matrix[1][0] << ", " << matrix[1][1] << ", " << matrix[1][2] << ", " << matrix[1][3] << ", " << matrix[2][0] << ", " << matrix[2][1] << ", " << matrix[2][2] << ", " << matrix[2][3] << ", " << matrix[3][0] << ", " << matrix[3][1] << ", " << matrix[3][2] << ", " << matrix[3][3] << ")";
    element->SetProperty("transform", s.str());
}

void internal_m2n_ui_element_wrapper_focus(std::intptr_t element_handle)
{
    if(auto* element = resolve_for_change(element_handle))
    {
        element->Focus();
    }
}

void internal_m2n_ui_element_wrapper_blur(std::intptr_t element_handle)
{
    if(auto* element = resolve_for_change(element_handle))
    {
        element->Blur();
    }
}

void internal_m2n_ui_element_wrapper_click(std::intptr_t element_handle)
{
    if(auto* element = resolve_ui_element_handle(element_handle))
    {
        element->Click();
    }
}

void internal_m2n_ui_element_wrapper_scroll_into_view(std::intptr_t element_handle, bool align_with_top)
{
    if(auto* element = resolve_for_layout(element_handle))
    {
        mark_ui_layout_stale();
        element->ScrollIntoView(align_with_top);
    }
}

//-------------------------------------------------------------------------
// Controls
//-------------------------------------------------------------------------

/// Control classes scripts wrap elements in; the values mirror UIElement's control kinds.
enum class ui_control_kind : int32_t
{
    element,
    button,
    input,
    slider,
    toggle,
    select,
    progress,
    tabset
};

auto get_input_control_kind(const Rml::Element& input) -> ui_control_kind
{
    const auto type = input.GetAttribute<Rml::String>("type", "text");
    if(type == "range")
    {
        return ui_control_kind::slider;
    }
    if(type == "checkbox" || type == "radio")
    {
        return ui_control_kind::toggle;
    }
    if(type == "submit" || type == "button")
    {
        return ui_control_kind::button;
    }
    return ui_control_kind::input;
}

auto get_control_kind(const Rml::Element& element) -> ui_control_kind
{
    const auto& tag = element.GetTagName();
    if(tag == "input")
    {
        return get_input_control_kind(element);
    }
    if(tag == "textarea")
    {
        return ui_control_kind::input;
    }
    if(tag == "button")
    {
        return ui_control_kind::button;
    }
    if(tag == "select")
    {
        return ui_control_kind::select;
    }
    if(tag == "progress" || tag == "progressbar")
    {
        return ui_control_kind::progress;
    }
    return tag == "tabset" ? ui_control_kind::tabset : ui_control_kind::element;
}

/// Counts the "change" events an element sends while a script applies a value.
class change_counter final : public Rml::EventListener
{
public:
    int count = 0;

    void ProcessEvent(Rml::Event&) override
    {
        ++count;
    }
};

/// Applies a control change. With @p notify exactly one "change" reaches scripts: the one the
/// control sends itself (RmlUi does for some controls, not for others), or one sent here. Without
/// it none does; data bindings still see the control's own.
template<typename Apply>
void apply_control_change(Rml::Element& element, bool notify, const Rml::Variant& change_value, Apply&& apply)
{
    change_counter counter;
    element.AddEventListener(Rml::EventId::Change, &counter);
    auto* previous = g_silenced_change_target;
    if(!notify)
    {
        g_silenced_change_target = &element;
    }
    apply();
    g_silenced_change_target = previous;
    element.RemoveEventListener(Rml::EventId::Change, &counter);
    if(notify && counter.count == 0)
    {
        element.DispatchEvent(Rml::EventId::Change, {{"value", change_value}});
    }
}

auto internal_m2n_ui_element_wrapper_get_control_kind(std::intptr_t element_handle) -> int32_t
{
    auto* element = resolve_ui_element_handle(element_handle);
    return static_cast<int32_t>(element ? get_control_kind(*element) : ui_control_kind::element);
}

auto internal_m2n_ui_element_wrapper_get_value(std::intptr_t element_handle) -> std::string
{
    auto* element = resolve_ui_element_handle(element_handle);
    if(!element)
    {
        return {};
    }
    if(auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(element))
    {
        return control->GetValue();
    }
    return element->GetAttribute<Rml::String>("value", "");
}

void internal_m2n_ui_element_wrapper_set_value(std::intptr_t element_handle, const std::string& value, bool notify)
{
    auto* element = resolve_for_change(element_handle);
    if(!element)
    {
        return;
    }
    // A slider reports its value as a number, like its own change events.
    const auto change_value =
        get_control_kind(*element) == ui_control_kind::slider ? Rml::Variant(std::strtof(value.c_str(), nullptr)) : Rml::Variant(value);
    apply_control_change(*element,
                         notify,
                         change_value,
                         [&]()
                         {
                             if(auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(element))
                             {
                                 control->SetValue(value);
                             }
                             else
                             {
                                 element->SetAttribute("value", value);
                             }
                         });
}

auto internal_m2n_ui_element_wrapper_is_disabled(std::intptr_t element_handle) -> bool
{
    auto* element = resolve_ui_element_handle(element_handle);
    return element && element->HasAttribute("disabled");
}

void internal_m2n_ui_element_wrapper_set_disabled(std::intptr_t element_handle, bool disabled)
{
    auto* element = resolve_for_change(element_handle);
    if(!element)
    {
        return;
    }
    if(auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(element))
    {
        control->SetDisabled(disabled);
    }
    else if(disabled)
    {
        element->SetAttribute("disabled", "");
    }
    else
    {
        element->RemoveAttribute("disabled");
    }
}

auto internal_m2n_ui_element_wrapper_is_checked(std::intptr_t element_handle) -> bool
{
    auto* element = resolve_ui_element_handle(element_handle);
    return element && element->HasAttribute("checked");
}

void internal_m2n_ui_element_wrapper_set_checked(std::intptr_t element_handle, bool checked, bool notify)
{
    auto* element = resolve_for_change(element_handle);
    if(!element || element->HasAttribute("checked") == checked)
    {
        return;
    }
    apply_control_change(*element,
                         notify,
                         Rml::Variant(checked),
                         [&]()
                         {
                             if(checked)
                             {
                                 element->SetAttribute("checked", "");
                             }
                             else
                             {
                                 element->RemoveAttribute("checked");
                             }
                         });
}

void internal_m2n_ui_element_wrapper_select_text(std::intptr_t element_handle, int32_t start, int32_t end)
{
    auto* element = resolve_ui_element_handle(element_handle);
    const bool is_all = start < 0;
    if(auto* input = element ? rmlui_dynamic_cast<Rml::ElementFormControlInput*>(element) : nullptr)
    {
        is_all ? input->Select() : input->SetSelectionRange(start, end);
    }
    else if(auto* area = element ? rmlui_dynamic_cast<Rml::ElementFormControlTextArea*>(element) : nullptr)
    {
        is_all ? area->Select() : area->SetSelectionRange(start, end);
    }
}

auto resolve_select(std::intptr_t element_handle) -> Rml::ElementFormControlSelect*
{
    auto* element = resolve_ui_element_handle(element_handle);
    return element ? rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(element) : nullptr;
}

auto internal_m2n_ui_element_wrapper_get_selected_index(std::intptr_t element_handle) -> int32_t
{
    auto* select = resolve_select(element_handle);
    return select ? select->GetSelection() : -1;
}

void internal_m2n_ui_element_wrapper_set_selected_index(std::intptr_t element_handle, int32_t index, bool notify)
{
    auto* select = resolve_select(element_handle);
    if(!select || select->GetSelection() == index)
    {
        return;
    }
    mark_ui_layout_stale();
    apply_control_change(*select,
                         notify,
                         Rml::Variant(Rml::String()),
                         [&]() { select->SetSelection(index); });
}

auto internal_m2n_ui_element_wrapper_get_options(std::intptr_t element_handle) -> handle_list
{
    handle_list handles;
    if(auto* select = resolve_select(element_handle))
    {
        const int count = select->GetNumOptions();
        for(int i = 0; i < count; ++i)
        {
            handles.push_back(get_ui_element_handle(select->GetOption(i)));
        }
    }
    return handles;
}

auto internal_m2n_ui_element_wrapper_add_option(std::intptr_t element_handle, const std::string& rml, const std::string& value, int32_t before)
    -> int32_t
{
    auto* select = resolve_select(element_handle);
    if(!select)
    {
        return -1;
    }
    mark_ui_layout_stale();
    int index = -1;
    // Editing options is not a user change: a selection it moves does not notify scripts.
    apply_control_change(*select, false, Rml::Variant(), [&]() { index = select->Add(rml, value, before); });
    return index;
}

void internal_m2n_ui_element_wrapper_remove_option(std::intptr_t element_handle, int32_t index)
{
    if(auto* select = resolve_select(element_handle))
    {
        mark_ui_layout_stale();
        apply_control_change(*select,
                             false,
                             Rml::Variant(),
                             [&]() { index < 0 ? select->RemoveAll() : select->Remove(index); });
    }
}

auto resolve_progress(std::intptr_t element_handle) -> Rml::ElementProgress*
{
    auto* element = resolve_ui_element_handle(element_handle);
    return element ? rmlui_dynamic_cast<Rml::ElementProgress*>(element) : nullptr;
}

/// The progress value (x) and its maximum (y).
auto internal_m2n_ui_element_wrapper_get_progress(std::intptr_t element_handle) -> math::vec2
{
    auto* progress = resolve_progress(element_handle);
    return progress ? math::vec2(progress->GetValue(), progress->GetMax()) : math::vec2(0.0f);
}

void internal_m2n_ui_element_wrapper_set_progress(std::intptr_t element_handle, float value, float max_value)
{
    if(auto* progress = resolve_progress(element_handle))
    {
        progress->SetMax(max_value);
        progress->SetValue(value);
    }
}

auto resolve_tabset(std::intptr_t element_handle) -> Rml::ElementTabSet*
{
    auto* element = resolve_ui_element_handle(element_handle);
    return element ? rmlui_dynamic_cast<Rml::ElementTabSet*>(element) : nullptr;
}

/// The active tab (x) and the number of tabs (y).
auto internal_m2n_ui_element_wrapper_get_tabs(std::intptr_t element_handle) -> math::vec2
{
    auto* tabset = resolve_tabset(element_handle);
    return tabset ? math::vec2(float(tabset->GetActiveTab()), float(tabset->GetNumTabs())) : math::vec2(-1.0f, 0.0f);
}

void internal_m2n_ui_element_wrapper_set_active_tab(std::intptr_t element_handle, int32_t index)
{
    if(auto* tabset = resolve_tabset(element_handle))
    {
        mark_ui_layout_stale();
        tabset->SetActiveTab(index);
    }
}

void internal_m2n_ui_element_wrapper_set_tab(std::intptr_t element_handle, int32_t index, const std::string& tab_rml, const std::string& panel_rml)
{
    if(auto* tabset = resolve_tabset(element_handle))
    {
        mark_ui_layout_stale();
        tabset->SetTab(index, tab_rml);
        tabset->SetPanel(index, panel_rml);
    }
}

void internal_m2n_ui_element_wrapper_remove_tab(std::intptr_t element_handle, int32_t index)
{
    if(auto* tabset = resolve_tabset(element_handle))
    {
        mark_ui_layout_stale();
        tabset->RemoveTab(index);
    }
}

//-------------------------------------------------------------------------
// DOM
//-------------------------------------------------------------------------

auto internal_m2n_ui_element_wrapper_get_parent(std::intptr_t element_handle) -> std::intptr_t
{
    auto* element = resolve_ui_element_handle(element_handle);
    // A document's parent is its context's hidden root, not part of any document.
    if(!element || is_document(*element))
    {
        return 0;
    }
    return get_ui_element_handle(element->GetParentNode());
}

auto internal_m2n_ui_element_wrapper_get_children(std::intptr_t element_handle) -> handle_list
{
    handle_list handles;
    if(auto* element = resolve_ui_element_handle(element_handle))
    {
        // DOM children only: scrollbars and other widget parts are left out.
        const int count = element->GetNumChildren(false);
        handles.reserve(static_cast<size_t>(count));
        for(int i = 0; i < count; ++i)
        {
            handles.push_back(get_ui_element_handle(element->GetChild(i)));
        }
    }
    return handles;
}

auto internal_m2n_ui_element_wrapper_query_selector(std::intptr_t element_handle, const std::string& selector) -> std::intptr_t
{
    auto* element = resolve_ui_element_handle(element_handle);
    return element ? get_ui_element_handle(element->QuerySelector(selector)) : 0;
}

auto internal_m2n_ui_element_wrapper_query_selector_all(std::intptr_t element_handle, const std::string& selector) -> handle_list
{
    Rml::ElementList elements;
    if(auto* element = resolve_ui_element_handle(element_handle))
    {
        element->QuerySelectorAll(elements, selector);
    }
    return to_handles(elements);
}

auto internal_m2n_ui_element_wrapper_closest(std::intptr_t element_handle, const std::string& selector) -> std::intptr_t
{
    auto* element = resolve_ui_element_handle(element_handle);
    return element ? get_ui_element_handle(element->Closest(selector)) : 0;
}

/// Takes @p element out of its tree, or out of the kept orphans.
/// @return Its owning pointer; null for a document, which cannot move.
auto detach_ui_element(Rml::Element& element) -> Rml::ElementPtr
{
    if(is_document(element))
    {
        return nullptr;
    }
    if(auto orphan = take_orphan_ui_element(&element))
    {
        return orphan;
    }
    auto* parent = element.GetParentNode();
    return parent ? parent->RemoveChild(&element) : nullptr;
}

/// Whether @p child may move under @p parent: not a document, not the parent or one of its ancestors.
auto can_insert_ui_element(Rml::Element& parent, Rml::Element& child) -> bool
{
    if(is_document(child) || &child == &parent || child.Contains(&parent))
    {
        APPLOG_WARNING("UI: cannot insert <{} id='{}'> into <{} id='{}'>: a document, or the parent itself or one of "
                       "its ancestors",
                       child.GetTagName(),
                       child.GetId(),
                       parent.GetTagName(),
                       parent.GetId());
        return false;
    }
    return true;
}

auto internal_m2n_ui_element_wrapper_append_child(std::intptr_t parent_handle, std::intptr_t child_handle) -> bool
{
    auto* parent = resolve_for_change(parent_handle);
    auto* child = resolve_ui_element_handle(child_handle);
    if(!parent || !child || !can_insert_ui_element(*parent, *child))
    {
        return false;
    }
    auto owned = detach_ui_element(*child);
    return owned && parent->AppendChild(std::move(owned)) != nullptr;
}

auto internal_m2n_ui_element_wrapper_insert_before(std::intptr_t parent_handle, std::intptr_t child_handle, std::intptr_t before_handle) -> bool
{
    auto* parent = resolve_for_change(parent_handle);
    auto* child = resolve_ui_element_handle(child_handle);
    auto* before = resolve_ui_element_handle(before_handle);
    if(!parent || !child || !can_insert_ui_element(*parent, *child))
    {
        return false;
    }
    if(before == child)
    {
        return true;
    }
    // Not a child of the parent: append, as RmlUi does.
    if(before && before->GetParentNode() != parent)
    {
        before = nullptr;
    }
    auto owned = detach_ui_element(*child);
    if(!owned)
    {
        return false;
    }
    return (before ? parent->InsertBefore(std::move(owned), before) : parent->AppendChild(std::move(owned))) != nullptr;
}

void internal_m2n_ui_element_wrapper_remove(std::intptr_t element_handle)
{
    if(auto* element = resolve_for_change(element_handle))
    {
        // Destroyed when the owning pointer goes out of scope.
        auto owned = detach_ui_element(*element);
    }
}

auto internal_m2n_ui_element_wrapper_clone(std::intptr_t element_handle) -> std::intptr_t
{
    auto* element = resolve_ui_element_handle(element_handle);
    if(!element)
    {
        return 0;
    }
    return get_ui_element_handle(adopt_orphan_ui_element(element->GetOwnerDocument(), element->Clone()));
}

//-------------------------------------------------------------------------
// Style
//-------------------------------------------------------------------------

auto internal_m2n_ui_element_wrapper_get_inline_style(std::intptr_t element_handle, const std::string& name) -> std::string
{
    auto* element = resolve_ui_element_handle(element_handle);
    const auto id = Rml::StyleSheetSpecification::GetPropertyId(name);
    if(!element || id == Rml::PropertyId::Invalid)
    {
        return {};
    }
    const auto& inline_properties = element->GetLocalStyleProperties();
    auto it = inline_properties.find(id);
    return it != inline_properties.end() ? it->second.ToString() : std::string();
}

auto internal_m2n_ui_element_wrapper_set_inline_style(std::intptr_t element_handle, const std::string& name, const std::string& value) -> bool
{
    auto* element = resolve_for_change(element_handle);
    if(!element)
    {
        return false;
    }
    if(!element->SetProperty(name, value))
    {
        APPLOG_WARNING("UI: '{}: {}' on <{} id='{}'> is not a valid RCSS declaration",
                       name,
                       value,
                       element->GetTagName(),
                       element->GetId());
        return false;
    }
    return true;
}

void internal_m2n_ui_element_wrapper_remove_inline_style(std::intptr_t element_handle, const std::string& name)
{
    if(auto* element = resolve_for_change(element_handle))
    {
        element->RemoveProperty(name);
    }
}

auto internal_m2n_ui_element_wrapper_get_computed_style(std::intptr_t element_handle, const std::string& name) -> std::string
{
    auto* element = resolve_ui_element_handle(element_handle);
    const auto* property = element ? element->GetProperty(name) : nullptr;
    return property ? property->ToString() : std::string();
}

/// Units the numeric style shortcuts write; the values mirror the managed UIStyle.
enum class ui_style_unit : int32_t
{
    number,
    dp
};

void internal_m2n_ui_element_wrapper_set_style_number(std::intptr_t element_handle, const std::string& name, float value, int32_t unit)
{
    auto* element = resolve_for_change(element_handle);
    const auto id = Rml::StyleSheetSpecification::GetPropertyId(name);
    if(!element || id == Rml::PropertyId::Invalid)
    {
        return;
    }
    const auto rml_unit = unit == static_cast<int32_t>(ui_style_unit::dp) ? Rml::Unit::DP : Rml::Unit::NUMBER;
    element->SetProperty(id, Rml::Property(value, rml_unit));
}

auto internal_m2n_ui_element_wrapper_get_style_number(std::intptr_t element_handle, const std::string& name) -> float
{
    auto* element = resolve_ui_element_handle(element_handle);
    const auto* property = element ? element->GetProperty(name) : nullptr;
    const bool is_plain_number =
        property && Rml::Any(property->unit & (Rml::Unit::NUMBER | Rml::Unit::DP | Rml::Unit::PX));
    return is_plain_number ? property->Get<float>() : std::numeric_limits<float>::quiet_NaN();
}

//-------------------------------------------------------------------------
// Geometry and scrolling
//-------------------------------------------------------------------------

/// Box areas a rect can cover; the values mirror the managed UIElement calls.
enum class ui_box_area : int32_t
{
    border,
    padding,
    content
};

auto to_rml_box_area(int32_t area) -> Rml::BoxArea
{
    switch(static_cast<ui_box_area>(area))
    {
        case ui_box_area::padding:
            return Rml::BoxArea::Padding;
        case ui_box_area::content:
            return Rml::BoxArea::Content;
        default:
            return Rml::BoxArea::Border;
    }
}

/// The element's box in its document's pixels: x, y of the top left, width, height.
auto internal_m2n_ui_element_wrapper_get_rect(std::intptr_t element_handle, int32_t area) -> math::vec4
{
    auto* element = resolve_for_layout(element_handle);
    if(!element)
    {
        return math::vec4(0.0f);
    }
    return get_ui_element_rect(*element, to_rml_box_area(area));
}

/// Scroll position (x, y) and scrollable size (z, w) of the element's content.
auto internal_m2n_ui_element_wrapper_get_scroll(std::intptr_t element_handle) -> math::vec4
{
    auto* element = resolve_for_layout(element_handle);
    if(!element)
    {
        return math::vec4(0.0f);
    }
    return {element->GetScrollLeft(), element->GetScrollTop(), element->GetScrollWidth(), element->GetScrollHeight()};
}

void internal_m2n_ui_element_wrapper_scroll_to(std::intptr_t element_handle, const math::vec2& offset, bool smooth)
{
    if(auto* element = resolve_for_layout(element_handle))
    {
        mark_ui_layout_stale();
        element->ScrollTo({offset.x, offset.y}, smooth ? Rml::ScrollBehavior::Smooth : Rml::ScrollBehavior::Instant);
    }
}

//-------------------------------------------------------------------------
// Animation
//-------------------------------------------------------------------------

/// Easing codes from the managed UIEasing: tween type * 4 + direction.
auto decode_easing(int32_t easing) -> Rml::Tween
{
    const int32_t type = easing >> 2;
    const int32_t direction = easing & 3;
    const bool is_valid = type > Rml::Tween::None && type < Rml::Tween::Callback && direction >= Rml::Tween::In &&
                          direction <= Rml::Tween::InOut;
    if(!is_valid)
    {
        return Rml::Tween{};
    }
    return Rml::Tween(static_cast<Rml::Tween::Type>(type), static_cast<Rml::Tween::Direction>(direction));
}

/// Parses @p value as the value of property @p id.
auto parse_property_value(Rml::PropertyId id, const std::string& value, Rml::Property& property) -> bool
{
    const auto* definition = Rml::StyleSheetSpecification::GetProperty(id);
    return definition && definition->ParseValue(property, value);
}

struct ui_animation_request
{
    std::string property;
    std::string target;
    float duration{};
    int32_t easing{};
    int32_t loops{};
    bool alternate{};
    float delay{};
};

auto start_ui_animation(Rml::Element& element, const ui_animation_request& request) -> bool
{
    const auto id = Rml::StyleSheetSpecification::GetPropertyId(request.property);
    Rml::Property target;
    if(id == Rml::PropertyId::Invalid || !parse_property_value(id, request.target, target))
    {
        APPLOG_WARNING("UI: cannot animate '{}' to '{}' on <{} id='{}'>: not a property and value RCSS accepts",
                       request.property,
                       request.target,
                       element.GetTagName(),
                       element.GetId());
        return false;
    }
    return element.Animate(id,
                           target,
                           request.duration,
                           decode_easing(request.easing),
                           request.loops,
                           request.alternate,
                           request.delay);
}

auto internal_m2n_ui_element_wrapper_animate(std::intptr_t element_handle,
                                             const std::string& property,
                                             const std::string& target,
                                             float duration,
                                             int32_t easing,
                                             int32_t loops,
                                             bool alternate,
                                             float delay) -> bool
{
    auto* element = resolve_for_change(element_handle);
    return element && start_ui_animation(*element, {property, target, duration, easing, loops, alternate, delay});
}

void internal_m2n_ui_element_wrapper_stop_animation(std::intptr_t element_handle, const std::string& property)
{
    auto* element = resolve_ui_element_handle(element_handle);
    const auto id = Rml::StyleSheetSpecification::GetPropertyId(property);
    const auto* current = element && id != Rml::PropertyId::Invalid ? element->GetProperty(id) : nullptr;
    if(current)
    {
        // RmlUi has no stop: a zero-length animation to the current value replaces the running one.
        const Rml::Property value = *current;
        element->Animate(id, value, 0.0f);
    }
}

void internal_m2n_ui_element_wrapper_restart_animation(std::intptr_t element_handle)
{
    auto* element = resolve_for_change(element_handle);
    const auto* animation = element ? element->GetProperty(Rml::PropertyId::Animation) : nullptr;
    if(!animation)
    {
        return;
    }
    // Writing the 'animation' property restarts its keyframe animations at the next update. An inline
    // value is written back as is; a stylesheet value is written and removed, leaving no inline value.
    const Rml::Property value = *animation;
    const auto& inline_properties = element->GetLocalStyleProperties();
    const bool is_inline = inline_properties.find(Rml::PropertyId::Animation) != inline_properties.end();
    element->SetProperty(Rml::PropertyId::Animation, value);
    if(!is_inline)
    {
        element->RemoveProperty(Rml::PropertyId::Animation);
    }
}


} // namespace

auto make_ui_event_payload(const Rml::Event& event, bool is_capture_listener, std::string& text) -> ui_event_payload
{
    ui_event_payload payload;
    payload.native_event = reinterpret_cast<std::intptr_t>(&event);
    payload.target_handle = get_ui_element_handle(event.GetTargetElement());
    payload.current_handle = get_ui_element_handle(event.GetCurrentElement());
    payload.phase = static_cast<int32_t>(event.GetPhase());
    payload.is_capture_listener = is_capture_listener ? 1 : 0;
    payload.modifiers = get_event_modifiers(event);
    const auto kind = get_ui_event_kind(event);
    payload.kind = static_cast<int32_t>(kind);
    text = fill_kind_payload(payload, kind, event);
    return payload;
}

void register_ui_document_component_script_bindings()
{

    APPLOG_TRACE("{}", __func__);
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.UIDocumentComponent");
        reg.add_internal_call("internal_m2n_ui_document_get_asset", dotnet_internal_call(internal_m2n_ui_document_get_asset));
        reg.add_internal_call("internal_m2n_ui_document_set_asset", dotnet_internal_call(internal_m2n_ui_document_set_asset));
        reg.add_internal_call("internal_m2n_ui_document_is_loaded", dotnet_internal_call(internal_m2n_ui_document_is_loaded));
        reg.add_internal_call("internal_m2n_ui_document_is_enabled", dotnet_internal_call(internal_m2n_ui_document_is_enabled));
        reg.add_internal_call("internal_m2n_ui_document_set_enabled", dotnet_internal_call(internal_m2n_ui_document_set_enabled));
        reg.add_internal_call("internal_m2n_ui_document_close", dotnet_internal_call(internal_m2n_ui_document_close));
        reg.add_internal_call("internal_m2n_ui_document_reload", dotnet_internal_call(internal_m2n_ui_document_reload));
        reg.add_internal_call("internal_m2n_ui_document_get_title", dotnet_internal_call(internal_m2n_ui_document_get_title));
        reg.add_internal_call("internal_m2n_ui_document_set_title", dotnet_internal_call(internal_m2n_ui_document_set_title));
        reg.add_internal_call("internal_m2n_ui_document_get_wrapper", dotnet_internal_call(internal_m2n_ui_document_get_wrapper));
    }
}

void register_ui_document_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.UIDocument");
        reg.add_internal_call("internal_m2n_ui_document_wrapper_is_valid", dotnet_internal_call(internal_m2n_ui_document_wrapper_is_valid));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_get_title", dotnet_internal_call(internal_m2n_ui_document_wrapper_get_title));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_set_title", dotnet_internal_call(internal_m2n_ui_document_wrapper_set_title));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_is_visible", dotnet_internal_call(internal_m2n_ui_document_wrapper_is_visible));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_show", dotnet_internal_call(internal_m2n_ui_document_wrapper_show));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_hide", dotnet_internal_call(internal_m2n_ui_document_wrapper_hide));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_close", dotnet_internal_call(internal_m2n_ui_document_wrapper_close));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_get_element_by_id", dotnet_internal_call(internal_m2n_ui_document_get_element_wrapper_by_id));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_query_selector", dotnet_internal_call(internal_m2n_ui_document_query_selector_wrapper));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_query_selector_all", dotnet_internal_call(internal_m2n_ui_document_wrapper_query_selector_all));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_create_element", dotnet_internal_call(internal_m2n_ui_document_wrapper_create_element));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_element_at", dotnet_internal_call(internal_m2n_ui_document_wrapper_element_at));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_update_layout", dotnet_internal_call(internal_m2n_ui_document_wrapper_update_layout));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_get_dp_ratio", dotnet_internal_call(internal_m2n_ui_document_wrapper_get_dp_ratio));
    }
}

void register_ui_element_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.UIElement");
        reg.add_internal_call("internal_m2n_ui_element_wrapper_is_valid", dotnet_internal_call(internal_m2n_ui_element_wrapper_is_valid));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_id", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_id));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_tag_name", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_tag_name));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_inner_rml", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_inner_rml));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_inner_rml", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_inner_rml));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_text", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_text));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_text", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_text));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_is_visible", dotnet_internal_call(internal_m2n_ui_element_wrapper_is_visible));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_visible", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_visible));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_attribute", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_attribute));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_attribute", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_attribute));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_remove_attribute", dotnet_internal_call(internal_m2n_ui_element_wrapper_remove_attribute));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_has_attribute", dotnet_internal_call(internal_m2n_ui_element_wrapper_has_attribute));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_class", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_class));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_is_class_set", dotnet_internal_call(internal_m2n_ui_element_wrapper_is_class_set));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_pseudo_class", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_pseudo_class));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_is_pseudo_class_set", dotnet_internal_call(internal_m2n_ui_element_wrapper_is_pseudo_class_set));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_sync_transform_to_entity", dotnet_internal_call(internal_m2n_ui_element_wrapper_sync_transform_to_entity));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_focus", dotnet_internal_call(internal_m2n_ui_element_wrapper_focus));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_blur", dotnet_internal_call(internal_m2n_ui_element_wrapper_blur));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_click", dotnet_internal_call(internal_m2n_ui_element_wrapper_click));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_scroll_into_view", dotnet_internal_call(internal_m2n_ui_element_wrapper_scroll_into_view));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_parent", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_parent));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_children", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_children));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_query_selector", dotnet_internal_call(internal_m2n_ui_element_wrapper_query_selector));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_query_selector_all", dotnet_internal_call(internal_m2n_ui_element_wrapper_query_selector_all));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_closest", dotnet_internal_call(internal_m2n_ui_element_wrapper_closest));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_append_child", dotnet_internal_call(internal_m2n_ui_element_wrapper_append_child));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_insert_before", dotnet_internal_call(internal_m2n_ui_element_wrapper_insert_before));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_remove", dotnet_internal_call(internal_m2n_ui_element_wrapper_remove));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_clone", dotnet_internal_call(internal_m2n_ui_element_wrapper_clone));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_inline_style", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_inline_style));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_inline_style", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_inline_style));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_remove_inline_style", dotnet_internal_call(internal_m2n_ui_element_wrapper_remove_inline_style));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_computed_style", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_computed_style));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_style_number", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_style_number));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_style_number", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_style_number));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_rect", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_rect));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_scroll", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_scroll));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_scroll_to", dotnet_internal_call(internal_m2n_ui_element_wrapper_scroll_to));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_animate", dotnet_internal_call(internal_m2n_ui_element_wrapper_animate));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_stop_animation", dotnet_internal_call(internal_m2n_ui_element_wrapper_stop_animation));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_restart_animation", dotnet_internal_call(internal_m2n_ui_element_wrapper_restart_animation));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_control_kind", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_control_kind));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_value", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_value));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_value", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_value));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_is_disabled", dotnet_internal_call(internal_m2n_ui_element_wrapper_is_disabled));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_disabled", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_disabled));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_is_checked", dotnet_internal_call(internal_m2n_ui_element_wrapper_is_checked));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_checked", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_checked));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_select_text", dotnet_internal_call(internal_m2n_ui_element_wrapper_select_text));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_selected_index", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_selected_index));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_selected_index", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_selected_index));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_options", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_options));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_add_option", dotnet_internal_call(internal_m2n_ui_element_wrapper_add_option));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_remove_option", dotnet_internal_call(internal_m2n_ui_element_wrapper_remove_option));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_progress", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_progress));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_progress", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_progress));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_tabs", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_tabs));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_active_tab", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_active_tab));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_tab", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_tab));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_remove_tab", dotnet_internal_call(internal_m2n_ui_element_wrapper_remove_tab));
    }
}

void register_ui_event_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.UIEventManager");
        reg.add_internal_call("internal_m2n_ui_ensure_native_event_listener",
                              dotnet_internal_call(internal_m2n_ui_ensure_native_event_listener));
        reg.add_internal_call("internal_m2n_ui_remove_native_event_listener",
                              dotnet_internal_call(internal_m2n_ui_remove_native_event_listener));
        reg.add_internal_call("internal_m2n_ui_take_destroyed_handle",
                              dotnet_internal_call(internal_m2n_ui_take_destroyed_handle));
    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.UIEventBase");
        reg.add_internal_call("internal_m2n_ui_stop_propagation", dotnet_internal_call(internal_m2n_ui_stop_propagation));
        reg.add_internal_call("internal_m2n_ui_stop_immediate_propagation",
                              dotnet_internal_call(internal_m2n_ui_stop_immediate_propagation));
    }
}

} // namespace unravel
