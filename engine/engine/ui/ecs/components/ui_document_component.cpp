#include "ui_document_component.h"

#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>

namespace unravel
{

// The ui_document_component is now primarily a data container.
// All loading/unloading logic is handled by the ui_system in its update loop.

auto ui_document_component::get_world_space_scale() const -> math::vec2
{
    return {static_cast<float>(size.width) / pixels_per_world_unit, static_cast<float>(size.height) / pixels_per_world_unit};
}


auto ui_document_component::get_world_space_quad(const math::transform& entity_world) const -> math::transform
{
    return entity_world * math::transform::scaling(get_world_space_scale());
}

auto ui_document_component::get_document_to_world(const math::transform& entity_world) const -> math::mat4
{
    // Document pixel (0, 0) is the quad's local (-0.5, 0.5) and y runs down the quad. The document reads
    // from local -z, so a CSS transform that lifts an element toward the viewer moves it along -z, one
    // world unit per pixels_per_world_unit pixels.
    math::mat4 document_to_local(1.0f);
    document_to_local[0][0] = 1.0f / static_cast<float>(size.width);
    document_to_local[1][1] = -1.0f / static_cast<float>(size.height);
    document_to_local[2][2] = -1.0f / pixels_per_world_unit;
    document_to_local[3] = math::vec4(-0.5f, 0.5f, 0.0f, 1.0f);
    return get_world_space_quad(entity_world).get_matrix() * document_to_local;
}

auto ui_document_component::draws_into_scene() const -> bool
{
    return render_mode == ui_render_mode::world_space && !needs_render_target;
}

auto ui_document_component::get_bounds() const -> math::bbox
{
    const auto scale = get_world_space_scale();
    math::bbox bbox;
    bbox.min = math::vec3(-scale.x * 0.5f, -scale.y * 0.5f, 0.0f);
    bbox.max = math::vec3(scale.x * 0.5f, scale.y * 0.5f, 0.001f);
    return bbox;
}
auto ui_document_component::is_loaded() const -> bool
{
    return document != nullptr;
}

auto ui_document_component::is_enabled() const -> bool
{
    return enabled_;
}

void ui_document_component::set_enabled(bool enabled)
{
    enabled_ = enabled;
}

void get_documents_by_sort_order(entt::registry& registry, std::vector<entt::entity>& documents)
{
    documents.clear();
    const auto view = registry.view<ui_document_component>();
    view.each(
        [&](entt::entity entity, const ui_document_component& document)
        {
            // After the listed documents of the same order, which keeps the registry's order; unlike
            // std::stable_sort, inserting in place needs no buffer.
            const auto place = std::upper_bound(documents.begin(),
                                                documents.end(),
                                                document.sort_order,
                                                [&](int order, entt::entity listed)
                                                {
                                                    return order < view.get<ui_document_component>(listed).sort_order;
                                                });
            documents.insert(place, entity);
        });
}

} // namespace unravel
