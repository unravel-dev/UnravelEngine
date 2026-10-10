#pragma once
#include <engine/assets/asset_handle.h>
#include <filesystem/cache.hpp>
#include <hpp/uuid.hpp>

#include <string>
#include <vector>

namespace gfx
{
struct texture;
} // namespace gfx

namespace unravel
{
class mesh;
class material;
struct physics_material;
struct audio_clip;
struct animation_clip;

/// One labeled value of an item's tooltip, such as "Triangles" / "12,480".
struct content_item_detail
{
    std::string label;
    std::string value;
};

/// A titled group of tooltip details, such as the "Mesh" or "Global Illumination" facts of a mesh, or the "File"
/// facts every file has.
struct content_item_section
{
    std::string title;
    std::vector<content_item_detail> details;

    /**
     * @brief Appends a row. An empty value is skipped, so optional facts can be passed unconditionally.
     * @param label The row's label.
     * @param value The row's value.
     */
    void add(std::string label, std::string value);
};

using content_item_sections = std::vector<content_item_section>;

/**
 * @brief Collects the facts every file has: its size on disk and its compiled size. The tooltip shows the name,
 * path and uid in its header and footer.
 * @param entry The directory cache entry of the item.
 * @param sections Receives a "File" section; nothing for a folder.
 */
void collect_file_details(const fs::directory_cache::cache_entry& entry, content_item_sections& sections);

/**
 * @brief A short type-specific fact badged on the card's thumbnail, such as a triangle count.
 *
 * Asset types without one fall back to this overload. Called for every visible entry each frame, so it reads only an
 * instance something else already holds and never extends the asset's residency: an asset nothing uses has no badge.
 * @return The fact, empty for none.
 */
template<typename T>
auto get_asset_summary(const asset_handle<T>& /*asset*/) -> std::string
{
    return {};
}

/// Triangle count of the base LOD.
auto get_asset_summary(const asset_handle<mesh>& asset) -> std::string;
/// Resolution.
auto get_asset_summary(const asset_handle<gfx::texture>& asset) -> std::string;
/// Duration.
auto get_asset_summary(const asset_handle<audio_clip>& asset) -> std::string;
/// Duration.
auto get_asset_summary(const asset_handle<animation_clip>& asset) -> std::string;

/**
 * @brief Appends the type-specific tooltip sections of an asset.
 *
 * Asset types without type-specific facts fall back to this overload. Reads the asset once its load has finished and
 * never starts one; called only while the entry is hovered, so it counts as a use of the asset.
 * @param asset The asset.
 * @param sections Receives the sections.
 */
template<typename T>
void collect_asset_details(const asset_handle<T>& /*asset*/, content_item_sections& /*sections*/)
{
}

/// Geometry, LODs, skinning and memory, then the distance fields and mesh cards global illumination keeps.
void collect_asset_details(const asset_handle<mesh>& asset, content_item_sections& sections);
/// Resolution, format, mips, layers and GPU memory.
void collect_asset_details(const asset_handle<gfx::texture>& asset, content_item_sections& sections);
/// Duration, sample rate, channels and bit depth.
void collect_asset_details(const asset_handle<audio_clip>& asset, content_item_sections& sections);
/// Duration, animated nodes and root motion.
void collect_asset_details(const asset_handle<animation_clip>& asset, content_item_sections& sections);
/// Shading, alpha mode, culling, texture maps and emission.
void collect_asset_details(const asset_handle<material>& asset, content_item_sections& sections);
/// Friction, restitution, stiffness and damping.
void collect_asset_details(const asset_handle<physics_material>& asset, content_item_sections& sections);

} // namespace unravel
