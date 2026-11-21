#include "material_graph.hpp"

#include <engine/assets/impl/asset_writer.h>
#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/core/math/vector.hpp>

#include <filesystem/file_istream.h>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/string.hpp>
#include <serialization/types/vector.hpp>

#include <filesystem/filesystem.h>

#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace unravel
{

namespace
{
/// The file format's version, saved for migrations to read.
constexpr uint32_t MATERIAL_GRAPH_FORMAT_VERSION = 1;
constexpr const char* ROOT_NAME = "material_graph";

/// The value's components up to its last that is not zero, at least one: [0.5] reads better than four numbers.
auto to_components(const math::vec4& value) -> std::vector<float>
{
    size_t count = 4;
    while(count > 1 && value[int(count - 1)] == 0.0f)
    {
        --count;
    }
    std::vector<float> components(count);
    for(size_t i = 0; i < count; ++i)
    {
        components[i] = value[int(i)];
    }
    return components;
}

auto from_components(const std::vector<float>& components) -> math::vec4
{
    math::vec4 value{};
    for(size_t i = 0; i < components.size() && i < 4; ++i)
    {
        value[int(i)] = components[i];
    }
    return value;
}
} // namespace

SAVE(material_graph_input)
{
    try_save(ar, ser20::make_nvp("pin", obj.pin));
    try_save(ar, ser20::make_nvp("link", obj.link));
    const std::vector<float> components = to_components(obj.value);
    try_save(ar, ser20::make_nvp("value", components));
}
SAVE_INSTANTIATE(material_graph_input, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(material_graph_input, ser20::oarchive_binary_t);

LOAD(material_graph_input)
{
    try_load(ar, ser20::make_nvp("pin", obj.pin));
    try_load(ar, ser20::make_nvp("link", obj.link));
    std::vector<float> components;
    if(try_load(ar, ser20::make_nvp("value", components)))
    {
        obj.value = from_components(components);
    }
}
LOAD_INSTANTIATE(material_graph_input, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(material_graph_input, ser20::iarchive_binary_t);

SERIALIZE(material_graph_setting)
{
    try_serialize(ar, ser20::make_nvp("name", obj.name));
    try_serialize(ar, ser20::make_nvp("value", obj.value));
}
SERIALIZE_INSTANTIATE(material_graph_setting, ser20::oarchive_associative_t);
SERIALIZE_INSTANTIATE(material_graph_setting, ser20::oarchive_binary_t);
SERIALIZE_INSTANTIATE(material_graph_setting, ser20::iarchive_associative_t);
SERIALIZE_INSTANTIATE(material_graph_setting, ser20::iarchive_binary_t);

SAVE(material_graph_node)
{
    try_save(ar, ser20::make_nvp("id", obj.id));
    try_save(ar, ser20::make_nvp("type", obj.type));
    try_save(ar, ser20::make_nvp("position", obj.position));
    try_save(ar, ser20::make_nvp("inputs", obj.inputs));
    try_save(ar, ser20::make_nvp("settings", obj.settings));
    // A node lists only what it has, so a hand-written graph and a saved one read alike.
    if(obj.texture)
    {
        try_save(ar, ser20::make_nvp("texture", obj.texture));
        // The key besides the uid: readable, and what a graph written by hand names the texture by.
        try_save(ar, ser20::make_nvp("texture_key", obj.texture ? obj.texture.id() : std::string{}));
    }
    if(!obj.split_outputs.empty())
    {
        try_save(ar, ser20::make_nvp("split_outputs", obj.split_outputs));
    }
    if(!obj.split_inputs.empty())
    {
        try_save(ar, ser20::make_nvp("split_inputs", obj.split_inputs));
    }
    if(obj.is_preview_hidden)
    {
        try_save(ar, ser20::make_nvp("preview_hidden", obj.is_preview_hidden));
    }
}
SAVE_INSTANTIATE(material_graph_node, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(material_graph_node, ser20::oarchive_binary_t);

LOAD(material_graph_node)
{
    try_load(ar, ser20::make_nvp("id", obj.id));
    try_load(ar, ser20::make_nvp("type", obj.type));
    try_load(ar, ser20::make_nvp("position", obj.position));
    try_load(ar, ser20::make_nvp("inputs", obj.inputs));
    try_load(ar, ser20::make_nvp("settings", obj.settings));
    try_load(ar, ser20::make_nvp("texture", obj.texture));
    std::string texture_key;
    try_load(ar, ser20::make_nvp("texture_key", texture_key));
    // A uid that is not known (or none) falls back to the key.
    if(!obj.texture && !texture_key.empty())
    {
        auto& am = engine::context().get_cached<asset_manager>();
        obj.texture = am.get_asset<gfx::texture>(texture_key, load_flags::standard, load_mode::deferred);
    }
    try_load(ar, ser20::make_nvp("split_outputs", obj.split_outputs));
    try_load(ar, ser20::make_nvp("split_inputs", obj.split_inputs));
    try_load(ar, ser20::make_nvp("preview_hidden", obj.is_preview_hidden));
}
LOAD_INSTANTIATE(material_graph_node, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(material_graph_node, ser20::iarchive_binary_t);

SERIALIZE(material_graph_comment)
{
    try_serialize(ar, ser20::make_nvp("text", obj.text));
    try_serialize(ar, ser20::make_nvp("position", obj.position));
    try_serialize(ar, ser20::make_nvp("size", obj.size));
}
SERIALIZE_INSTANTIATE(material_graph_comment, ser20::oarchive_associative_t);
SERIALIZE_INSTANTIATE(material_graph_comment, ser20::oarchive_binary_t);
SERIALIZE_INSTANTIATE(material_graph_comment, ser20::iarchive_associative_t);
SERIALIZE_INSTANTIATE(material_graph_comment, ser20::iarchive_binary_t);

SAVE(material_graph)
{
    try_save(ar, ser20::make_nvp("version", MATERIAL_GRAPH_FORMAT_VERSION));
    try_save(ar, ser20::make_nvp("nodes", obj.nodes));
    try_save(ar, ser20::make_nvp("comments", obj.comments));
}
SAVE_INSTANTIATE(material_graph, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(material_graph, ser20::oarchive_binary_t);

LOAD(material_graph)
{
    uint32_t version = MATERIAL_GRAPH_FORMAT_VERSION;
    try_load(ar, ser20::make_nvp("version", version));
    try_load(ar, ser20::make_nvp("nodes", obj.nodes));
    try_load(ar, ser20::make_nvp("comments", obj.comments));
    upgrade_material_graph(obj);
}
LOAD_INSTANTIATE(material_graph, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(material_graph, ser20::iarchive_binary_t);

auto save_to_string(const material_graph& obj) -> std::string
{
    std::stringstream stream;
    {
        auto ar = ser20::create_oarchive_associative(stream);
        try_save(ar, ser20::make_nvp(ROOT_NAME, obj));
    }
    return stream.str();
}

auto load_from_string(const std::string& text, material_graph& obj) -> bool
{
    std::stringstream stream(text);
    try
    {
        auto ar = ser20::create_iarchive_associative(stream);
        return try_load(ar, ser20::make_nvp(ROOT_NAME, obj));
    }
    catch(const std::exception&)
    {
        return false;
    }
}

auto save_to_file(const std::string& absolute_path, const material_graph& obj) -> bool
{
    const auto text = save_to_string(obj);
    fs::error_code ec;
    asset_writer::atomic_write_file(
        absolute_path,
        [&text](const fs::path& temp)
        {
            std::ofstream stream(temp, std::ios::binary);
            stream << text;
            if(!stream.good())
            {
                throw std::runtime_error("Writing " + temp.generic_string() + " failed.");
            }
        },
        ec);
    return !ec;
}

auto load_from_file(const std::string& absolute_path, material_graph& obj) -> bool
{
    fs::file_istream input(absolute_path);
    if(!input.is_open())
    {
        return false;
    }
    std::stringstream text;
    text << input.rdbuf();
    return load_from_string(text.str(), obj);
}

auto load_material_function(const std::string& key) -> std::shared_ptr<const material_graph>
{
    struct cached_function
    {
        fs::file_time_type write_time{};
        std::shared_ptr<const material_graph> graph;
    };
    static std::mutex mutex;
    static std::map<std::string, cached_function> cache;
    const auto path = fs::resolve_protocol(key);
    fs::error_code ec;
    const auto write_time = fs::last_write_time(path, ec);
    if(ec)
    {
        return nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto found = cache.find(key);
        if(found != cache.end() && found->second.write_time == write_time)
        {
            return found->second.graph;
        }
    }
    auto graph = std::make_shared<material_graph>();
    if(!load_from_file(path.string(), *graph))
    {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex);
    cache[key] = {write_time, graph};
    return graph;
}

} // namespace unravel
