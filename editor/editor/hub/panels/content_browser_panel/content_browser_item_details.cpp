#include "content_browser_item_details.h"

#include <engine/animation/animation.h>
#include <engine/assets/impl/asset_reader.h>
#include <engine/audio/audio_clip.h>
#include <engine/physics/physics_material.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <graphics/texture.h>
#include <logging/logging.h>

#include <bimg/bimg.h>

#include <array>

namespace unravel
{
// Unity builds merge the anonymous namespaces of the editor's sources; the helpers' own namespace keeps their
// generic names from clashing.
namespace item_details
{
namespace
{
constexpr double BYTES_PER_KILOBYTE = 1024.0;
constexpr uint64_t DIGIT_GROUP_SIZE = 1000;
constexpr double COMPACT_COUNT_STEP = 1000.0;
/// Half of the last decimal the badge prints.
constexpr double COMPACT_ROUNDING_MARGIN = 0.05;
constexpr double SECONDS_PER_MINUTE = 60.0;
constexpr uint8_t MONO_CHANNELS = 1;
constexpr uint8_t STEREO_CHANNELS = 2;

/// A byte count as a compact, human friendly string, e.g. "1.4 MB".
auto format_byte_size(uint64_t bytes) -> std::string
{
    constexpr std::array<const char*, 5> units{"B", "KB", "MB", "GB", "TB"};
    auto value = static_cast<double>(bytes);
    size_t unit = 0;
    while(value >= BYTES_PER_KILOBYTE && unit + 1 < units.size())
    {
        value /= BYTES_PER_KILOBYTE;
        ++unit;
    }
    if(unit == 0)
    {
        return fmt::format("{} {}", bytes, units[0]);
    }
    return fmt::format("{:.1f} {}", value, units[unit]);
}

/// A count with its thousands grouped, e.g. "12,480".
auto format_grouped_count(uint64_t count) -> std::string
{
    if(count < DIGIT_GROUP_SIZE)
    {
        return std::to_string(count);
    }
    return fmt::format("{},{:03}", format_grouped_count(count / DIGIT_GROUP_SIZE), count % DIGIT_GROUP_SIZE);
}

/// A grouped count followed by the noun in agreement with it, e.g. "1 set", "12 sets".
auto format_counted_noun(uint64_t count, const char* singular, const char* plural) -> std::string
{
    return fmt::format("{} {}", format_grouped_count(count), count == 1 ? singular : plural);
}

/// A count shortened for the card's badge, e.g. "12.5K".
auto format_compact_count(uint64_t count) -> std::string
{
    constexpr std::array<const char*, 4> suffixes{"", "K", "M", "B"};
    auto value = static_cast<double>(count);
    size_t suffix = 0;
    // Step up early enough that rounding to one decimal never prints "1000.0K".
    while(value >= COMPACT_COUNT_STEP - COMPACT_ROUNDING_MARGIN && suffix + 1 < suffixes.size())
    {
        value /= COMPACT_COUNT_STEP;
        ++suffix;
    }
    if(suffix == 0)
    {
        return std::to_string(count);
    }
    return fmt::format("{:.1f}{}", value, suffixes[suffix]);
}

/// A duration for the tooltip: seconds below a minute ("12.40 s"), minutes and seconds above ("2:05.3").
auto format_duration(double seconds) -> std::string
{
    if(seconds < SECONDS_PER_MINUTE)
    {
        return fmt::format("{:.2f} s", seconds);
    }
    const auto minutes = static_cast<uint64_t>(seconds / SECONDS_PER_MINUTE);
    return fmt::format("{}:{:04.1f}", minutes, seconds - double(minutes) * SECONDS_PER_MINUTE);
}

/// A duration shortened for the card's badge ("12.4s", "2:05").
auto format_compact_duration(double seconds) -> std::string
{
    if(seconds < SECONDS_PER_MINUTE)
    {
        return fmt::format("{:.1f}s", seconds);
    }
    const auto whole_seconds = static_cast<uint64_t>(seconds);
    const auto seconds_per_minute = static_cast<uint64_t>(SECONDS_PER_MINUTE);
    return fmt::format("{}:{:02}", whole_seconds / seconds_per_minute, whole_seconds % seconds_per_minute);
}

/// The asset once its load has finished, without starting one. Hovering an entry is a use of its asset, so
/// reading it refreshes the asset's last access like any other use.
template<typename T>
auto get_loaded_asset(const asset_handle<T>& asset) -> std::shared_ptr<T>
{
    constexpr bool WAIT = false;
    return asset.is_ready() ? asset.get(WAIT) : nullptr;
}

/// The size of a file on disk, empty when it cannot be read.
auto get_file_size_text(const fs::path& path) -> std::string
{
    fs::error_code ec;
    if(!fs::exists(path, ec))
    {
        return {};
    }
    const auto bytes = fs::file_size(path, ec);
    return ec ? std::string{} : format_byte_size(bytes);
}

/// The smallest and largest voxel edge of a mesh's distance fields, one value when they agree.
auto format_voxel_size_range(float min_size, float max_size) -> std::string
{
    if(min_size == max_size)
    {
        return fmt::format("{:.4g}", min_size);
    }
    return fmt::format("{:.4g} - {:.4g}", min_size, max_size);
}

/// Vertex and triangle counts, submeshes, LODs, skinning, bounds and CPU memory of a mesh.
auto make_mesh_section(const mesh& loaded) -> content_item_section
{
    const mesh::info info = loaded.get_info();
    content_item_section section{"Mesh"};
    section.add("Vertices", format_grouped_count(info.vertices));
    section.add("Triangles", format_grouped_count(info.triangles));
    section.add("Submeshes", format_grouped_count(info.submeshes));
    section.add("Materials", format_grouped_count(info.data_groups));
    for(size_t lod = 0; lod < info.lods.size(); ++lod)
    {
        const auto& lod_info = info.lods[lod];
        section.add(fmt::format("LOD {}", lod + 1),
                    fmt::format("{} tris ({:.0f}%)", format_grouped_count(lod_info.triangles), lod_info.percent));
    }
    const auto& bones = loaded.get_skin_bind_data().get_bones();
    if(!bones.empty())
    {
        section.add("Bones", format_grouped_count(bones.size()));
    }
    const math::vec3 extent = loaded.get_bounds().get_dimensions();
    section.add("Bounds", fmt::format("{:.2f} x {:.2f} x {:.2f}", extent.x, extent.y, extent.z));
    section.add("Vertex Memory", info.vertex_memory);
    section.add("Index Memory", info.index_memory);
    return section;
}

/// The distance fields and Lumen cards global illumination keeps for a mesh.
auto make_mesh_gi_section(const mesh& loaded) -> content_item_section
{
    const mesh::gi_info gi = loaded.get_gi_info();
    content_item_section section{"Global Illumination"};
    if(gi.fields == 0)
    {
        section.add("Distance Fields", "None");
    }
    else
    {
        section.add("Distance Fields",
                    fmt::format("{} of {}",
                                gi.fields,
                                format_counted_noun(loaded.get_submeshes_count(), "submesh", "submeshes")));
        if(gi.two_sided_fields > 0)
        {
            section.add("Two-Sided Fields", format_grouped_count(gi.two_sided_fields));
        }
        section.add("Voxel Size", format_voxel_size_range(gi.min_voxel_size, gi.max_voxel_size));
        section.add("Surface Bricks", format_grouped_count(gi.surface_bricks));
        section.add("Field Memory", fmt::format("{} ({} finest)", gi.field_memory, gi.finest_field_memory));
    }
    section.add("Lumen Cards", gi.card_source);
    if(gi.cards > 0)
    {
        section.add("Cards",
                    fmt::format("{} in {}", format_grouped_count(gi.cards), format_counted_noun(gi.card_sets, "set", "sets")));
        section.add("Max Cards / Submesh", format_grouped_count(gi.max_cards_per_submesh));
        section.add("Card Memory", gi.card_memory);
        section.add("Card Table", fmt::format("{} per placement", gi.card_table_memory));
    }
    if(!loaded.are_lumen_cards_disabled())
    {
        section.add("Cards LOD", std::to_string(loaded.get_lumen_cards_lod()));
    }
    return section;
}

/// The texture maps a PBR material assigns, by name.
auto get_assigned_map_names(const pbr_material& pbr) -> std::string
{
    const std::array<std::pair<const char*, bool>, 6> maps{{
        {"Color", static_cast<bool>(pbr.get_color_map())},
        {"Normal", static_cast<bool>(pbr.get_normal_map())},
        {"Roughness", static_cast<bool>(pbr.get_roughness_map())},
        {"Metalness", static_cast<bool>(pbr.get_metalness_map())},
        {"AO", static_cast<bool>(pbr.get_ao_map())},
        {"Emissive", static_cast<bool>(pbr.get_emissive_map())},
    }};
    std::string names;
    for(const auto& [name, is_assigned] : maps)
    {
        if(is_assigned)
        {
            names += names.empty() ? name : fmt::format(", {}", name);
        }
    }
    return names.empty() ? "None" : names;
}

/// Display name of a material's alpha mode, with the cutoff for masked materials.
auto get_alpha_mode_text(const pbr_material& pbr) -> std::string
{
    switch(pbr.get_alpha_mode())
    {
        case alpha_mode::opaque:
            return "Opaque";
        case alpha_mode::mask:
            return fmt::format("Mask (cutoff {:.2f})", pbr.get_alpha_cutoff());
        case alpha_mode::blend:
            return "Blend";
    }
    return {};
}

/// Display name of a material's face culling.
auto get_cull_type_text(cull_type type) -> std::string
{
    switch(type)
    {
        case cull_type::none:
            return "None (two-sided)";
        case cull_type::clockwise:
            return "Clockwise";
        case cull_type::counter_clockwise:
            return "Counter-clockwise";
    }
    return {};
}

/// Display name of how two physics materials combine a coefficient.
auto get_combine_mode_text(combine_mode mode) -> const char*
{
    switch(mode)
    {
        case combine_mode::average:
            return "average";
        case combine_mode::minimum:
            return "minimum";
        case combine_mode::multiply:
            return "multiply";
        case combine_mode::maximum:
            return "maximum";
        default:
            return "";
    }
}

/// Display name of an audio channel layout.
auto get_channel_layout_text(uint8_t channels) -> std::string
{
    if(channels == MONO_CHANNELS)
    {
        return "Mono";
    }
    if(channels == STEREO_CHANNELS)
    {
        return "Stereo";
    }
    return std::to_string(channels);
}
} // namespace
} // namespace item_details

void content_item_section::add(std::string label, std::string value)
{
    if(value.empty())
    {
        return;
    }
    details.push_back({std::move(label), std::move(value)});
}

void collect_file_details(const fs::directory_cache::cache_entry& entry,
                          const hpp::uuid& uid,
                          content_item_sections& sections)
{
    content_item_section section;
    section.add("Name", entry.filename);
    section.add("Path", entry.protocol_path);
    if(!entry.entry.is_directory())
    {
        section.add("Disk Size", item_details::get_file_size_text(entry.entry.path()));
        const auto compiled_path = asset_reader::resolve_compiled_asset_path(entry.protocol_path, entry.extension);
        if(!compiled_path.empty())
        {
            section.add("Compiled Size", item_details::get_file_size_text(compiled_path));
        }
        if(!uid.is_nil())
        {
            section.add("UID", uid.to_string());
        }
    }
    sections.push_back(std::move(section));
}

auto get_asset_summary(const asset_handle<mesh>& asset) -> std::string
{
    const auto loaded = asset.peek();
    // A mesh still being compiled has no triangles yet; a "0 tris" badge would misreport it.
    if(!loaded || loaded->get_face_count() == 0)
    {
        return {};
    }
    return fmt::format("{} tris", item_details::format_compact_count(loaded->get_face_count()));
}

auto get_asset_summary(const asset_handle<gfx::texture>& asset) -> std::string
{
    const auto loaded = asset.peek();
    if(!loaded)
    {
        return {};
    }
    return fmt::format("{}x{}", loaded->info.width, loaded->info.height);
}

auto get_asset_summary(const asset_handle<audio_clip>& asset) -> std::string
{
    const auto loaded = asset.peek();
    if(!loaded)
    {
        return {};
    }
    return item_details::format_compact_duration(loaded->get_info().duration.count());
}

auto get_asset_summary(const asset_handle<animation_clip>& asset) -> std::string
{
    const auto loaded = asset.peek();
    if(!loaded)
    {
        return {};
    }
    return item_details::format_compact_duration(loaded->duration.count());
}

void collect_asset_details(const asset_handle<mesh>& asset, content_item_sections& sections)
{
    const auto loaded = item_details::get_loaded_asset(asset);
    if(!loaded)
    {
        return;
    }
    sections.push_back(item_details::make_mesh_section(*loaded));
    sections.push_back(item_details::make_mesh_gi_section(*loaded));
}

void collect_asset_details(const asset_handle<gfx::texture>& asset, content_item_sections& sections)
{
    const auto loaded = item_details::get_loaded_asset(asset);
    if(!loaded)
    {
        return;
    }
    const auto& info = loaded->info;
    content_item_section section{"Texture"};
    section.add("Size",
                info.depth > 1 ? fmt::format("{} x {} x {}", info.width, info.height, info.depth)
                               : fmt::format("{} x {}", info.width, info.height));
    section.add("Format", bimg::getName(bimg::TextureFormat::Enum(info.format)));
    section.add("Mips", std::to_string(info.numMips));
    if(info.numLayers > 1)
    {
        section.add("Layers", std::to_string(info.numLayers));
    }
    if(info.cubeMap)
    {
        section.add("Cube Map", "Yes");
    }
    section.add("GPU Memory", item_details::format_byte_size(info.storageSize));
    sections.push_back(std::move(section));
}

void collect_asset_details(const asset_handle<audio_clip>& asset, content_item_sections& sections)
{
    const auto loaded = item_details::get_loaded_asset(asset);
    if(!loaded)
    {
        return;
    }
    const auto& info = loaded->get_info();
    content_item_section section{"Audio"};
    section.add("Duration", item_details::format_duration(info.duration.count()));
    section.add("Sample Rate", fmt::format("{} Hz", item_details::format_grouped_count(info.sample_rate)));
    section.add("Channels", item_details::get_channel_layout_text(info.channels));
    section.add("Bit Depth", fmt::format("{} bit", uint32_t(info.bits_per_sample)));
    section.add("Frames", item_details::format_grouped_count(info.frames));
    sections.push_back(std::move(section));
}

void collect_asset_details(const asset_handle<animation_clip>& asset, content_item_sections& sections)
{
    const auto loaded = item_details::get_loaded_asset(asset);
    if(!loaded)
    {
        return;
    }
    content_item_section section{"Animation"};
    section.add("Clip", loaded->name);
    section.add("Duration", item_details::format_duration(loaded->duration.count()));
    section.add("Animated Nodes", item_details::format_grouped_count(loaded->channels.size()));
    section.add("Root Motion Node", loaded->root_motion.position_node_name);
    sections.push_back(std::move(section));
}

void collect_asset_details(const asset_handle<material>& asset, content_item_sections& sections)
{
    const auto loaded = item_details::get_loaded_asset(asset);
    if(!loaded)
    {
        return;
    }
    content_item_section section{"Material"};
    section.add("Culling", item_details::get_cull_type_text(loaded->get_cull_type()));
    if(const auto pbr = std::dynamic_pointer_cast<pbr_material>(loaded))
    {
        section.add("Shading", "PBR");
        section.add("Alpha", item_details::get_alpha_mode_text(*pbr));
        section.add("Roughness", fmt::format("{:.2f}", pbr->get_roughness()));
        section.add("Metalness", fmt::format("{:.2f}", pbr->get_metalness()));
        section.add("Texture Maps", item_details::get_assigned_map_names(*pbr));
        if(pbr->get_emissive_intensity() > 0.0f)
        {
            section.add("Emissive Intensity", fmt::format("{:.2f}", pbr->get_emissive_intensity()));
        }
    }
    sections.push_back(std::move(section));
}

void collect_asset_details(const asset_handle<physics_material>& asset, content_item_sections& sections)
{
    const auto loaded = item_details::get_loaded_asset(asset);
    if(!loaded)
    {
        return;
    }
    content_item_section section{"Physics Material"};
    section.add("Friction",
                fmt::format("{:.2f} ({})",
                            loaded->friction,
                            item_details::get_combine_mode_text(loaded->friction_combine)));
    section.add("Restitution",
                fmt::format("{:.2f} ({})",
                            loaded->restitution,
                            item_details::get_combine_mode_text(loaded->restitution_combine)));
    section.add("Stiffness", fmt::format("{:.2f}", loaded->stiffness));
    section.add("Damping", fmt::format("{:.2f}", loaded->damping));
    sections.push_back(std::move(section));
}

} // namespace unravel
