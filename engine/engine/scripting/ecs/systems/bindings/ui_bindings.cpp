// UIDocumentComponent, UIDocument, UIElement, and UI event registries (shared Rml helpers).
#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"
#include "../script_system.h"

#include <engine/assets/asset_manager.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ui/ecs/components/ui_document_component.h>
#include <engine/ui/rmlui/RmlUi_SystemInterface.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <sstream>

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
        if(comp->document)
        {
            comp->document->Close();
            comp->document = nullptr;
        }
    }
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

  ______ _      ______ __  __ ______ _   _ _______
 |  ____| |    |  ____|  \/  |  ____| \ | |__   __|
 | |__  | |    | |__  | \  / | |__  |  \| |  | |
 |  __| | |    |  __| | |\/| |  __| | . ` |  | |
 | |____| |____| |____| |  | | |____| |\  |  | |
 |______|______|______|_|  |_|______|_| \_|  |_|


*/
//-------------------------------------------------------------------------

// Helper function to get UI element safely
auto get_ui_element_safe(entt::entity entity_id, const std::string& element_id) -> Rml::Element*
{
    if(auto comp = safe_get_component<ui_document_component>(entity_id))
    {
        if(comp->document)
        {
            return comp->document->GetElementById(element_id);
        }
    }
    return nullptr;
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

template<typename T>
void dispatch_ui_event_to_manager(const T& event_data)
{
    try
    {
        const auto& ctx = engine::context();
        const auto& script_cache = ctx.get_cached<script_system>().get_cache();

        if(!script_cache.ui_dispatch_event_method.valid())
        {
            APPLOG_ERROR("UIEventManager.InternalDispatchEvent method not found");
            return;
        }

        auto method_invoker =
            dotnet::make_method_invoker<void(const T&)>(script_cache.ui_dispatch_event_method, false);
        method_invoker(event_data);
    }
    catch (const dotnet::exception& e)
    {
        APPLOG_ERROR("C# exception dispatching UI event: {}", e.what());
    }
    catch (const std::exception& e)
    {
        APPLOG_ERROR("Error dispatching UI event: {}", e.what());
    }
}



// UI Event Type Classification
enum class ui_event_type
{
    unknown,
    key,
    textinput,
    pointer,
    change,
    value
};

// Determine UI event type for efficient dispatch
auto get_ui_event_type(const Rml::Event& event) -> ui_event_type
{
    const auto event_id = event.GetId();
    
    // Check key events first (most common check)
    if (event_id == Rml::EventId::Keydown || event_id == Rml::EventId::Keyup)
    {
        return ui_event_type::key;
    }
    
    // Check text input events
    if (event_id == Rml::EventId::Textinput)
    {
        return ui_event_type::textinput;
    }
    
    // Check pointer events (including full drag family)
    if (event_id == Rml::EventId::Click || event_id == Rml::EventId::Mousedown || event_id == Rml::EventId::Mouseup ||
        event_id == Rml::EventId::Mousemove || event_id == Rml::EventId::Mouseover || event_id == Rml::EventId::Mouseout ||
        event_id == Rml::EventId::Mousescroll || event_id == Rml::EventId::Dblclick || event_id == Rml::EventId::Drag ||
        event_id == Rml::EventId::Dragstart || event_id == Rml::EventId::Dragover || event_id == Rml::EventId::Dragdrop ||
        event_id == Rml::EventId::Dragmove || event_id == Rml::EventId::Dragout || event_id == Rml::EventId::Dragend ||
        event_id == Rml::EventId::Handledrag)
    {
        return ui_event_type::pointer;
    }
    // Change always classifies, including empty values (e.g. cleared text fields).
    if (event_id == Rml::EventId::Change)
    {
        if (auto* element = event.GetCurrentElement())
        {
            if (element->HasAttribute("min") || element->HasAttribute("max"))
            {
                return ui_event_type::value;
            }
        }
        return ui_event_type::change;
    }
    return ui_event_type::unknown;
}


// Fill base event data common to all event types
void fill_base_event_data(dotnetpp_backend::managed_interface::ui_event_base& event_data, 
                         const Rml::Event& event, 
                         Rml::Element* target_element, 
                         Rml::Element* current_element)
{
    event_data.native_ptr = reinterpret_cast<std::intptr_t>(&event);
    event_data.target_element_id = target_element->GetId();
    event_data.target_element_ptr = reinterpret_cast<std::intptr_t>(target_element);
    event_data.current_element_id = current_element->GetId();
    event_data.current_element_ptr = reinterpret_cast<std::intptr_t>(current_element);
    event_data.event_type = event.GetType();
    event_data.phase = static_cast<int>(event.GetPhase());
}

// Dispatch key event to C# UIEventManager
void dispatch_key_event_to_manager(const Rml::Event& event, 
                                  Rml::Element* target_element, 
                                  Rml::Element* current_element)
{
     
    // Create key event data
    dotnetpp_backend::managed_interface::ui_key_event key_event_data;
    fill_base_event_data(key_event_data, event, target_element, current_element);
    
    // Fill key-specific data based on actual RmlUi parameters
    auto key_identifier = event.GetParameter<int>("key_identifier", 0);
    key_event_data.key_code = RmlEngine::convert_rml_key_to_input(static_cast<Rml::Input::KeyIdentifier>(key_identifier));
    key_event_data.ctrl_key = event.GetParameter<int>("ctrl_key", 0) > 0;
    key_event_data.shift_key = event.GetParameter<int>("shift_key", 0) > 0;
    key_event_data.alt_key = event.GetParameter<int>("alt_key", 0) > 0;
    key_event_data.meta_key = event.GetParameter<int>("meta_key", 0) > 0;
    
    dispatch_ui_event_to_manager(key_event_data);
}

// Dispatch pointer event to C# UIEventManager
void dispatch_pointer_event_to_manager(const Rml::Event& event, 
                                      Rml::Element* target_element, 
                                      Rml::Element* current_element)
{

    // Create pointer event data
    dotnetpp_backend::managed_interface::ui_pointer_event pointer_event_data;
    fill_base_event_data(pointer_event_data, event, target_element, current_element);
    
    // Fill pointer-specific data based on actual RmlUi parameters
    pointer_event_data.x = event.GetParameter<float>("mouse_x", 0.0f);
    pointer_event_data.y = event.GetParameter<float>("mouse_y", 0.0f);
    pointer_event_data.button = event.GetParameter<int>("button", -1);
    pointer_event_data.ctrl_key = event.GetParameter<int>("ctrl_key", 0) > 0;
    pointer_event_data.shift_key = event.GetParameter<int>("shift_key", 0) > 0;
    pointer_event_data.alt_key = event.GetParameter<int>("alt_key", 0) > 0;
    pointer_event_data.meta_key = event.GetParameter<int>("meta_key", 0) > 0;
    pointer_event_data.delta_x = event.GetParameter<float>("wheel_delta_x", 0.0f);
    pointer_event_data.delta_y = event.GetParameter<float>("wheel_delta_y", 0.0f);
    
    dispatch_ui_event_to_manager(pointer_event_data);

}

// Dispatch text input event to C# UIEventManager
void dispatch_textinput_event_to_manager(const Rml::Event& event, 
                                         Rml::Element* target_element, 
                                         Rml::Element* current_element)
{
    // Create text input event data
    dotnetpp_backend::managed_interface::ui_textinput_event textinput_event_data;
    fill_base_event_data(textinput_event_data, event, target_element, current_element);

          
    // Fill text input-specific data
    textinput_event_data.text = event.GetParameter<std::string>("text", "");
    textinput_event_data.ctrl_key = event.GetParameter<int>("ctrl_key", 0) > 0;
    textinput_event_data.shift_key = event.GetParameter<int>("shift_key", 0) > 0;
    textinput_event_data.alt_key = event.GetParameter<int>("alt_key", 0) > 0;
    textinput_event_data.meta_key = event.GetParameter<int>("meta_key", 0) > 0;
    
    
    dispatch_ui_event_to_manager(textinput_event_data);
}

// Dispatch value event to C# UIEventManager
void dispatch_value_event_to_manager(const Rml::Event& event, 
                                     Rml::Element* target_element, 
                                     Rml::Element* current_element)
{
   
    // Create value event data
    dotnetpp_backend::managed_interface::ui_slider_event value_event_data;
    fill_base_event_data(value_event_data, event, target_element, current_element);
    
    // Fill value-specific data
    value_event_data.value = event.GetParameter<float>("value", 0);

    if(auto* slider_element = event.GetCurrentElement())
    {
        value_event_data.min_value = slider_element->GetAttribute<float>("min", 0);
        value_event_data.max_value = slider_element->GetAttribute<float>("max", 0);
        value_event_data.step = slider_element->GetAttribute<float>("step", 0);
    }

    dispatch_ui_event_to_manager(value_event_data);
}

// Dispatch change event to C# UIEventManager
void dispatch_change_event_to_manager(const Rml::Event& event, 
                                      Rml::Element* target_element, 
                                      Rml::Element* current_element)
{
   
    // Create change event data
    dotnetpp_backend::managed_interface::ui_change_event change_event_data;
    fill_base_event_data(change_event_data, event, target_element, current_element);
    
    // Fill change-specific data
    change_event_data.value = event.GetParameter<std::string>("value", "");

    dispatch_ui_event_to_manager(change_event_data);

}

// Dispatch base event to C# UIEventManager (fallback)
void dispatch_base_event_to_manager(const Rml::Event& event, 
                                   Rml::Element* target_element, 
                                   Rml::Element* current_element)
{
    dotnetpp_backend::managed_interface::ui_event_base event_data;
    fill_base_event_data(event_data, event, target_element, current_element);
    dispatch_ui_event_to_manager(event_data);
}

// Global event listener that dispatches all UI events to C# UIEventManager
class ui_global_event_listener : public Rml::EventListener
{
    Rml::Event* current_event_ = nullptr;
public:
    void ProcessEvent(Rml::Event& event) override
    {
        current_event_ = &event;
        try
        {
            // Get event information
            auto* target_element = event.GetTargetElement();
            if (!target_element)
            {
                return;
            }

            auto* current_element = event.GetCurrentElement();
            if (!current_element)
            {
                return;
            }

            // Determine event type and dispatch accordingly using efficient switch
            const auto event_type = get_ui_event_type(event);
            
            switch (event_type)
            {
                case ui_event_type::key:
                    dispatch_key_event_to_manager(event, target_element, current_element);
                    break;
                    
                case ui_event_type::textinput:
                    dispatch_textinput_event_to_manager(event, target_element, current_element);
                    break;
                    
                case ui_event_type::pointer:
                    dispatch_pointer_event_to_manager(event, target_element, current_element);
                    break;
                    
                case ui_event_type::change:
                    dispatch_change_event_to_manager(event, target_element, current_element);
                    break;
                    
                case ui_event_type::value:
                    dispatch_value_event_to_manager(event, target_element, current_element);
                    break;
                    
                case ui_event_type::unknown:
                default:
                    // Fallback to base event for unknown types
                    dispatch_base_event_to_manager(event, target_element, current_element);
                    break;
            }
        }
        catch (const std::exception& e)
        {
            APPLOG_ERROR("Error processing UI event: {}", e.what());
        }
        current_event_ = nullptr;
    }

    // Allow access to current event for propagation control
    auto get_current_event() const -> Rml::Event*
    {
        return current_event_;
    }
};
    
// Global event listener instance
ui_global_event_listener g_ui_global_listener;

// Ensure a native event listener is attached to the element for the given event type
void internal_m2n_ui_ensure_native_event_listener(std::intptr_t element_ptr, const std::string& event_type)
{
    if (element_ptr == 0)
    {
        return;
    }
    try
    {
        auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
        element->AddEventListener(event_type, &g_ui_global_listener);
        APPLOG_TRACE("Ensured native UI event listener: element='{}', event='{}'", element->GetId(), event_type);
    }
    catch (const dotnet::exception& e)
    {
        APPLOG_ERROR("C# exception ensuring native UI event listener: {}", e.what());
    }
    catch (const std::exception& e)
    {
        APPLOG_ERROR("Error ensuring native UI event listener: {}", e.what());
    }
}

void internal_m2n_ui_remove_native_event_listener(std::intptr_t element_ptr, const std::string& event_type)
{
    if (element_ptr == 0)
    {
        return;
    }
    try
    {
        auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
        element->RemoveEventListener(event_type, &g_ui_global_listener);
        APPLOG_TRACE("Removed native UI event listener: element='{}', event='{}'", element->GetId(), event_type);
    }
    catch (const dotnet::exception& e)
    {
        APPLOG_ERROR("C# exception removing native UI event listener: {}", e.what());
    }
    catch (const std::exception& e)
    {
        APPLOG_ERROR("Error removing native UI event listener: {}", e.what());
    }
}

// Stop event propagation - called from C# UIEventBase.StopPropagation()
void internal_m2n_ui_stop_propagation(std::intptr_t native_ptr)
{
    try
    {

        auto* current_event = g_ui_global_listener.get_current_event();
        if (current_event && current_event == reinterpret_cast<Rml::Event*>(native_ptr))
        {
            current_event->StopPropagation();
        }
        else
        {
            APPLOG_WARNING("No current UI event to stop propagation on");
        }

    }
    catch (const std::exception& e)
    {
        APPLOG_ERROR("Error stopping UI event propagation: {}", e.what());
    }
}

// Stop immediate event propagation - called from C# UIEventBase.StopImmediatePropagation()
void internal_m2n_ui_stop_immediate_propagation(std::intptr_t native_ptr)
{
    try
    {
        auto* current_event = g_ui_global_listener.get_current_event();
        if (current_event && current_event == reinterpret_cast<Rml::Event*>(native_ptr))
        {
            current_event->StopImmediatePropagation();
        }
        else
        {
            APPLOG_WARNING("No current UI event to stop immediate propagation on");
        }
    }
    catch (const std::exception& e)
    {
        APPLOG_ERROR("Error stopping UI event immediate propagation: {}", e.what());
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

// Helper function to validate element pointer by checking if it exists in the owner entity's UI document
auto validate_ui_element_wrapper(std::intptr_t element_ptr, entt::entity owner_entity) -> bool
{
    if (element_ptr == 0)
    {
        return false;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    
    // Check if this element exists in the owner entity's UI document
    if (auto comp = safe_get_component<ui_document_component>(owner_entity))
    {
        if (comp->document)
        {
            // Check if this element belongs to this document
            return comp->document->Contains(element);
        }
    }
    
    return false;
}

// Helper function to validate document pointer by checking if it matches the owner entity's UI component
auto validate_ui_document_wrapper(std::intptr_t document_ptr, entt::entity owner_entity) -> bool
{
    if (document_ptr == 0)
    {
        return false;
    }
    
    auto* document = reinterpret_cast<Rml::ElementDocument*>(document_ptr);
    
    // Check if this document exists in the owner entity's UI component
    if (auto comp = safe_get_component<ui_document_component>(owner_entity))
    {
        if (comp->document && comp->document == document)
        {
            return true;
        }
    }
    
    return false;
}

//-------------------------------------------------------------------------
// UI Document Wrapper Functions
//-------------------------------------------------------------------------

auto internal_m2n_ui_document_get_wrapper(entt::entity entity_id) -> std::intptr_t
{
    if (auto comp = safe_get_component<ui_document_component>(entity_id))
    {
        if (comp->document)
        {
            return reinterpret_cast<std::intptr_t>(comp->document);
        }
    }
    return 0;
}

auto internal_m2n_ui_document_get_element_wrapper_by_id(std::intptr_t document_ptr, entt::entity owner_entity, const std::string& element_id) -> std::intptr_t
{
    if (!validate_ui_document_wrapper(document_ptr, owner_entity))
    {
        return 0;
    }
    
    auto* document = reinterpret_cast<Rml::ElementDocument*>(document_ptr);
    auto* element = document->GetElementById(element_id);
    return reinterpret_cast<std::intptr_t>(element);
}

auto internal_m2n_ui_document_query_selector_wrapper(std::intptr_t document_ptr, entt::entity owner_entity, const std::string& selector) -> std::intptr_t
{
    if (!validate_ui_document_wrapper(document_ptr, owner_entity))
    {
        return 0;
    }
    
    auto* document = reinterpret_cast<Rml::ElementDocument*>(document_ptr);
    auto element = document->QuerySelector(selector);
    if (element)
    {
        return reinterpret_cast<std::intptr_t>(element);
    }
    
    return 0;
}

// Get element ID from element pointer
auto internal_m2n_ui_element_wrapper_get_id(std::intptr_t element_ptr, entt::entity owner_entity) -> std::string
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return "";
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    return element->GetId();
}

//-------------------------------------------------------------------------
// UI Document Wrapper Methods
//-------------------------------------------------------------------------

auto internal_m2n_ui_document_wrapper_is_valid(std::intptr_t document_ptr, entt::entity owner_entity) -> bool
{
    return validate_ui_document_wrapper(document_ptr, owner_entity);
}

auto internal_m2n_ui_document_wrapper_get_title(std::intptr_t document_ptr, entt::entity owner_entity) -> std::string
{
    if (!validate_ui_document_wrapper(document_ptr, owner_entity))
    {
        return "";
    }
    
    auto* document = reinterpret_cast<Rml::ElementDocument*>(document_ptr);
    return document->GetTitle();
}

void internal_m2n_ui_document_wrapper_set_title(std::intptr_t document_ptr, entt::entity owner_entity, const std::string& title)
{
    if (!validate_ui_document_wrapper(document_ptr, owner_entity))
    {
        return;
    }
    
    auto* document = reinterpret_cast<Rml::ElementDocument*>(document_ptr);
    document->SetTitle(title);
}

auto internal_m2n_ui_document_wrapper_is_visible(std::intptr_t document_ptr, entt::entity owner_entity) -> bool
{
    if (!validate_ui_document_wrapper(document_ptr, owner_entity))
    {
        return false;
    }
    
    auto* document = reinterpret_cast<Rml::ElementDocument*>(document_ptr);
    return document->IsVisible();
}

void internal_m2n_ui_document_wrapper_show(std::intptr_t document_ptr, entt::entity owner_entity)
{
    if (!validate_ui_document_wrapper(document_ptr, owner_entity))
    {
        return;
    }
    
    auto* document = reinterpret_cast<Rml::ElementDocument*>(document_ptr);
    document->Show();
}

void internal_m2n_ui_document_wrapper_hide(std::intptr_t document_ptr, entt::entity owner_entity)
{
    if (!validate_ui_document_wrapper(document_ptr, owner_entity))
    {
        return;
    }
    
    auto* document = reinterpret_cast<Rml::ElementDocument*>(document_ptr);
    document->Hide();
}

void internal_m2n_ui_document_wrapper_close(std::intptr_t document_ptr, entt::entity owner_entity)
{
    if (!validate_ui_document_wrapper(document_ptr, owner_entity))
    {
        return;
    }
    
    auto* document = reinterpret_cast<Rml::ElementDocument*>(document_ptr);
    document->Close();
}

//-------------------------------------------------------------------------
// UI Element Wrapper Methods  
//-------------------------------------------------------------------------

auto internal_m2n_ui_element_wrapper_is_valid(std::intptr_t element_ptr, entt::entity owner_entity) -> bool
{
    return validate_ui_element_wrapper(element_ptr, owner_entity);
}

auto internal_m2n_ui_element_wrapper_get_inner_rml(std::intptr_t element_ptr, entt::entity owner_entity) -> std::string
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return "";
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    return element->GetInnerRML();
}

void internal_m2n_ui_element_wrapper_set_inner_rml(std::intptr_t element_ptr, entt::entity owner_entity, const std::string& rml)
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    auto* element_text = rmlui_dynamic_cast<Rml::ElementText*>(element);

    if(!element_text)
    {
        if(auto* first_child = element->GetFirstChild())
        {
            element_text = rmlui_dynamic_cast<Rml::ElementText*>(first_child);
        }
    }

    if(element_text)
    {
        element_text->SetText(rml);
    }
    else
    {
        auto current_rml = element->GetInnerRML();
        if(current_rml != rml)
        {
            element->SetInnerRML(rml);
        }
    }
}

auto internal_m2n_ui_element_wrapper_is_visible(std::intptr_t element_ptr, entt::entity owner_entity) -> bool
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return false;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    return element->IsVisible();
}

void internal_m2n_ui_element_wrapper_set_visible(std::intptr_t element_ptr, entt::entity owner_entity, bool visible)
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    if (visible)
    {
        element->SetProperty("display", "block");
    }
    else
    {
        element->SetProperty("display", "none");
    }
}

auto internal_m2n_ui_element_wrapper_get_attribute(std::intptr_t element_ptr, entt::entity owner_entity, const std::string& attribute_name) -> std::string
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return "";
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    return element->GetAttribute<Rml::String>(attribute_name, "");
}

void internal_m2n_ui_element_wrapper_set_attribute(std::intptr_t element_ptr, entt::entity owner_entity, const std::string& attribute_name, const std::string& value)
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    element->SetAttribute(attribute_name, value);
}

void internal_m2n_ui_element_wrapper_remove_attribute(std::intptr_t element_ptr, entt::entity owner_entity, const std::string& attribute_name)
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    element->RemoveAttribute(attribute_name);
}

auto internal_m2n_ui_element_wrapper_has_attribute(std::intptr_t element_ptr, entt::entity owner_entity, const std::string& attribute_name) -> bool
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return false;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    return element->HasAttribute(attribute_name);
}

void internal_m2n_ui_element_wrapper_set_class(std::intptr_t element_ptr, entt::entity owner_entity, const std::string& class_name, bool activate)
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    element->SetClass(class_name, activate);
}

auto internal_m2n_ui_element_wrapper_is_class_set(std::intptr_t element_ptr, entt::entity owner_entity, const std::string& class_name) -> bool
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return false;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    return element->IsClassSet(class_name);
}

void internal_m2n_ui_element_wrapper_sync_transform_to_entity(std::intptr_t element_ptr, entt::entity owner_entity, entt::entity transform_entity)
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

    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);

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

void internal_m2n_ui_element_wrapper_focus(std::intptr_t element_ptr, entt::entity owner_entity)
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    element->Focus();
}

void internal_m2n_ui_element_wrapper_blur(std::intptr_t element_ptr, entt::entity owner_entity)
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    element->Blur();
}

void internal_m2n_ui_element_wrapper_click(std::intptr_t element_ptr, entt::entity owner_entity)
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    element->Click();
}

void internal_m2n_ui_element_wrapper_scroll_into_view(std::intptr_t element_ptr, entt::entity owner_entity, bool align_with_top)
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    element->ScrollIntoView(align_with_top);
}


} // namespace

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
    }
}

void register_ui_element_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.UIElement");
        reg.add_internal_call("internal_m2n_ui_element_wrapper_is_valid", dotnet_internal_call(internal_m2n_ui_element_wrapper_is_valid));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_inner_rml", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_inner_rml));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_inner_rml", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_inner_rml));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_is_visible", dotnet_internal_call(internal_m2n_ui_element_wrapper_is_visible));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_visible", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_visible));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_attribute", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_attribute));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_attribute", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_attribute));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_remove_attribute", dotnet_internal_call(internal_m2n_ui_element_wrapper_remove_attribute));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_has_attribute", dotnet_internal_call(internal_m2n_ui_element_wrapper_has_attribute));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_class", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_class));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_is_class_set", dotnet_internal_call(internal_m2n_ui_element_wrapper_is_class_set));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_sync_transform_to_entity", dotnet_internal_call(internal_m2n_ui_element_wrapper_sync_transform_to_entity));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_focus", dotnet_internal_call(internal_m2n_ui_element_wrapper_focus));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_blur", dotnet_internal_call(internal_m2n_ui_element_wrapper_blur));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_click", dotnet_internal_call(internal_m2n_ui_element_wrapper_click));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_scroll_into_view", dotnet_internal_call(internal_m2n_ui_element_wrapper_scroll_into_view));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_id", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_id));
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
    }
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.UIEventBase");
        reg.add_internal_call("internal_m2n_ui_stop_propagation", dotnet_internal_call(internal_m2n_ui_stop_propagation));
        reg.add_internal_call("internal_m2n_ui_stop_immediate_propagation",
                              dotnet_internal_call(internal_m2n_ui_stop_immediate_propagation));
    }
}

} // namespace unravel
