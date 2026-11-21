/**
 * @file binary_archive.cpp
 * @brief The binary archive (serialization/binary_archive.h) on compiled data:
 *   - packed lists (a mesh's triangles and bone weights, an animation's keys) load back what was saved, in a
 *     binary document and in JSON alike;
 *   - an array where a packed list is expected (a JSON document converted to binary) loads the same;
 *   - a packed list read by an element type other than the one that wrote it is refused, whether the
 *     elements read less or more than each row holds, and the list keeps what it held;
 *   - a packed list whose elements write no bytes is refused when saved, and the save reports it; a save
 *     that fails inside a list's rows leaves the list refused and what is saved after it whole;
 *   - the names a list's rows were given do not name what follows the list;
 *   - a compiled file cut short or with bytes changed fails its load without reading past its end, and a file
 *     that is not a binary document is refused without a throw;
 *   - both archives list an object's member keys alike, which loading an entity relies on;
 *   - vectors, quaternions and colors are one packed value in a binary document while JSON keeps the same
 *     objects of named numbers, a converted object still loads, an override of one component still applies,
 *     and numbers saved as one type read into another;
 *   - a binary document starts with the 32 zero bytes that keep an older positional reader from reading
 *     sizes out of its header;
 *   - the start of a document reads alone: its first values, and the count of a list the bytes read cut short;
 *     a graph material shader file's header reads from its start, and from the whole file when it is longer;
 *   - a stream that cannot seek is not checked for a document, and none of its bytes are taken;
 *   - a filtered load of a list steps over the elements it leaves out, in JSON and binary alike, and a filtered
 *     save of a packed value leaves out the component it filters, as JSON does;
 *   - a compiled file reports a value that will not load, a document read for a copy does not;
 *   - numbers read as the JSON archive reads them: a negative into an unsigned field, a fraction into an integer
 *     field and a number into a bool are refused, a whole number into a float is not;
 *   - asking whether a member is there leaves the read position where it was, as in JSON;
 *   - a keyed member that does not load is left out and the members after it still load;
 *   - a list whose count is more than its elements fails its load;
 *   - an empty document reads as no known layout in either format;
 *   - a save that throws past the archive writes nothing, rather than a document cut short that reads as whole.
 */

#include "../tests.h"

#include <engine/meta/animation/animation.hpp>
#include <engine/meta/core/math/quaternion.hpp>
#include <engine/meta/core/math/vector.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <engine/meta/rendering/graph_material.hpp>
#include <engine/meta/rendering/mesh.hpp>
#include <filesystem/filesystem.h>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/document_archive.h>
#include <serialization/document_convert.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <type_traits>
#include <vector>

using namespace unravel::test;

using namespace unravel;

namespace
{

/// Puts a path filter in force for its scope, and the context before it back when the scope ends, a failed case's
/// included.
class path_filter_in_force
{
public:
    explicit path_filter_in_force(serialization::path_context& filter) : previous_(serialization::get_path_context())
    {
        serialization::set_path_context(&filter);
    }

    ~path_filter_in_force()
    {
        serialization::set_path_context(previous_);
    }

    path_filter_in_force(const path_filter_in_force&) = delete;
    auto operator=(const path_filter_in_force&) -> path_filter_in_force& = delete;

private:
    serialization::path_context* previous_;
};

auto make_mesh_data() -> mesh::load_data
{
    mesh::load_data data;
    data.vertex_count = 600;
    data.vertex_data.resize(data.vertex_count * 32);
    for(size_t i = 0; i < data.vertex_data.size(); ++i)
    {
        data.vertex_data[i] = static_cast<uint8_t>(i * 7);
    }
    data.triangle_count = 1000;
    data.triangle_data.resize(data.triangle_count);
    for(uint32_t i = 0; i < data.triangle_count; ++i)
    {
        auto& triangle = data.triangle_data[i];
        triangle.data_group_id = i % 3;
        triangle.indices = {i % 600, (i + 1) % 600, (i * 7) % 600};
        triangle.flags = static_cast<uint8_t>(i & 0xFF);
    }
    for(uint32_t bone = 0; bone < 3; ++bone)
    {
        skin_bind_data::bone_influence influence;
        influence.bone_id = "bone_" + std::to_string(bone);
        for(uint32_t i = 0; i < 400; ++i)
        {
            influence.influences.push_back({(i * 3 + bone) % 600, 0.25f + 0.001f * float(i)});
        }
        data.skin_data.add_bone(influence);
    }
    for(uint32_t i = 0; i < 300; ++i)
    {
        data.gi_source_geometry.positions.emplace_back(float(i), float(i) * 0.5f, -float(i));
        data.gi_source_geometry.indices.push_back(i);
    }
    return data;
}

auto is_same_mesh_data(const mesh::load_data& lhs, const mesh::load_data& rhs) -> bool
{
    if(lhs.vertex_count != rhs.vertex_count || lhs.vertex_data != rhs.vertex_data ||
       lhs.triangle_count != rhs.triangle_count || lhs.triangle_data.size() != rhs.triangle_data.size() ||
       lhs.gi_source_geometry.positions != rhs.gi_source_geometry.positions ||
       lhs.gi_source_geometry.indices != rhs.gi_source_geometry.indices)
    {
        return false;
    }
    for(size_t i = 0; i < lhs.triangle_data.size(); ++i)
    {
        const auto& a = lhs.triangle_data[i];
        const auto& b = rhs.triangle_data[i];
        if(a.data_group_id != b.data_group_id || a.indices != b.indices || a.flags != b.flags)
        {
            return false;
        }
    }
    const auto& lhs_bones = lhs.skin_data.get_bones();
    const auto& rhs_bones = rhs.skin_data.get_bones();
    if(lhs_bones.size() != rhs_bones.size())
    {
        return false;
    }
    for(size_t bone = 0; bone < lhs_bones.size(); ++bone)
    {
        const auto& a = lhs_bones[bone];
        const auto& b = rhs_bones[bone];
        if(a.bone_id != b.bone_id || a.influences.size() != b.influences.size())
        {
            return false;
        }
        for(size_t i = 0; i < a.influences.size(); ++i)
        {
            if(a.influences[i].vertex_index != b.influences[i].vertex_index ||
               a.influences[i].weight != b.influences[i].weight)
            {
                return false;
            }
        }
    }
    return true;
}

auto save_binary_mesh(const mesh::load_data& data) -> std::string
{
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    serialization::write_binary(stream, [&](auto& ar) { try_save(ar, ser20::make_nvp("mesh", data)); });
    return stream.str();
}

auto load_binary_mesh(const std::string& bytes, mesh::load_data& data) -> bool
{
    std::stringstream stream(bytes, std::ios::in | std::ios::binary);
    return serialization::read_binary(stream, [&](auto& ar) { return try_load(ar, ser20::make_nvp("mesh", data)); });
}

auto save_json_mesh(const mesh::load_data& data) -> std::string
{
    std::stringstream stream;
    {
        auto ar = ser20::create_oarchive_associative(stream);
        try_save(ar, ser20::make_nvp("mesh", data));
    }
    return stream.str();
}

void test_packed_mesh_round_trip()
{
    const auto data = make_mesh_data();

    const auto binary = save_binary_mesh(data);
    mesh::load_data from_binary;
    check(load_binary_mesh(binary, from_binary), "the binary mesh loads");
    check(is_same_mesh_data(from_binary, data), "and holds the triangles, bone weights and GI source geometry saved");

    const auto json = save_json_mesh(data);
    mesh::load_data from_json;
    {
        std::stringstream stream(json);
        auto ar = ser20::create_iarchive_associative(stream);
        check(try_load(ar, ser20::make_nvp("mesh", from_json)), "the JSON mesh loads");
    }
    check(is_same_mesh_data(from_json, data), "JSON holds the packed lists as ordinary arrays");

    // Rows hold the values alone: a triangle is 17 bytes of values.
    check(binary.size() < data.vertex_data.size() + data.triangle_data.size() * 20 + 3 * 400 * 8 + 300 * 16 + 4096,
          "packed lists add no keys or type bytes per element (" + std::to_string(binary.size()) + " bytes)");
}

void test_packed_animation_round_trip()
{
    animation_clip clip;
    clip.name = "walk";
    clip.duration = animation_clip::seconds_t(2.0f);
    for(uint32_t channel = 0; channel < 2; ++channel)
    {
        animation_channel node;
        node.node_name = "node_" + std::to_string(channel);
        node.node_index = channel;
        for(uint32_t i = 0; i < 100; ++i)
        {
            const float time = float(i) / 50.0f;
            node.position_keys.push_back({animation_channel::seconds_t(time), math::vec3(time, 1.0f, -time)});
            node.rotation_keys.push_back(
                {animation_channel::seconds_t(time), math::angleAxis(time, math::vec3(0.0f, 1.0f, 0.0f))});
            node.scaling_keys.push_back({animation_channel::seconds_t(time), math::vec3(1.0f + time)});
        }
        clip.channels.push_back(node);
    }

    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    serialization::write_binary(stream, [&](auto& ar) { try_save(ar, ser20::make_nvp("animation", clip)); });
    animation_clip loaded;
    check(serialization::read_binary(stream,
                                     [&](auto& ar) { return try_load(ar, ser20::make_nvp("animation", loaded)); }),
          "the binary clip loads");
    check(loaded.channels.size() == clip.channels.size(), "with its channels");
    bool same_keys = loaded.channels.size() == clip.channels.size();
    for(size_t c = 0; same_keys && c < clip.channels.size(); ++c)
    {
        same_keys = loaded.channels[c].node_name == clip.channels[c].node_name &&
                    loaded.channels[c].position_keys == clip.channels[c].position_keys &&
                    loaded.channels[c].rotation_keys == clip.channels[c].rotation_keys &&
                    loaded.channels[c].scaling_keys == clip.channels[c].scaling_keys;
    }
    check(same_keys, "and every position, rotation and scaling key");
}

void test_converted_array_loads_as_packed()
{
    const auto data = make_mesh_data();
    std::string error;
    const auto converted = serialization::json_to_binary_document(save_json_mesh(data), error);
    check(!converted.empty(), "the JSON mesh converts" + (error.empty() ? std::string{} : ": " + error));
    mesh::load_data loaded;
    check(load_binary_mesh(std::string(converted.begin(), converted.end()), loaded), "the converted mesh loads");
    check(is_same_mesh_data(loaded, data), "its arrays load where packed lists are expected");
}

void test_packed_list_of_other_type_refused()
{
    const auto data = make_mesh_data();
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    serialization::write_binary(stream,
                                [&](auto& ar)
                                { try_save(ar, ser20::make_nvp("list", serialization::packed(data.triangle_data))); });
    std::vector<skin_bind_data::vertex_influence> influences{{7u, 0.5f}};
    const bool loaded = serialization::read_binary(
        stream,
        [&](auto& ar) { return try_load(ar, ser20::make_nvp("list", serialization::packed(influences))); });
    check(!loaded, "rows of triangles read as bone weights, which read less than each row holds, are refused");
    check(influences.size() == 1 && influences[0].vertex_index == 7u, "and the list keeps what it held");

    const auto& weights = data.skin_data.get_bones()[0].influences;
    std::stringstream weights_stream(std::ios::in | std::ios::out | std::ios::binary);
    serialization::write_binary(weights_stream,
                                [&](auto& ar) { try_save(ar, ser20::make_nvp("list", serialization::packed(weights))); });
    std::decay_t<decltype(data.triangle_data)> triangles(1);
    triangles[0].flags = 9;
    const bool loaded_triangles = serialization::read_binary(
        weights_stream,
        [&](auto& ar) { return try_load(ar, ser20::make_nvp("list", serialization::packed(triangles))); });
    check(!loaded_triangles, "rows of bone weights read as triangles, which read more than each row holds, are refused");
    check(triangles.size() == 1 && triangles[0].flags == 9, "and that list keeps what it held too");
}

/// An element whose save writes nothing.
struct empty_row
{
};

template<typename Archive>
void SAVE_FUNCTION_NAME(Archive& /*ar*/, const empty_row& /*row*/)
{
}

template<typename Archive>
void LOAD_FUNCTION_NAME(Archive& /*ar*/, empty_row& /*row*/)
{
}

/// An element holding a list of elements that write nothing.
struct rows_of_nothing
{
    std::vector<empty_row> rows;
};

template<typename Archive>
void SAVE_FUNCTION_NAME(Archive& ar, const rows_of_nothing& element)
{
    try_save(ar, ser20::make_nvp("rows", element.rows));
}

template<typename Archive>
void LOAD_FUNCTION_NAME(Archive& ar, rows_of_nothing& element)
{
    try_load(ar, ser20::make_nvp("rows", element.rows));
}

void test_rows_of_nothing_refused()
{
    const std::vector<empty_row> rows(4);
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    const bool saved = serialization::write_binary(
        stream,
        [&](auto& ar) { return try_save(ar, ser20::make_nvp("rows", serialization::packed(rows))); });
    check(!saved, "a packed list whose elements write no bytes is refused when saved, as a reader would refuse it");

    // The save fails inside the rows: in a list each element holds.
    const std::vector<rows_of_nothing> lists(2, rows_of_nothing{std::vector<empty_row>(3)});
    std::stringstream nested(std::ios::in | std::ios::out | std::ios::binary);
    serialization::write_binary(nested,
                                [&](auto& ar)
                                {
                                    try_save(ar, ser20::make_nvp("lists", serialization::packed(lists)));
                                    try_save(ar, ser20::make_nvp("after", int32_t(7)));
                                });
    std::vector<rows_of_nothing> loaded;
    int32_t after = 0;
    bool lists_loaded = true;
    serialization::read_binary(nested,
                               [&](auto& ar)
                               {
                                   lists_loaded = try_load(ar, ser20::make_nvp("lists", serialization::packed(loaded)));
                                   return try_load(ar, ser20::make_nvp("after", after));
                               });
    check(!lists_loaded && loaded.empty(), "a list whose save failed inside its rows is refused");
    check(after == 7, "and what was saved after it reads back");
}

void test_names_end_with_the_rows()
{
    animation_channel channel;
    channel.position_keys.push_back({animation_channel::seconds_t(0.5f), math::vec3(1.0f, 2.0f, 3.0f)});
    channel.position_keys.push_back({animation_channel::seconds_t(1.0f), math::vec3(4.0f, 5.0f, 6.0f)});
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    serialization::write_binary(stream,
                                [&](auto& ar)
                                {
                                    try_save(ar, ser20::make_nvp("keys", serialization::packed(channel.position_keys)));
                                    ar(int32_t(42));
                                });

    // A key's last value is a named vector, written into the rows without its name.
    const auto bytes = stream.str();
    serialization::tagged::reader document(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    std::vector<std::string> names;
    serialization::tagged::walk_values(document,
                                       [&](const char* name, const auto& /*value*/, size_t depth)
                                       {
                                           if(depth == 0 && name != nullptr)
                                           {
                                               names.emplace_back(name);
                                           }
                                           return true;
                                       });
    check(names == std::vector<std::string>{"keys", "value0"},
          "a value saved after a packed list takes its own name, not the last name its rows were given");

    std::decay_t<decltype(channel.position_keys)> keys;
    int32_t after = 0;
    serialization::read_binary(stream,
                               [&](auto& ar)
                               {
                                   try_load(ar, ser20::make_nvp("keys", serialization::packed(keys)));
                                   ar(after);
                                   return true;
                               });
    check(keys == channel.position_keys && after == 42,
          "and a load reads it after the list without looking for a name the rows used");
}

void test_broken_compiled_mesh_refused()
{
    const auto binary = save_binary_mesh(make_mesh_data());
    size_t loads = 0;
    const size_t cut_step = std::max<size_t>(1, binary.size() / 131);
    for(size_t size = 0; size < binary.size(); size += cut_step)
    {
        mesh::load_data loaded;
        load_binary_mesh(binary.substr(0, size), loaded);
        ++loads;
    }
    const size_t flip_step = std::max<size_t>(1, binary.size() / 211);
    for(size_t at = 0; at < binary.size(); at += flip_step)
    {
        auto corrupt = binary;
        corrupt[at] = static_cast<char>(corrupt[at] ^ 0x5A);
        mesh::load_data loaded;
        load_binary_mesh(corrupt, loaded);
        ++loads;
    }
    check(loads > 300, "every broken mesh's load returned");

    // What the positional archive wrote: a count, then values with no header.
    const std::string positional("\x01\x00\x00\x00\x00\x00\x00\x00\x07\x00\x00\x00\x00\x00\x00\x00" "default", 23);
    mesh::load_data loaded;
    check(!load_binary_mesh(positional, loaded), "a file in the old positional layout is refused, not thrown");
}

void test_member_keys_listed_alike()
{
    struct sample
    {
        int first = 1;
        float second = 2.0f;
        std::string third = "three";
    };
    const sample value;
    auto save = [&](auto& ar)
    {
        ar.setNextName("object");
        ar.startNode();
        try_save(ar, ser20::make_nvp("first", value.first));
        try_save(ar, ser20::make_nvp("second", value.second));
        try_save(ar, ser20::make_nvp("third", value.third));
        ar.finishNode();
    };
    auto list = [](auto& ar)
    {
        std::vector<uint64_t> keys;
        ar.setNextName("object");
        ar.startNode();
        ar.forEachMemberKey([&](uint64_t hash) { keys.push_back(hash); });
        ar.finishNode();
        return keys;
    };

    std::stringstream binary(std::ios::in | std::ios::out | std::ios::binary);
    serialization::write_binary(binary, save);
    std::vector<uint64_t> binary_keys;
    serialization::read_binary(binary,
                               [&](auto& ar)
                               {
                                   binary_keys = list(ar);
                                   return true;
                               });

    std::stringstream json;
    {
        auto ar = ser20::create_oarchive_associative(json);
        save(ar);
    }
    auto ar = ser20::create_iarchive_associative(json);
    const auto json_keys = list(ar);

    const std::vector<uint64_t> expected{ser20::name_hash("first"), ser20::name_hash("second"), ser20::name_hash("third")};
    check(binary_keys == expected, "the binary archive lists the object's keys in order");
    check(json_keys == expected, "the JSON archive lists the same hashes");
}

/// What a binary document stores for vectors, quaternions and colors, and what JSON keeps.
struct math_values
{
    math::vec2 two{1.5f, -2.0f};
    math::vec3 three{1.0f, 2.0f, 3.0f};
    math::vec4 four{4.0f, 5.0f, 6.0f, 7.0f};
    math::quat rotation{0.5f, 0.5f, 0.5f, 0.5f};
    math::color tint{0.25f, 0.5f, 0.75f, 1.0f};
    math::ivec3 cell{-3, 4, 5};
};

template<typename Archive>
void save_math_values(Archive& ar, const math_values& values)
{
    try_save(ar, ser20::make_nvp("two", values.two));
    try_save(ar, ser20::make_nvp("three", values.three));
    try_save(ar, ser20::make_nvp("four", values.four));
    try_save(ar, ser20::make_nvp("rotation", values.rotation));
    try_save(ar, ser20::make_nvp("tint", values.tint));
    try_save(ar, ser20::make_nvp("cell", values.cell));
}

template<typename Archive>
void load_math_values(Archive& ar, math_values& values)
{
    try_load(ar, ser20::make_nvp("two", values.two));
    try_load(ar, ser20::make_nvp("three", values.three));
    try_load(ar, ser20::make_nvp("four", values.four));
    try_load(ar, ser20::make_nvp("rotation", values.rotation));
    try_load(ar, ser20::make_nvp("tint", values.tint));
    try_load(ar, ser20::make_nvp("cell", values.cell));
}

auto is_same_math_values(const math_values& lhs, const math_values& rhs) -> bool
{
    return lhs.two == rhs.two && lhs.three == rhs.three && lhs.four == rhs.four && lhs.rotation == rhs.rotation &&
           lhs.tint.value == rhs.tint.value && lhs.cell == rhs.cell;
}

auto save_math_values_json(const math_values& values) -> std::string
{
    std::stringstream stream;
    {
        auto ar = ser20::create_oarchive_associative(stream);
        save_math_values(ar, values);
    }
    return stream.str();
}

/// The JSON a vector, a quaternion and a color were written as before they had a packed form: an object of
/// named numbers, written member by member.
auto save_math_values_json_by_member(const math_values& values) -> std::string
{
    std::stringstream stream;
    {
        auto ar = ser20::create_oarchive_associative(stream);
        auto members = [&](const char* name, auto& components, const auto& names)
        {
            ar.setNextName(name);
            ar.startNode();
            for(size_t index = 0; index < names.size(); ++index)
            {
                try_save(ar, ser20::make_nvp(names[index], *components[index]));
            }
            ar.finishNode();
        };
        auto copy = values;
        auto two = std::array<float*, 2>{&copy.two.x, &copy.two.y};
        auto three = std::array<float*, 3>{&copy.three.x, &copy.three.y, &copy.three.z};
        auto four = std::array<float*, 4>{&copy.four.x, &copy.four.y, &copy.four.z, &copy.four.w};
        auto rotation = std::array<float*, 4>{&copy.rotation.x, &copy.rotation.y, &copy.rotation.z, &copy.rotation.w};
        auto tint = std::array<float*, 4>{&copy.tint.value.r, &copy.tint.value.g, &copy.tint.value.b, &copy.tint.value.a};
        auto cell = std::array<int*, 3>{&copy.cell.x, &copy.cell.y, &copy.cell.z};
        members("two", two, serialization::xy_names);
        members("three", three, serialization::xyz_names);
        members("four", four, serialization::xyzw_names);
        members("rotation", rotation, serialization::xyzw_names);
        members("tint", tint, serialization::rgba_names);
        members("cell", cell, serialization::xyz_names);
    }
    return stream.str();
}

void test_packed_values_keep_json()
{
    const math_values values;
    const auto json = save_math_values_json(values);
    check_eq(json, save_math_values_json_by_member(values), "JSON writes vectors, quaternions and colors as objects of named numbers, as before");

    math_values loaded;
    loaded.three = math::vec3(0.0f);
    loaded.cell = math::ivec3(0);
    {
        std::stringstream stream(json);
        auto ar = ser20::create_iarchive_associative(stream);
        load_math_values(ar, loaded);
    }
    check(is_same_math_values(loaded, values), "and reads them back");
}

void test_packed_values_round_trip()
{
    const math_values values;
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    serialization::write_binary(stream, [&](auto& ar) { save_math_values(ar, values); });
    const auto bytes = stream.str();
    check(bytes.size() < 48 + 6 * 24 + 64, "each value is one packed value (" + std::to_string(bytes.size()) + " bytes)");

    math_values loaded;
    loaded.three = math::vec3(0.0f);
    loaded.cell = math::ivec3(0);
    serialization::read_binary(stream,
                               [&](auto& ar)
                               {
                                   load_math_values(ar, loaded);
                                   return true;
                               });
    check(is_same_math_values(loaded, values), "a binary document reads its packed values back");

    std::string error;
    const auto converted = serialization::json_to_binary_document(save_math_values_json(values), error);
    math_values from_converted;
    from_converted.three = math::vec3(0.0f);
    std::stringstream converted_stream(std::string(converted.begin(), converted.end()), std::ios::in | std::ios::binary);
    serialization::read_binary(converted_stream,
                               [&](auto& ar)
                               {
                                   load_math_values(ar, from_converted);
                                   return true;
                               });
    check(is_same_math_values(from_converted, values), "a document converted from JSON reads its objects into the same values");
}

void test_packed_value_component_override()
{
    math_values values;
    values.three = math::vec3(7.0f, 8.0f, 9.0f);
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    serialization::write_binary(stream, [&](auto& ar) { save_math_values(ar, values); });

    // An override of three/x: the load leaves that one component as it is.
    serialization::path_context filter;
    filter.should_serialize_property_callback = [](const std::string& path)
    {
        return path != "three/x";
    };
    filter.enable_recording();
    math_values loaded;
    loaded.three = math::vec3(-1.0f);
    {
        const path_filter_in_force in_force(filter);
        serialization::read_binary(stream,
                                   [&](auto& ar)
                                   {
                                       load_math_values(ar, loaded);
                                       return true;
                                   });
    }
    check(loaded.three == math::vec3(-1.0f, 8.0f, 9.0f), "an override of one component keeps it on a packed value");
}

void test_packed_value_converts_numbers()
{
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    const math::dvec3 wide{1.25, -2.5, 3.75};
    serialization::write_binary(stream, [&](auto& ar) { try_save(ar, ser20::make_nvp("value", wide)); });
    math::vec3 narrow(0.0f);
    const bool loaded =
        serialization::read_binary(stream, [&](auto& ar) { return try_load(ar, ser20::make_nvp("value", narrow)); });
    check(loaded && narrow == math::vec3(1.25f, -2.5f, 3.75f), "a value saved as doubles reads into floats");
}

void test_document_preamble()
{
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    serialization::write_binary(stream, [&](auto& ar) { save_math_values(ar, math_values{}); });
    const auto bytes = stream.str();
    bool zero = bytes.size() > 32;
    for(size_t index = 0; zero && index < 32; ++index)
    {
        zero = bytes[index] == 0;
    }
    check(zero, "a binary document starts with 32 zero bytes, which an older positional reader reads as empty sizes");
    auto shifted = bytes.substr(32);
    check(!serialization::is_binary_document(shifted.data(), shifted.size()), "a document without them is refused");
}

void test_document_head_read()
{
    const std::vector<std::string> variants(40, std::string(10000, 'v'));
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    serialization::write_binary(stream,
                                [&](auto& ar)
                                {
                                    try_save(ar, ser20::make_nvp("code_hash", std::string("abc")));
                                    try_save(ar, ser20::make_nvp("variants", variants));
                                    try_save(ar, ser20::make_nvp("after", int32_t(7)));
                                });

    std::string code_hash;
    ser20::size_type count = 0;
    bool has_after = true;
    const bool read = serialization::read_binary_head(stream,
                                                      4096,
                                                      [&](auto& ar)
                                                      {
                                                          const bool has_hash =
                                                              try_load(ar, ser20::make_nvp("code_hash", code_hash));
                                                          ar.setNextName("variants");
                                                          ar.startNode();
                                                          ar(ser20::make_size_tag(count));
                                                          ar.finishNode();
                                                          has_after = ar.hasNextName("after");
                                                          return has_hash;
                                                      });
    check(read && code_hash == "abc", "the start of a document reads its first values");
    check(count == 40, "a list the bytes read cut short still reads its count");
    check(!has_after, "and what lies past the bytes read is absent");
}

/// A stream buffer over bytes that cannot seek, as a pipe is read.
class forward_only_buffer : public std::streambuf
{
public:
    explicit forward_only_buffer(std::string bytes) : bytes_(std::move(bytes))
    {
        setg(bytes_.data(), bytes_.data(), bytes_.data() + bytes_.size());
    }

private:
    std::string bytes_;
};

void test_forward_only_stream()
{
    const math_values values;
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    serialization::write_binary(stream, [&](auto& ar) { save_math_values(ar, values); });
    const auto bytes = stream.str();

    forward_only_buffer buffer(bytes);
    std::istream input(&buffer);
    check(!serialization::is_binary_document(input), "a stream that cannot seek is not checked for a document");
    check(buffer.in_avail() == static_cast<std::streamsize>(bytes.size()), "and none of its bytes are taken");

    math_values loaded;
    loaded.three = math::vec3(0.0f);
    const bool read = serialization::read_binary(input,
                                                 [&](auto& ar)
                                                 {
                                                     load_math_values(ar, loaded);
                                                     return true;
                                                 });
    check(read && is_same_math_values(loaded, values), "it still reads as a whole");
}

void test_shader_header_read()
{
    fs::error_code ec;
    const auto folder = fs::resolve_protocol("app:/binary_archive");
    fs::create_directories(folder, ec);

    compiled_graph_material_shader shader;
    shader.code_hash = "code";
    shader.compile_parameters = "parameters";
    shader.variants.resize(2);
    shader.variants[0].bytes.assign(200000, 7);
    const auto file = (folder / "shader.dxil").string();
    save_to_file_bin(file, shader);
    compiled_graph_material_shader_header header;
    check(load_header_from_file_bin(file, header) && header.code_hash == "code" &&
              header.compile_parameters == "parameters" && header.has_variants,
          "a shader file's header reads from its start");

    shader.compile_parameters.assign(100000, 'p');
    const auto long_file = (folder / "long_header.dxil").string();
    save_to_file_bin(long_file, shader);
    compiled_graph_material_shader_header long_header;
    check(load_header_from_file_bin(long_file, long_header) && long_header.compile_parameters == shader.compile_parameters,
          "a header longer than the bytes read first is read from the whole file");

    // Parameters that end just inside the bytes read first: the variants are cut short or past them.
    for(const size_t size : {65400u, 65480u, 65530u})
    {
        shader.compile_parameters.assign(size, 'p');
        const auto edge_file = (folder / ("edge_" + std::to_string(size) + ".dxil")).string();
        save_to_file_bin(edge_file, shader);
        compiled_graph_material_shader_header edge_header;
        check(load_header_from_file_bin(edge_file, edge_header) && edge_header.has_variants &&
                  edge_header.compile_parameters.size() == size,
              "variants past the bytes read first are read from the whole file (" + std::to_string(size) + ")");
    }
    fs::remove_all(folder, ec);
}

/// Loads a list of three with a filter that leaves its second element out.
template<typename Archive>
auto load_list_leaving_out_the_second(Archive& ar) -> std::vector<std::string>
{
    std::vector<std::string> list{"x", "y", "z"};
    serialization::path_context filter;
    filter.should_serialize_property_callback = [](const std::string& path)
    {
        return path != "list[1]";
    };
    filter.enable_recording();
    const path_filter_in_force in_force(filter);
    try_load(ar, ser20::make_nvp("list", list));
    return list;
}

void test_filtered_list_load()
{
    const std::vector<std::string> saved{"a", "b", "c"};
    const std::vector<std::string> expected{"a", "y", "c"};

    std::stringstream json;
    {
        auto ar = ser20::create_oarchive_associative(json);
        try_save(ar, ser20::make_nvp("list", saved));
    }
    auto json_ar = ser20::create_iarchive_associative(json);
    check(load_list_leaving_out_the_second(json_ar) == expected,
          "a filtered JSON load steps over the element it leaves out, so the next is read from its own place");

    std::stringstream binary(std::ios::in | std::ios::out | std::ios::binary);
    serialization::write_binary(binary, [&](auto& ar) { return try_save(ar, ser20::make_nvp("list", saved)); });
    std::vector<std::string> from_binary;
    serialization::read_binary(binary,
                               [&](auto& ar)
                               {
                                   from_binary = load_list_leaving_out_the_second(ar);
                                   return true;
                               });
    check(from_binary == expected, "and so does a binary one");
}

void test_filtered_save_of_packed_value()
{
    const math::vec3 value(1.0f, 2.0f, 3.0f);
    serialization::path_context filter;
    filter.should_serialize_property_callback = [](const std::string& path)
    {
        return path != "three/y";
    };
    filter.enable_recording();

    std::stringstream binary(std::ios::in | std::ios::out | std::ios::binary);
    std::stringstream json;
    {
        const path_filter_in_force in_force(filter);
        serialization::write_binary(binary, [&](auto& ar) { return try_save(ar, ser20::make_nvp("three", value)); });
        auto ar = ser20::create_oarchive_associative(json);
        try_save(ar, ser20::make_nvp("three", value));
    }

    math::vec3 from_binary(-1.0f);
    serialization::read_binary(binary, [&](auto& ar) { return try_load(ar, ser20::make_nvp("three", from_binary)); });
    math::vec3 from_json(-1.0f);
    {
        auto ar = ser20::create_iarchive_associative(json);
        try_load(ar, ser20::make_nvp("three", from_json));
    }
    check(from_binary == math::vec3(1.0f, -1.0f, 3.0f),
          "a filtered binary save of a packed value leaves out the component it filters");
    check(from_binary == from_json, "as a filtered JSON save does");
}

void test_failed_loads_reported_for_compiled_files()
{
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    serialization::write_binary(stream, [&](auto& ar) { save_math_values(ar, math_values{}); });
    const auto bytes = stream.str();

    bool compiled_reports = false;
    serialization::read_binary(stream,
                               [&](auto& ar)
                               {
                                   compiled_reports = ar.reportsFailedLoads();
                                   return true;
                               });
    bool copy_reports = true;
    serialization::read_document(bytes.data(),
                                 bytes.size(),
                                 [&](auto& ar)
                                 {
                                     if constexpr(requires { ar.reportsFailedLoads(); })
                                     {
                                         copy_reports = ar.reportsFailedLoads();
                                     }
                                 });
    check(compiled_reports, "a compiled file reports a value that will not load");
    check(!copy_reports, "a document read for a copy, or converted from JSON, reads as quietly as JSON");
}

/// Saves value under "value" in a binary document and reads it into a T; whether the read took it.
template<typename T, typename U>
auto read_as(const U& value, T& read) -> bool
{
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    serialization::write_binary(stream, [&](auto& ar) { return try_save(ar, ser20::make_nvp("value", value)); });
    return serialization::read_binary(stream, [&](auto& ar) { return try_load(ar, ser20::make_nvp("value", read)); });
}

void test_numbers_read_as_json_reads_them()
{
    uint32_t count = 5;
    check(!read_as(int32_t(-1), count) && count == 5, "a negative number is refused by an unsigned field");
    int32_t whole = 5;
    check(!read_as(2.0, whole) && whole == 5, "a fraction is refused by an integer field");
    bool flag = false;
    check(!read_as(int32_t(1), flag) && !flag, "a number is refused by a bool");
    float fraction = 0.0f;
    check(read_as(int64_t(3), fraction) && fraction == 3.0f, "a whole number reads into a float");
    int64_t wide = 0;
    check(read_as(uint32_t(4000000000u), wide) && wide == 4000000000, "an unsigned number reads into a wider signed field");
}

void test_probe_leaves_the_read_position()
{
    auto save = [](auto& ar)
    {
        try_save(ar, ser20::make_nvp("first", int32_t(1)));
        try_save(ar, ser20::make_nvp("second", int32_t(2)));
    };
    auto probe_then_read = [](auto& ar)
    {
        const bool has_second = ar.hasNextName("second");
        int32_t value = 0;
        ar(value);
        return has_second ? value : -1;
    };

    std::stringstream json;
    {
        auto ar = ser20::create_oarchive_associative(json);
        save(ar);
    }
    auto json_ar = ser20::create_iarchive_associative(json);
    const auto from_json = probe_then_read(json_ar);

    std::stringstream binary(std::ios::in | std::ios::out | std::ios::binary);
    serialization::write_binary(binary, save);
    int32_t from_binary = 0;
    serialization::read_binary(binary,
                               [&](auto& ar)
                               {
                                   from_binary = probe_then_read(ar);
                                   return true;
                               });
    check(from_json == 1 && from_binary == 1, "asking for a later member leaves the read position where it was");
}

void test_keyed_members_go_on_past_a_failure()
{
    auto save = [](auto& ar)
    {
        ar.setNextName("members");
        ar.startNode();
        try_save(ar, ser20::make_nvp("a", int32_t(1)));
        try_save(ar, ser20::make_nvp("b", std::string("text")));
        try_save(ar, ser20::make_nvp("c", int32_t(3)));
        ar.finishNode();
    };
    auto load = [](auto& ar)
    {
        std::vector<std::string> loaded;
        serialization::load_named_members(ar,
                                          "members",
                                          [&](const std::string& name)
                                          {
                                              int32_t value = 0;
                                              if(!try_load(ar, ser20::make_nvp(name.c_str(), value)))
                                              {
                                                  return false;
                                              }
                                              loaded.push_back(name);
                                              return true;
                                          });
        return loaded;
    };
    const std::vector<std::string> expected{"a", "c"};

    std::stringstream json;
    {
        auto ar = ser20::create_oarchive_associative(json);
        save(ar);
    }
    auto json_ar = ser20::create_iarchive_associative(json);
    check(load(json_ar) == expected, "a keyed member that does not load is left out and the rest load, in JSON");

    std::stringstream binary(std::ios::in | std::ios::out | std::ios::binary);
    serialization::write_binary(binary, save);
    std::vector<std::string> from_binary;
    serialization::read_binary(binary,
                               [&](auto& ar)
                               {
                                   from_binary = load(ar);
                                   return true;
                               });
    check(from_binary == expected, "and in binary");
}

void test_overstated_count_refused()
{
    const std::vector<std::string> saved{"one", "two", "three"};
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    serialization::write_binary(stream, [&](auto& ar) { return try_save(ar, ser20::make_nvp("list", saved)); });
    auto bytes = stream.str();

    // The list's node header follows its key and type byte: body bytes, then the count.
    serialization::tagged::reader document(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    const auto list = document.peek();
    const auto count_at = static_cast<size_t>(list.payload - reinterpret_cast<const uint8_t*>(bytes.data())) + 4;
    const uint32_t overstated = 9;
    std::memcpy(bytes.data() + count_at, &overstated, sizeof(overstated));

    std::vector<std::string> loaded;
    std::stringstream broken(bytes, std::ios::in | std::ios::binary);
    const bool read =
        serialization::read_binary(broken, [&](auto& ar) { return try_load(ar, ser20::make_nvp("list", loaded)); });
    check(!read, "a list that counts more elements than it holds fails its load");
}

void test_empty_document_has_no_layout()
{
    std::string error;
    const auto converted = serialization::json_to_binary_document("{}", error);
    const hpp::string_view binary(reinterpret_cast<const char*>(converted.data()), converted.size());
    check(read_document_version("{}") == UNKNOWN_DOCUMENT_VERSION &&
              read_document_version(binary) == UNKNOWN_DOCUMENT_VERSION,
          "an empty document reads as no known layout, as JSON and as binary");
}

void test_throwing_save_writes_nothing()
{
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    bool thrown = false;
    try
    {
        serialization::write_binary(stream,
                                    [&](auto& ar)
                                    {
                                        try_save(ar, ser20::make_nvp("first", int32_t(1)));
                                        throw std::runtime_error("The save stops here.");
                                    });
    }
    catch(const std::runtime_error&)
    {
        thrown = true;
    }
    check(thrown && stream.str().empty() && stream.bad(),
          "a save that throws past the archive writes nothing and leaves the stream failed");
}

void run_binary_archive_suite(rtti::context& /*ctx*/)
{
    RUN_CASE(test_packed_mesh_round_trip());
    RUN_CASE(test_packed_animation_round_trip());
    RUN_CASE(test_converted_array_loads_as_packed());
    RUN_CASE(test_packed_list_of_other_type_refused());
    RUN_CASE(test_rows_of_nothing_refused());
    RUN_CASE(test_names_end_with_the_rows());
    RUN_CASE(test_broken_compiled_mesh_refused());
    RUN_CASE(test_member_keys_listed_alike());
    RUN_CASE(test_packed_values_keep_json());
    RUN_CASE(test_packed_values_round_trip());
    RUN_CASE(test_packed_value_component_override());
    RUN_CASE(test_packed_value_converts_numbers());
    RUN_CASE(test_document_preamble());
    RUN_CASE(test_document_head_read());
    RUN_CASE(test_shader_header_read());
    RUN_CASE(test_forward_only_stream());
    RUN_CASE(test_filtered_list_load());
    RUN_CASE(test_filtered_save_of_packed_value());
    RUN_CASE(test_failed_loads_reported_for_compiled_files());
    RUN_CASE(test_numbers_read_as_json_reads_them());
    RUN_CASE(test_probe_leaves_the_read_position());
    RUN_CASE(test_keyed_members_go_on_past_a_failure());
    RUN_CASE(test_overstated_count_refused());
    RUN_CASE(test_empty_document_has_no_layout());
    RUN_CASE(test_throwing_save_writes_nothing());
}

} // namespace

REGISTER_TEST_SUITE("binary archive", run_binary_archive_suite)
