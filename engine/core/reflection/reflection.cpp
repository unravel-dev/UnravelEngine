#include "reflection.h"

namespace entt
{

auto get_pretty_name(meta_type t) -> std::string
{
    attributes attrs = t.custom();
    if(attrs.find("pretty_name") != attrs.end())
    {
        return attrs["pretty_name"].cast<std::string>();
    }

    return std::string(t.info().name());
}

auto get_pretty_name(const meta_data& prop) -> std::string
{
    attributes attrs = prop.custom();
    if(attrs.find("pretty_name") != attrs.end())
    {
        return attrs["pretty_name"].cast<std::string>();
    }

    if(attrs.find("name") != attrs.end())
    {
        return attrs["name"].cast<std::string>();
    }

    return "N/A";
}

auto property_predicate(std::function<bool(meta_handle&)> predicate) -> std::function<bool(meta_handle&)>
{
    return std::move(predicate);
}

}


namespace rttr
{
auto get_pretty_name(type t) -> std::string
{
    std::string name = t.get_name().to_string();
    auto meta_id = t.get_metadata("pretty_name");
    if(meta_id)
    {
        name = meta_id.to_string();
    }
    return name;
}

auto get_pretty_name(const rttr::property& prop) -> std::string
{
    std::string name = prop.get_name().to_string();
    auto meta_id = prop.get_metadata("pretty_name");
    if(meta_id)
    {
        name = meta_id.to_string();
    }
    return name;
}

auto property_predicate(std::function<bool(rttr::instance&)> predicate) -> std::function<bool(rttr::instance&)>
{
    return std::move(predicate);
}


} // namespace rttr

auto register_type_helper(const char* name) -> int
{
      return 0;
}
