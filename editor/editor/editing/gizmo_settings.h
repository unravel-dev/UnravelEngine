#pragma once

#include <math/math.h>

#include <tuple>
#include <type_traits>

namespace unravel
{
class camera_component;
class light_component;
class reflection_probe_component;
class audio_source_component;
class particle_emitter_component;

/// The components whose objects draw an icon in the Scene view, and can be picked by it.
using billboard_components = std::tuple<camera_component,
                                        light_component,
                                        reflection_probe_component,
                                        audio_source_component,
                                        particle_emitter_component>;

/// The icons the Scene view draws for every object of a type in the scene.
struct billboard_gizmos
{
    float opacity = 0.75f;
    float size = 0.5f;
    bool depth_aware {false};
    bool show_camera {true};
    bool show_light {true};
    bool show_reflection_probe {true};
    bool show_audio_source {true};
    bool show_particle_emitter {true};

    auto operator==(const billboard_gizmos&) const -> bool = default;

    /// Whether the icons of the objects with component T are drawn and can be picked.
    template<typename T>
    auto is_shown() const -> bool
    {
        if constexpr(std::is_same_v<T, camera_component>)
        {
            return show_camera;
        }
        else if constexpr(std::is_same_v<T, light_component>)
        {
            return show_light;
        }
        else if constexpr(std::is_same_v<T, reflection_probe_component>)
        {
            return show_reflection_probe;
        }
        else if constexpr(std::is_same_v<T, audio_source_component>)
        {
            return show_audio_source;
        }
        else
        {
            static_assert(std::is_same_v<T, particle_emitter_component>, "This component has no icon.");
            return show_particle_emitter;
        }
    }

    /// Whether any type of object shows its icon.
    auto is_any_shown() const -> bool
    {
        return show_camera || show_light || show_reflection_probe || show_audio_source || show_particle_emitter;
    }

    /// Hides the icons of every type of object.
    void hide_all()
    {
        show_camera = false;
        show_light = false;
        show_reflection_probe = false;
        show_audio_source = false;
        show_particle_emitter = false;
    }
};

/// What the Scene view draws on the selected objects.
struct gizmos_data
{
    bool show_selection_outline {true};
    bool show_selection_wireframe {false};
    math::vec4 selection_wireframe_color {0.15f, 0.85f, 0.75f, 1.0f};
    float selection_wireframe_thickness {1.5f};
    bool show_camera {true};
    bool show_light {true};
    bool show_reflection_probe {true};
    bool show_volume {true};
    /// World bounds, colored by the current LOD.
    bool show_model_bounds {false};
    /// Bounds of every submesh. Drawn while show_model_bounds is on.
    bool show_model_submesh_bounds {false};
    bool show_text {true};
    bool show_particle_emitter {true};
    /// The gizmos components register themselves: physics shapes, character controllers, IK and spring bones. The
    /// physics world's debug lines go with them.
    bool show_component_gizmos {true};
    /// Drawn while show_particle_emitter is on, as are the two below.
    bool show_particle_emitter_bounds {true};
    bool show_particle_emitter_shape {true};
    bool show_particle_emitter_direction {true};

    auto operator==(const gizmos_data&) const -> bool = default;
};

/// Everything that decides which icons and gizmos the Scene view shows.
struct gizmo_setup
{
    /// The master switch over the other two.
    bool show_gizmos {true};
    billboard_gizmos billboards;
    gizmos_data selection;

    auto operator==(const gizmo_setup&) const -> bool = default;
};

/// Sets @p field of @p saved to its value in @p after when one edit changed it from @p before.
template<typename Owner, typename Field>
void carry_change(Owner& saved, const Owner& before, const Owner& after, Field Owner::*field)
{
    if(!(before.*field == after.*field))
    {
        saved.*field = after.*field;
    }
}

/**
 * @brief @p saved with what one edit changed between @p before and @p after, field by field. The project keeps the
 *        user's own choices: a change a tool made for a capture is in @p before and @p after alike, so it stays out.
 */
inline auto merge_gizmo_change(gizmo_setup saved, const gizmo_setup& before, const gizmo_setup& after) -> gizmo_setup
{
    carry_change(saved, before, after, &gizmo_setup::show_gizmos);
    carry_change(saved.billboards, before.billboards, after.billboards, &billboard_gizmos::opacity);
    carry_change(saved.billboards, before.billboards, after.billboards, &billboard_gizmos::size);
    carry_change(saved.billboards, before.billboards, after.billboards, &billboard_gizmos::depth_aware);
    carry_change(saved.billboards, before.billboards, after.billboards, &billboard_gizmos::show_camera);
    carry_change(saved.billboards, before.billboards, after.billboards, &billboard_gizmos::show_light);
    carry_change(saved.billboards, before.billboards, after.billboards, &billboard_gizmos::show_reflection_probe);
    carry_change(saved.billboards, before.billboards, after.billboards, &billboard_gizmos::show_audio_source);
    carry_change(saved.billboards, before.billboards, after.billboards, &billboard_gizmos::show_particle_emitter);
    carry_change(saved.selection, before.selection, after.selection, &gizmos_data::show_selection_outline);
    carry_change(saved.selection, before.selection, after.selection, &gizmos_data::show_selection_wireframe);
    carry_change(saved.selection, before.selection, after.selection, &gizmos_data::selection_wireframe_color);
    carry_change(saved.selection, before.selection, after.selection, &gizmos_data::selection_wireframe_thickness);
    carry_change(saved.selection, before.selection, after.selection, &gizmos_data::show_camera);
    carry_change(saved.selection, before.selection, after.selection, &gizmos_data::show_light);
    carry_change(saved.selection, before.selection, after.selection, &gizmos_data::show_reflection_probe);
    carry_change(saved.selection, before.selection, after.selection, &gizmos_data::show_volume);
    carry_change(saved.selection, before.selection, after.selection, &gizmos_data::show_model_bounds);
    carry_change(saved.selection, before.selection, after.selection, &gizmos_data::show_model_submesh_bounds);
    carry_change(saved.selection, before.selection, after.selection, &gizmos_data::show_text);
    carry_change(saved.selection, before.selection, after.selection, &gizmos_data::show_particle_emitter);
    carry_change(saved.selection, before.selection, after.selection, &gizmos_data::show_component_gizmos);
    carry_change(saved.selection, before.selection, after.selection, &gizmos_data::show_particle_emitter_bounds);
    carry_change(saved.selection, before.selection, after.selection, &gizmos_data::show_particle_emitter_shape);
    carry_change(saved.selection, before.selection, after.selection, &gizmos_data::show_particle_emitter_direction);
    return saved;
}

} // namespace unravel
