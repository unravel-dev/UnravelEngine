// UIDataModel: RmlUi data models whose variables, events and transforms scripts serve, and inline
// "on<event>" attributes that call script methods.
#include "script_glue_common.h"
#include "script_bindings.h"
#include "ui_event_payload.h"

#include "../script_system.h"
#include "../script_value_encoding.h"

#include <engine/play_mode.h>
#include <engine/ui/ecs/components/ui_document_component.h>
#include <engine/ui/ui_data_transforms.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataVariable.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/EventListenerInstancer.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Variant.h>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace unravel
{
namespace
{

/// Kinds of model nodes; the values mirror UIDataModels.NodeKind.
enum class model_node_kind : int64_t
{
    missing,
    scalar,
    structure,
    array
};

/// A child node id travels with its kind in the low bits.
constexpr int64_t NODE_KIND_BITS = 2;
constexpr int64_t NODE_KIND_MASK = (int64_t(1) << NODE_KIND_BITS) - 1;

using namespace script_value_encoding;

/// Name of the element count of an array variable in data expressions ("rows.size").
constexpr const char* ARRAY_SIZE_NAME = "size";

auto get_cache() -> const script_system::engine_script_cache&
{
    return engine::context().get_cached<script_system>().get_cache();
}

auto to_node(void* ptr) -> int64_t
{
    return static_cast<int64_t>(reinterpret_cast<std::intptr_t>(ptr));
}

auto format_number(double value) -> std::string
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return stream.str();
}

auto parse_number(const std::string& text) -> double
{
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    double value = 0.0;
    stream >> value;
    return value;
}

auto encode_variant(const Rml::Variant& variant) -> std::string
{
    switch(variant.GetType())
    {
        case Rml::Variant::NONE:
            return std::string(1, VALUE_MISSING);
        case Rml::Variant::BOOL:
            return std::string(1, VALUE_BOOL) + (variant.Get<bool>() ? "1" : "0");
        case Rml::Variant::BYTE:
        case Rml::Variant::CHAR:
        case Rml::Variant::INT:
        case Rml::Variant::INT64:
        case Rml::Variant::UINT:
        case Rml::Variant::UINT64:
            return VALUE_INTEGER + std::to_string(variant.Get<int64_t>());
        case Rml::Variant::FLOAT:
        case Rml::Variant::DOUBLE:
            return VALUE_NUMBER + format_number(variant.Get<double>());
        default:
            return VALUE_STRING + variant.Get<Rml::String>();
    }
}

/// @return false for a missing value.
auto decode_variant(const std::string& text, Rml::Variant& variant) -> bool
{
    if(text.empty())
    {
        return false;
    }
    const auto value = text.substr(1);
    switch(text[0])
    {
        case VALUE_BOOL:
            variant = value == "1";
            return true;
        case VALUE_INTEGER:
            variant = static_cast<int64_t>(std::strtoll(value.c_str(), nullptr, 10));
            return true;
        case VALUE_NUMBER:
            variant = parse_number(value);
            return true;
        case VALUE_STRING:
            variant = value;
            return true;
        default:
            return false;
    }
}

auto encode_variants(const Rml::VariantList& variants) -> std::string
{
    std::string text;
    for(size_t i = 0; i < variants.size(); ++i)
    {
        if(i > 0)
        {
            text += VALUE_SEPARATOR;
        }
        text += encode_variant(variants[i]);
    }
    return text;
}

void log_script_exception(const char* what, const dotnet::exception& e)
{
    APPLOG_ERROR("UI data model {}: {}", what, e.what());
}

//-------------------------------------------------------------------------
// Variables
//-------------------------------------------------------------------------

/// A data model variable whose value lives in a script: the pointer RmlUi keeps is a managed node id.
class managed_variable_definition final : public Rml::VariableDefinition
{
public:
    explicit managed_variable_definition(Rml::DataVariableType type) : Rml::VariableDefinition(type)
    {
    }

    auto Get(void* ptr, Rml::Variant& variant) -> bool override
    {
        try
        {
            auto invoker = get_cache().ui_model_get_invoker;
            return invoker.valid() && decode_variant(invoker(to_node(ptr)), variant);
        }
        catch(const dotnet::exception& e)
        {
            log_script_exception("read", e);
            return false;
        }
    }

    auto Set(void* ptr, const Rml::Variant& variant) -> bool override
    {
        try
        {
            auto invoker = get_cache().ui_model_set_invoker;
            if(invoker.valid())
            {
                invoker(to_node(ptr), encode_variant(variant));
                return true;
            }
        }
        catch(const dotnet::exception& e)
        {
            log_script_exception("write", e);
        }
        return false;
    }

    auto Size(void* ptr) -> int override
    {
        try
        {
            auto invoker = get_cache().ui_model_size_invoker;
            return invoker.valid() ? invoker(to_node(ptr)) : 0;
        }
        catch(const dotnet::exception& e)
        {
            log_script_exception("size", e);
            return 0;
        }
    }

    auto Child(void* ptr, const Rml::DataAddressEntry& address) -> Rml::DataVariable override;
};

managed_variable_definition g_scalar_definition(Rml::DataVariableType::Scalar);
managed_variable_definition g_struct_definition(Rml::DataVariableType::Struct);
managed_variable_definition g_array_definition(Rml::DataVariableType::Array);

/// The variable of a node id with its kind in the low bits.
auto make_model_variable(int64_t encoded_node) -> Rml::DataVariable
{
    void* ptr = reinterpret_cast<void*>(static_cast<std::intptr_t>(encoded_node >> NODE_KIND_BITS));
    switch(static_cast<model_node_kind>(encoded_node & NODE_KIND_MASK))
    {
        case model_node_kind::scalar:
            return Rml::DataVariable(&g_scalar_definition, ptr);
        case model_node_kind::structure:
            return Rml::DataVariable(&g_struct_definition, ptr);
        case model_node_kind::array:
            return Rml::DataVariable(&g_array_definition, ptr);
        default:
            return {};
    }
}

auto managed_variable_definition::Child(void* ptr, const Rml::DataAddressEntry& address) -> Rml::DataVariable
{
    if(Type() == Rml::DataVariableType::Array && address.index < 0 && address.name == ARRAY_SIZE_NAME)
    {
        return Rml::MakeLiteralIntVariable(Size(ptr));
    }
    try
    {
        auto invoker = get_cache().ui_model_child_invoker;
        return invoker.valid() ? make_model_variable(invoker(to_node(ptr), address.name, address.index)) : Rml::DataVariable();
    }
    catch(const dotnet::exception& e)
    {
        log_script_exception("member", e);
        return {};
    }
}

//-------------------------------------------------------------------------
// Models
//-------------------------------------------------------------------------

/// What scripts bound into an entity's current context, so each binding is made once per context.
struct model_binding_record
{
    uint64_t context_generation = 0;
    std::unordered_set<std::string> keys;
};

std::unordered_map<entt::entity, model_binding_record> g_binding_records;

auto get_document_component(entt::entity owner) -> ui_document_component*
{
    auto entity = get_entity_from_id(owner);
    return entity ? entity.try_get<ui_document_component>() : nullptr;
}

/// Records a binding into the owner's current context.
/// @return false when that binding was made already.
auto record_binding(entt::entity owner, const ui_document_component& comp, const std::string& key) -> bool
{
    auto& record = g_binding_records[owner];
    if(record.context_generation != comp.context_generation)
    {
        record = model_binding_record{comp.context_generation, {}};
    }
    return record.keys.insert(key).second;
}

auto find_data_model(const ui_document_component& comp, const std::string& model) -> Rml::DataModelConstructor
{
    if(!comp.context)
    {
        return {};
    }
    auto models = comp.context->GetDataModels();
    auto it = models.find(model);
    return it != models.end() ? it->second : Rml::DataModelConstructor();
}

void dispatch_model_event(entt::entity owner,
                          const std::string& model,
                          const std::string& name,
                          Rml::Event& event,
                          const Rml::VariantList& arguments)
{
    auto invoker = get_cache().ui_model_event_invoker;
    if(!invoker.valid())
    {
        return;
    }
    std::string text;
    const auto payload = make_ui_event_payload(event, false, text);
    const auto* target = event.GetTargetElement();
    const auto* current = event.GetCurrentElement();
    try
    {
        invoker(owner,
                model,
                name,
                encode_variants(arguments),
                payload,
                event.GetType(),
                target ? target->GetId() : std::string(),
                current ? current->GetId() : std::string(),
                std::move(text));
    }
    catch(const dotnet::exception& e)
    {
        log_script_exception("event", e);
    }
}

/// @return false when the owner's scripts have no transform of that name.
auto call_model_transform(entt::entity owner, const std::string& name, const Rml::VariantList& arguments, Rml::Variant& result)
    -> bool
{
    try
    {
        auto invoker = get_cache().ui_model_transform_invoker;
        return invoker.valid() && decode_variant(invoker(owner, name, encode_variants(arguments)), result);
    }
    catch(const dotnet::exception& e)
    {
        log_script_exception("transform", e);
        return false;
    }
}

auto internal_m2n_ui_model_get_generation(entt::entity owner) -> uint64_t
{
    const auto* comp = get_document_component(owner);
    return comp && comp->context ? comp->context_generation : 0;
}

auto internal_m2n_ui_model_exists(entt::entity owner, const std::string& model) -> bool
{
    const auto* comp = get_document_component(owner);
    return comp && find_data_model(*comp, model);
}

auto internal_m2n_ui_model_bind_variable(entt::entity owner,
                                         const std::string& model,
                                         const std::string& name,
                                         int64_t encoded_node) -> bool
{
    auto* comp = get_document_component(owner);
    auto constructor = comp ? find_data_model(*comp, model) : Rml::DataModelConstructor();
    if(!constructor)
    {
        return false;
    }
    if(record_binding(owner, *comp, "v:" + model + "/" + name) &&
       !constructor.BindCustomDataVariable(name, make_model_variable(encoded_node)))
    {
        return false;
    }
    // Views evaluated the name while it was missing: they show the value at the next update.
    constructor.GetModelHandle().DirtyVariable(name);
    return true;
}

auto internal_m2n_ui_model_bind_event(entt::entity owner, const std::string& model, const std::string& name) -> bool
{
    auto* comp = get_document_component(owner);
    auto constructor = comp ? find_data_model(*comp, model) : Rml::DataModelConstructor();
    if(!constructor)
    {
        return false;
    }
    if(!record_binding(owner, *comp, "e:" + model + "/" + name))
    {
        return true;
    }
    return constructor.BindEventCallback(name,
                                         [owner, model, name](Rml::DataModelHandle,
                                                              Rml::Event& event,
                                                              const Rml::VariantList& arguments)
                                         {
                                             dispatch_model_event(owner, model, name, event, arguments);
                                         });
}

auto internal_m2n_ui_model_add_transform(entt::entity owner, const std::string& model, const std::string& name) -> bool
{
    auto* comp = get_document_component(owner);
    if(!comp || !comp->context || !find_data_model(*comp, model))
    {
        return false;
    }
    // Transforms belong to the context, shared by its models; the markup's are registered at load.
    ensure_ui_transform(*comp->context, comp->context_generation, owner, name);
    return true;
}

void internal_m2n_ui_model_dirty(entt::entity owner, const std::string& model, const std::string& name)
{
    const auto* comp = get_document_component(owner);
    auto constructor = comp ? find_data_model(*comp, model) : Rml::DataModelConstructor();
    if(!constructor)
    {
        return;
    }
    auto handle = constructor.GetModelHandle();
    if(name.empty())
    {
        handle.DirtyAllVariables();
    }
    else
    {
        handle.DirtyVariable(name);
    }
}

//-------------------------------------------------------------------------
// Inline handlers: onclick="Spin" calls Spin on a script of the document's entity
//-------------------------------------------------------------------------

auto find_document_owner(const Rml::Element* element) -> entt::entity
{
    const auto* document = element ? element->GetOwnerDocument() : nullptr;
    if(!document)
    {
        return entt::null;
    }
    entt::entity owner = entt::null;
    auto& scene = engine::context().get_cached<ecs>().get_scene();
    scene.registry->view<ui_document_component>().each(
        [&](entt::entity entity, const ui_document_component& comp)
        {
            if(comp.document == document)
            {
                owner = entity;
            }
        });
    return owner;
}

void dispatch_inline_event(const std::string& handler, Rml::Event& event)
{
    auto& ctx = engine::context();
    auto invoker = ctx.get_cached<script_system>().get_cache().ui_inline_event_invoker;
    // Scripts run only while the game plays.
    if(!invoker.valid() || !ctx.get_cached<play_mode>().is_simulation_running())
    {
        return;
    }
    const auto owner = find_document_owner(event.GetCurrentElement());
    if(owner == entt::null)
    {
        return;
    }
    std::string text;
    const auto payload = make_ui_event_payload(event, false, text);
    const auto* target = event.GetTargetElement();
    const auto* current = event.GetCurrentElement();
    try
    {
        invoker(owner,
                handler,
                payload,
                event.GetType(),
                target ? target->GetId() : std::string(),
                current ? current->GetId() : std::string(),
                std::move(text));
    }
    catch(const dotnet::exception& e)
    {
        log_script_exception("inline handler", e);
    }
}

/// The listener of one inline attribute. RmlUi hands ownership back through OnDetach, called when
/// the attribute changes or the element is destroyed, so it deletes itself there.
class inline_script_listener final : public Rml::EventListener
{
    std::string handler_;

public:
    explicit inline_script_listener(std::string handler) : handler_(std::move(handler))
    {
    }

    void ProcessEvent(Rml::Event& event) override
    {
        dispatch_inline_event(handler_, event);
    }

    void OnDetach(Rml::Element*) override
    {
        delete this;
    }
};

class inline_script_listener_instancer final : public Rml::EventListenerInstancer
{
public:
    auto InstanceEventListener(const Rml::String& value, Rml::Element*) -> Rml::EventListener* override
    {
        return new inline_script_listener(value);
    }
};

inline_script_listener_instancer g_inline_listener_instancer;

} // namespace

void register_ui_data_model_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    auto reg = dotnet::internal_call_registry("Unravel.Core.UIDataModel");
    reg.add_internal_call("internal_m2n_ui_model_get_generation", script_internal_call(internal_m2n_ui_model_get_generation));
    reg.add_internal_call("internal_m2n_ui_model_exists", script_internal_call(internal_m2n_ui_model_exists));
    reg.add_internal_call("internal_m2n_ui_model_bind_variable", script_internal_call(internal_m2n_ui_model_bind_variable));
    reg.add_internal_call("internal_m2n_ui_model_bind_event", script_internal_call(internal_m2n_ui_model_bind_event));
    reg.add_internal_call("internal_m2n_ui_model_add_transform", script_internal_call(internal_m2n_ui_model_add_transform));
    reg.add_internal_call("internal_m2n_ui_model_dirty", script_internal_call(internal_m2n_ui_model_dirty));
    Rml::Factory::RegisterEventListenerInstancer(&g_inline_listener_instancer);
    set_ui_transform_handler(call_model_transform);
}

} // namespace unravel
