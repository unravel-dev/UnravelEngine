#pragma once
#include <engine/engine_export.h>

#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/material_instance.h>

#include <vector>

namespace unravel
{

/**
 * @brief One post-process material a volume or a camera runs: the material, how strongly it shows, and this entry's
 * overrides of its parameters.
 */
class post_process_material_entry
{
public:
    /// A Post Process material; one of another domain runs nothing.
    auto get_material() const -> const asset_handle<material>&;
    void set_material(const asset_handle<material>& material);

    /// This entry's overrides of its material's parameters and textures.
    auto get_material_instance() const -> const material_instance&;
    auto get_material_instance() -> material_instance&;
    void set_material_instance(const material_instance& instance);

    /// The material the entry runs with: its material with the instance's overrides, or null.
    auto get_resolved_material() const -> material::sptr;

    /// Whether the entry runs.
    bool enabled = true;
    /// Scales the material's Opacity: 0 shows nothing, 1 the material as it is.
    float weight = 1.0f;

private:
    material_instance material_;
};

/**
 * @brief The post-process materials a volume (beside its other effect components) or a camera runs.
 *
 * A volume's entries blend with the volume: their weight is scaled by its contribution, and entries of one material
 * in several volumes blend their weights and their parameters by those contributions, so walking into a volume fades
 * its materials in and moves their values smoothly. A camera's entries apply after every volume's, for gameplay effects.
 * Each entry is one full-screen pass at its material's Location (post_material_pass.cpp).
 */
class post_process_material_component : public component_crtp<post_process_material_component>
{
public:
    std::vector<post_process_material_entry> entries;

    /// The entry of @p material, or null.
    auto find(const asset_handle<material>& material) -> post_process_material_entry*;
    auto find(const asset_handle<material>& material) const -> const post_process_material_entry*;
    /// The entry of @p material, added with @p weight when there is none.
    auto add(const asset_handle<material>& material, float weight = 1.0f) -> post_process_material_entry&;
    /// Removes the entry of @p material. @return True when there was one.
    auto remove(const asset_handle<material>& material) -> bool;
};

} // namespace unravel
