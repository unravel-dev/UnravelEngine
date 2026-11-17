#pragma once
#include <engine/engine_export.h>

#include <engine/ecs/components/basic_component.h>
#include <engine/layers/layer_mask.h>
#include <engine/rendering/material_instance.h>
#include <math/math.h>

namespace unravel
{

/**
 * @brief A decal: a box that lays a Decal-domain material onto the surfaces inside it.
 *
 * The box is centred on the entity: Size wide (local X) and high (local Y), Depth deep along local Z, the direction it
 * projects in (the gizmo's arrow). The entity's scale stretches it. Decals draw after the G-buffer and before any
 * lighting reads it (decal_pass.cpp), so lighting, shadows, reflections and AO treat them as the surface; a
 * renderer whose Receive Decals is off is drawn after them and keeps none.
 *
 * Per-decal values go through the material instance (a tint, an atlas frame): its first four registers keep decals of
 * one material in one instanced draw. In play, a Lifetime above 0 fades the decal out after it, and Remove After Fade
 * destroys the entity then (Decals.Spawn sets both).
 */
class decal_component : public component_crtp<decal_component>
{
public:
    /// The Decal-domain material the box projects. A material of another domain draws nothing.
    auto get_material() const -> const asset_handle<material>&;
    void set_material(const asset_handle<material>& material);

    /// This decal's overrides of its material: a value it does not override follows the material.
    auto get_material_instance() const -> const material_instance&;
    auto get_material_instance() -> material_instance&;
    void set_material_instance(const material_instance& instance);

    /// The material the decal draws with: its material with the instance's overrides, or null.
    auto get_resolved_material() const -> material::sptr;

    /// Sets Size to @p face (at least a millimetre a side) and Depth to half its smaller side, at least 5 cm: deep
    /// enough for uneven ground, shallow enough not to reach through a thin wall. Every way of placing a decal sizes it
    /// through this.
    void resize(const math::vec2& face);

    /// Width (X) and height (Y) of the projected face, in world units before the entity's scale.
    math::vec2 size{1.0f, 1.0f};
    /// How deep the box reaches along the projection (Z), in world units before the entity's scale.
    float depth = 1.0f;
    /// Scales the material's Opacity: what scripts animate.
    float opacity = 1.0f;
    /// Higher draws later, over lower; equal orders draw by material, then by entity.
    int sort_order = 0;
    /// The layers of the models the decal lays onto: a model whose entity is on none of them keeps none of it.
    layer_mask receiver_layers{layer_reserved::everything_layer};
    /// Degrees between a surface's normal and the direction back toward the projector where the decal starts to fade
    /// (x) and is gone (y): surfaces turned further away receive nothing.
    math::vec2 angle_fade{60.0f, 85.0f};
    /// Below this share of the view's height the decal fades out and is not drawn.
    float fade_screen_size = 0.004f;
    /// Beyond this distance from the camera the decal is not drawn; 0: no limit.
    float max_distance = 0.0f;
    /// Play: seconds the decal lasts after Fade In before it fades out; 0: it stays.
    float lifetime = 0.0f;
    /// Play: seconds to appear, and to disappear once its Lifetime is over.
    float fade_in = 0.0f;
    float fade_out = 1.0f;
    /// Play: the entity is destroyed when the decal has faded out.
    bool remove_after_fade = false;

    /// The lifetime fade once the decal ages (in play, or made by Decals.Spawn); 1 before, so edit mode shows a decal
    /// with a Fade In whole.
    auto get_play_fade() const -> float;

    /// Play: seconds since the decal appeared (decal_system); not saved.
    float age = 0.0f;
    /// Play: made by Decals.Spawn, and counted against max_transient_decals; not saved.
    bool is_transient = false;

    /// The most transient decals at once unless a script changes it; play end restores it.
    static constexpr uint32_t DEFAULT_MAX_TRANSIENT_DECALS = 256;
    /// The most transient decals at once (Decals.maxTransientDecals): past it the oldest start to fade out.
    static inline uint32_t max_transient_decals = DEFAULT_MAX_TRANSIENT_DECALS;
    /// Seconds an over-budget transient decal takes to fade out.
    static constexpr float TRANSIENT_OVERFLOW_FADE_OUT = 0.5f;

    /// The box in the entity's space: centred, Size by Depth.
    auto get_local_bounds() const -> math::bbox;
    /// From the unit box (-0.5 to 0.5) to the world, through the entity's @p world transform.
    auto get_box_to_world(const math::transform& world) const -> math::mat4;
    /// The fade of its Fade In, Lifetime and Fade Out at its age, 0 to 1; 1 while it does not age.
    auto get_lifetime_fade() const -> float;
    /// Whether the decal has aged past its Lifetime and Fade Out.
    auto has_faded_out() const -> bool;
    /// The Lifetime as the Decal Data node reads it: its age over Fade In, Lifetime and Fade Out (0 while it stays).
    auto get_normalized_age() const -> float;

private:
    material_instance material_;
};

} // namespace unravel
