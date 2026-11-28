#pragma once
#include <engine/engine_export.h>

#include <engine/assets/asset_handle.h>
#include <engine/rendering/material_instance.h>

#include <limits>

#include <cstdint>
#include <memory>

namespace unravel
{
class graph_material;

/**
 * @brief What the components that draw geometry built on the CPU (Line, Trail) share: a Surface material
 * with the component's own overrides of it, whether it casts a shadow, and a place in the translucency pass's
 * order.
 */
class dynamic_renderer
{
public:
    /// The Surface material the geometry draws with. Unset, or with a material of another domain, it draws the
    /// engine's default for the component.
    auto get_material() const -> const asset_handle<material>&;
    /// Picks the material. The overrides stay, and apply to the ones the new material has by the same names.
    void set_material(const asset_handle<material>& value);

    /// This renderer's overrides of its material: a value it does not override follows the material.
    auto get_material_instance() const -> const material_instance&;
    /// Takes the overrides of @p instance. The material stays the renderer's own: the instance's is not read.
    void set_material_instance(const material_instance& instance);

    /**
     * @brief Brings what the geometry draws with up to date: the material with the overrides applied, or @p fallback
     * while the renderer has none that is a Surface material (unset, still loading, of another domain). Never waits
     * for a load. The first call of a render frame does the work; a run calls it before it reads what is drawn.
     */
    void refresh_material(const asset_handle<material>& fallback);
    /// The Surface graph the geometry draws with, as refresh_material left it; null while none can draw.
    auto get_drawn_graph() const -> const graph_material*;
    /// The material of get_drawn_graph, kept alive by the renderer.
    auto get_drawn_material() const -> const std::shared_ptr<material>&;
    /// A key of what the drawn material binds (graph_material::get_draw_hash): renderers with the same key whose
    /// materials bind the same draw share one.
    auto get_draw_hash() const -> uint64_t;

    /// The geometry casts shadows, when its material casts them (graph_material::casts_shadow).
    bool casts_shadow = true;

    /// Where a see-through material draws among the other see-through renderers: lower first, then far to near.
    int32_t sort_priority = 0;

    /// The material a new Line or Trail draws with (STRIP_MATERIAL_KEY).
    static auto default_strip_material() -> asset_handle<material>&;

private:
    /// The material with the overrides applied; null without one or while it loads. Never waits.
    auto find_drawn_material() const -> std::shared_ptr<material>;

    material_instance material_;
    /// What refresh_material found, the revision of the material its hash was made at, and the render frame it ran
    /// in.
    std::shared_ptr<material> drawn_material_;
    const graph_material* drawn_graph_ = nullptr;
    uint64_t drawn_revision_ = 0;
    uint64_t drawn_hash_ = 0;
    uint32_t refreshed_frame_ = std::numeric_limits<uint32_t>::max();
};

} // namespace unravel
