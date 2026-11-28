#include "dynamic_renderer.h"

#include <engine/rendering/material.h>
#include <engine/rendering/material_graph/graph_material.h>

#include <graphics/graphics.h>

namespace unravel
{

namespace
{
/// The Surface graph material @p drawn is; null for a material of another kind or domain.
auto get_renderer_surface(const std::shared_ptr<material>& drawn) -> const graph_material*
{
    const auto* graph = drawn ? drawn->safe_cast<graph_material>() : nullptr;
    return graph != nullptr && graph->get_domain() == material_domain::surface ? graph : nullptr;
}
} // namespace

auto dynamic_renderer::default_strip_material() -> asset_handle<material>&
{
    static asset_handle<material> strip;
    return strip;
}

auto dynamic_renderer::get_material() const -> const asset_handle<material>&
{
    return material_.get_shared_material();
}

void dynamic_renderer::set_material(const asset_handle<material>& value)
{
    if(material_.get_shared_material() == value)
    {
        return;
    }
    material_.set_shared_material(value);
    material_.update_resolved();
    refreshed_frame_ = std::numeric_limits<uint32_t>::max();
}

auto dynamic_renderer::get_material_instance() const -> const material_instance&
{
    return material_;
}

void dynamic_renderer::set_material_instance(const material_instance& instance)
{
    material_instance updated = instance;
    // The instance's own shared material is the renderer's: the overrides follow the material picked.
    updated.set_shared_material(material_.get_shared_material());
    // The material drawn keeps its place and is rewritten there, so what is keyed on it holds across an edit.
    updated.adopt_resolved(material_);
    material_ = std::move(updated);
    material_.update_resolved();
    refreshed_frame_ = std::numeric_limits<uint32_t>::max();
}

void dynamic_renderer::refresh_material(const asset_handle<material>& fallback)
{
    const uint32_t frame = gfx::get_render_frame();
    if(refreshed_frame_ == frame)
    {
        return;
    }
    refreshed_frame_ = frame;
    material_.update_resolved();
    auto drawn = find_drawn_material();
    const auto* graph = get_renderer_surface(drawn);
    if(graph == nullptr)
    {
        // No material, one still loading, or one that is no surface: the engine's own for the component.
        drawn = fallback.try_get();
        graph = get_renderer_surface(drawn);
    }
    const uint64_t revision = graph != nullptr ? graph->get_revision() : 0;
    if(graph != drawn_graph_ || revision != drawn_revision_)
    {
        drawn_hash_ = graph != nullptr ? graph->get_draw_hash() : 0;
        drawn_revision_ = revision;
    }
    drawn_material_ = graph != nullptr ? std::move(drawn) : nullptr;
    drawn_graph_ = graph;
}

auto dynamic_renderer::get_drawn_graph() const -> const graph_material*
{
    return drawn_graph_;
}

auto dynamic_renderer::get_drawn_material() const -> const std::shared_ptr<material>&
{
    return drawn_material_;
}

auto dynamic_renderer::get_draw_hash() const -> uint64_t
{
    return drawn_hash_;
}

auto dynamic_renderer::find_drawn_material() const -> std::shared_ptr<material>
{
    if(const auto& resolved = material_.get_resolved())
    {
        return resolved;
    }
    auto loaded = material_.get_shared_material().try_get();
    return loaded && !asset_handle<material>::is_empty(loaded) ? loaded : nullptr;
}

} // namespace unravel
