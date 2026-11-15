#pragma once
#include <engine/engine_export.h>

#include "gpu_program.h"
#include "render_change.h"
#include <engine/assets/asset_handle.h>

#include <graphics/format.h>
#include <graphics/graphics.h>
#include <graphics/texture.h>
#include <math/math.h>
#include <reflection/registration.h>
#include <serialization/serialization.h>


namespace unravel
{

/**
 * @enum cull_type
 * @brief Enum representing the type of culling to be used.
 */
enum class cull_type : std::uint32_t
{
    none,              ///< No culling.
    clockwise,         ///< Cull clockwise faces.
    counter_clockwise, ///< Cull counter-clockwise faces.
};

/// glTF-aligned surface alpha behavior (default: opaque): how an imported or a legacy PBR material covers its surface
/// (standard_surface), which the Standard graph draws.
enum class alpha_mode : std::uint32_t
{
    opaque, ///< No alpha clip.
    mask,   ///< Hard alpha cutoff, its shadow cut out too.
    blend,  ///< Transparent: dithered out by its alpha.
};

/**
 * @brief The material properties the renderer reads. Each has one entry in get_material_property_traits: what an
 * edit of it changes and whether a material instance overrides it per instance. The setters count their edits from
 * that entry and the reflection takes its "per_instance" attribute from it, so the two cannot disagree.
 */
enum class material_property_id : std::uint8_t
{
    cull_type,
    /// A graph material's graph: it can change every aspect (graph_material).
    graph,
    /// A value given to a graph material's parameter over the graph's default (graph_material::set_parameter): it can
    /// feed any aspect, and a material instance can override it without a separate draw.
    graph_parameter,
    /// A texture given to a graph material's texture node over the graph's (graph_material::set_texture).
    graph_texture,
};

/// What an edit of one material_property_id changes, and how the property is stored.
struct material_property_traits
{
    /// The render_change aspects an edit of the property changes.
    render_change::mask changes = render_change::none;
    /// A material instance can override it without a separate draw (material_instance::shares_draw).
    bool is_per_instance = false;
};

/// The one table of what each material property changes (see material_property_id).
constexpr auto get_material_property_traits(material_property_id property) -> material_property_traits
{
    switch(property)
    {
        case material_property_id::cull_type:
            return {render_change::sidedness, false};
        case material_property_id::graph:
        case material_property_id::graph_texture:
            return {render_change::material_aspects, false};
        case material_property_id::graph_parameter:
            return {render_change::material_aspects, true};
    }
    return {};
}

/**
 * @class material
 * @brief Base class for materials used in rendering.
 */
class material : public crtp_meta_type<material>
{
public:
    SERIALIZABLE(material)

    using sptr = std::shared_ptr<material>;
    using wptr = std::weak_ptr<material>;
    using uptr = std::unique_ptr<material>;

    material() = default;
    virtual ~material() = default;

    /// Counts the edits made through the setters that changed any of @p changes (render_change aspects): a cache of
    /// what the material looked like keeps it for the aspects it reads (render_interest) and compares.
    auto get_revision(render_change::mask changes = render_change::material_aspects) const -> uint64_t
    {
        return state_revisions_.get(changes) + instance_values_revisions_.get(changes);
    }

    /// The same for values that are not per instance (textures, modes, tiling): they change how the material draws,
    /// so a cache of its look follows them at once.
    auto get_state_revision(render_change::mask changes = render_change::material_aspects) const -> uint64_t
    {
        return state_revisions_.get(changes);
    }

    /// The same for per-instance values (colors and numbers a material instance can override without a separate
    /// draw). A script may change them every frame, so a costly cache may follow them less often.
    auto get_instance_values_revision(render_change::mask changes = render_change::material_aspects) const
        -> uint64_t
    {
        return instance_values_revisions_.get(changes);
    }

    /**
     * @brief The identity of the content of the textures whose properties change any of @p changes
     * (asset_handle::version): a texture that finishes loading or is reloaded changes it without an edit.
     */
    auto get_content_version(render_change::mask changes = render_change::material_aspects) const -> uint64_t;

    /**
     * @brief Adds what the material looks like to @p keys, per render_change aspect: its edits (get_revision of each
     * aspect) and the content of its textures (get_content_version). A cache compares the keys of the aspects it reads
     * with the ones it was built with.
     */
    void add_change_keys(render_change_values& keys) const;

    /**
     * @brief Gets the culling type of the material.
     * @return The culling type.
     */
    auto get_cull_type() const -> cull_type;

    /**
     * @brief Sets the culling type of the material.
     * @param val The culling type to set.
     */
    void set_cull_type(cull_type val);

    /**
     * @brief Gets the render states for the material.
     * @param apply_cull Whether to apply culling.
     * @param depth_write Whether to write to the depth buffer.
     * @param depth_test Whether to perform depth testing.
     * @return The render states as a 64-bit integer.
     */
    virtual auto get_render_states(bool apply_cull = true, bool depth_write = true, bool depth_test = true) const
        -> uint64_t;

    /**
     * @brief Gets the default color map.
     * @return A reference to the default color map asset handle.
     */
    static auto default_color_map() -> asset_handle<gfx::texture>&;

    /**
     * @brief Gets the default normal map.
     * @return A reference to the default normal map asset handle.
     */
    static auto default_normal_map() -> asset_handle<gfx::texture>&;

    /**
     * @brief Clones a materiial
     * @return A shared_pointer to the new material
     */
    virtual auto clone() const -> material::sptr;

    /**
     * @brief Gives this material the values of @p source, a material of the same type, that reflection does not reach:
     * copy_material_values calls it after the reflected ones. Only values that differ are written, through the
     * setters, so the revisions count what changed. None here.
     */
    virtual void copy_unreflected_values(const material& source);

    /// The most constants a material's instanced draw reads per instance (instance_material_vertex_data).
    static constexpr uint32_t MAX_INSTANCE_CONSTANTS = 4;

    /**
     * @brief Writes the material's per-instance values (see material_instance::shares_draw) as the constants its
     * instanced shader reads per instance, so renderers whose values differ still share one instanced draw.
     * @param out At least MAX_INSTANCE_CONSTANTS constants.
     * @return How many constants were written; 0 for a material without per-instance values.
     */
    virtual auto write_instance_constants(math::vec4* out) const -> uint32_t;

protected:
    /// Adds the content identity of each texture to the keys of the aspects its property changes; none here.
    virtual void add_content_versions(render_change_values& keys) const;

    /// Every setter calls this after an edit that changed the value: it counts the edit for the property's aspects
    /// (get_material_property_traits), with the per-instance or the state values.
    void mark_changed(material_property_id property)
    {
        mark_changed(property, get_material_property_traits(property).changes);
    }

    /// The same for an edit that changed only @p changes of the property's aspects.
    void mark_changed(material_property_id property, render_change::mask changes)
    {
        auto& revisions = get_material_property_traits(property).is_per_instance ? instance_values_revisions_
                                                                                  : state_revisions_;
        revisions.bump(changes);
    }

    ///< The culling type for this material.
    cull_type cull_type_ = cull_type::counter_clockwise;

private:
    render_change_revisions state_revisions_;
    render_change_revisions instance_values_revisions_;
};

} // namespace unravel
