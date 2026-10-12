#include "ui_data_transforms.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Variant.h>

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace unravel
{
namespace
{

/// RmlUi registers these in every context; registering a name twice is an error.
const std::unordered_set<std::string>& get_builtin_transforms()
{
    static const std::unordered_set<std::string> names{"to_lower", "to_upper", "format", "round"};
    return names;
}

ui_transform_handler g_transform_handler;

/// Transform names registered per context generation.
std::unordered_map<uint64_t, std::unordered_set<std::string>> g_registered_transforms;

auto is_identifier_start(char c) -> bool
{
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

auto is_identifier_char(char c) -> bool
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

auto call_transform(entt::entity owner, const std::string& name, const Rml::VariantList& arguments) -> Rml::Variant
{
    Rml::Variant result;
    if(g_transform_handler && g_transform_handler(owner, name, arguments, result))
    {
        return result;
    }
    return arguments.empty() ? Rml::Variant() : arguments.front();
}

} // namespace

void set_ui_transform_handler(ui_transform_handler handler)
{
    g_transform_handler = std::move(handler);
}

void ensure_ui_transform(Rml::Context& context, uint64_t context_generation, entt::entity owner, const std::string& name)
{
    if(name.empty() || get_builtin_transforms().count(name) > 0)
    {
        return;
    }
    // Transforms live in the context's type register, which only data models reach.
    auto models = context.GetDataModels();
    if(models.empty() || !g_registered_transforms[context_generation].insert(name).second)
    {
        return;
    }
    models.begin()->second.RegisterTransformFunc(name,
                                                 [owner, name](const Rml::VariantList& arguments) -> Rml::Variant
                                                 {
                                                     return call_transform(owner, name, arguments);
                                                 });
}

auto collect_ui_transform_names(const std::string& rml) -> std::vector<std::string>
{
    std::vector<std::string> names;
    for(size_t pos = rml.find('|'); pos != std::string::npos; pos = rml.find('|', pos + 1))
    {
        // '||' is a logical or, not a transform.
        const bool is_double = (pos > 0 && rml[pos - 1] == '|') || (pos + 1 < rml.size() && rml[pos + 1] == '|');
        if(is_double)
        {
            continue;
        }
        size_t start = pos + 1;
        while(start < rml.size() && std::isspace(static_cast<unsigned char>(rml[start])))
        {
            ++start;
        }
        if(start >= rml.size() || !is_identifier_start(rml[start]))
        {
            continue;
        }
        size_t end = start;
        while(end < rml.size() && is_identifier_char(rml[end]))
        {
            ++end;
        }
        auto name = rml.substr(start, end - start);
        if(get_builtin_transforms().count(name) == 0 && std::find(names.begin(), names.end(), name) == names.end())
        {
            names.push_back(std::move(name));
        }
    }
    return names;
}

} // namespace unravel
