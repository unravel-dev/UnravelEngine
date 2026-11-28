#pragma once
#include <engine/engine_export.h>

#include "material.h"
#include "material_graph/graph_material.h"

#include <engine/assets/asset_handle.h>

#include <reflection/registration.h>
#include <serialization/serialization.h>

#include <cstdint>
#include <vector>

namespace unravel
{

/**
 * @brief Calls @p visit(id, data) for every property of a material type a material instance can override: each
 * writable reflected property, its base types' first, then its own. A read-only one (the cull type) is the graph's.
 */
template<typename Visitor>
void for_each_material_property(const entt::meta_type& type, Visitor&& visit)
{
    const auto visit_writable = [&visit](entt::id_type id, const entt::meta_data& data)
    {
        if(!data.is_const())
        {
            visit(id, data);
        }
    };
    for(auto [base_id, base] : type.base())
    {
        for(auto [id, data] : base.data())
        {
            visit_writable(id, data);
        }
    }
    for(auto [id, data] : type.data())
    {
        visit_writable(id, data);
    }
}

/**
 * @brief Reads one reflected property of a material (of its most derived type or a base type).
 * @return An owned copy of the value; empty when the material has no such property.
 */
auto get_material_value(const material& mat, entt::id_type property) -> entt::meta_any;

/**
 * @brief Writes one reflected property of a material through its setter.
 * @return False when the material has no such property or the value does not convert to its type.
 */
auto set_material_value(material& mat, entt::id_type property, const entt::meta_any& value) -> bool;

/**
 * @brief Gives @p target every value of @p source, in place: the reflected ones, then those reflection does not reach
 * (material::copy_unreflected_values, such as a graph material's graph and shader). @p target keeps its identity,
 * which caches of its look key on. Only values that differ are written. The two must be of the same type.
 */
void copy_material_values(const material& source, material& target);

/**
 * @brief The render_change aspects an edit of one reflected property of a material type changes: its
 * material_property_id entry (get_material_property_traits), or every material aspect for a property without one.
 */
auto get_material_property_changes(const entt::meta_type& type, entt::id_type property) -> render_change::mask;

/**
 * @class material_instance
 * @brief One renderer's overrides of the shared material of one model slot.
 *
 * A value the instance does not override follows the shared material: editing the shared material changes every
 * renderer that uses it, except the values an instance overrides. An instance without overrides draws exactly like
 * its shared material.
 *
 * Overrides are keyed by the reflected properties of the shared material's type. A property registered with the
 * "per_instance" attribute can differ between renderers that are still drawn together (see shares_draw). A graph
 * material's parameters and textures are overridden by name (set_graph_parameter, set_graph_texture).
 */
class material_instance
{
public:
    SERIALIZABLE(material_instance)

    /// One overridden property: the reflected property of the material type and the value it takes.
    struct property_override
    {
        entt::id_type property{};
        entt::meta_any value;
    };

    material_instance() = default;
    material_instance(const material_instance& other);
    material_instance(material_instance&& other) noexcept = default;
    auto operator=(const material_instance& other) -> material_instance&;
    auto operator=(material_instance&& other) noexcept -> material_instance& = default;
    ~material_instance() = default;

    /// Same shared material and the same overrides; the resolved material is a cache and does not count.
    auto operator==(const material_instance& other) const -> bool;

    /**
     * @brief Sets the shared material this instance overrides. The model keeps it in step with the slot.
     */
    void set_shared_material(const asset_handle<material>& shared);
    auto get_shared_material() const -> const asset_handle<material>&;

    /**
     * @brief Overrides a property of the shared material.
     * @param property The reflected property id of the material type (for example "base_color"_hs).
     * @param value The value; it must convert to the property's type.
     * @return False when the material type has no such property or the value does not convert to it.
     */
    auto set_override(entt::id_type property, const entt::meta_any& value) -> bool;

    /**
     * @brief The value the renderer draws with: the override, or the shared material's value.
     * @return An empty value when the shared material has no such property or is not loaded.
     */
    auto get_value(entt::id_type property) const -> entt::meta_any;

    auto is_overridden(entt::id_type property) const -> bool;

    /// Drops one override: the property follows the shared material again.
    void revert(entt::id_type property);

    /// Drops every override: the slot draws exactly like the shared material.
    void revert_all();

    auto has_overrides() const -> bool;
    auto get_overrides() const -> const std::vector<property_override>&;

    /**
     * @brief Overrides parameter @p name of a graph material (graph_material::set_parameter).
     * @return False when the shared material is not a loaded graph material with such a parameter.
     */
    auto set_graph_parameter(const std::string& name, const math::vec4& value) -> bool;

    /// What the renderer gives parameter @p name: the override, or the shared material's value. Empty when the shared
    /// material is not a loaded graph material with such a parameter.
    auto get_graph_parameter(hpp::string_view name) const -> hpp::optional<math::vec4>;

    auto is_graph_parameter_overridden(hpp::string_view name) const -> bool;

    /// Drops the override of parameter @p name: it follows the shared material again.
    void revert_graph_parameter(hpp::string_view name);

    /// The overridden graph parameters, in the order they were first overridden.
    auto get_graph_parameter_overrides() const -> const std::vector<graph_parameter_value>&;

    /**
     * @brief Overrides texture node @p name of a graph material (graph_material::set_texture); an empty handle samples
     * the default texture.
     * @return False when the shared material is not a loaded graph material with such a texture node.
     */
    auto set_graph_texture(const std::string& name, const asset_handle<gfx::texture>& texture) -> bool;

    /**
     * @brief Overrides texture node @p name without asking the shared material for it: for a caller that knows the node
     * from the material's generator and must not wait for the material to load, such as a scene load. An override of a
     * node the graph lacks changes nothing drawn.
     */
    void set_known_graph_texture(const std::string& name, const asset_handle<gfx::texture>& texture);

    /// What the renderer samples for texture node @p name: the override, or the shared material's texture.
    auto get_graph_texture(hpp::string_view name) const -> asset_handle<gfx::texture>;

    auto is_graph_texture_overridden(hpp::string_view name) const -> bool;

    /// Drops the override of texture node @p name: it follows the shared material again.
    void revert_graph_texture(hpp::string_view name);

    /// The overridden graph textures, in the order they were first overridden.
    auto get_graph_texture_overrides() const -> const std::vector<graph_texture_value>&;

    /**
     * @brief The render_change aspects this instance draws differently from @p previous: every material aspect when
     * the shared material differs (or a legacy copy waits), else the aspects of the properties whose override was
     * added, dropped or changed.
     */
    auto get_changes_from(const material_instance& previous) const -> render_change::mask;

    /**
     * @brief The overrides to save: the stored ones and, while a legacy copy waits, the properties where it
     * differs from the shared material (waiting for the shared material to load).
     */
    auto collect_overrides() const -> std::vector<property_override>;

    /**
     * @brief The reflected material type the overrides belong to (the shared material's type), or an invalid
     * type before the instance knows it.
     */
    auto get_material_type() const -> entt::meta_type;

    /**
     * @brief True when every override is a per-instance property, so the renderer can be drawn together with
     * the other renderers of the shared material. For a graph material: no texture is overridden, its World Position
     * Offset moves nothing (the vertex shader reads the material's own values), and every overridden parameter the
     * graph has sits in the registers the shader's instance_material variant reads per instance
     * (material::MAX_INSTANCE_CONSTANTS). A masked graph's shadow casters draw with the instance's resolved material.
     */
    auto shares_draw() const -> bool;

    /**
     * @brief Brings the resolved material up to date with the shared material and the overrides.
     *
     * Not thread safe: the model system calls it once a frame from the task of the model that owns the instance,
     * so every later reader of get_resolved() only reads.
     */
    void update_resolved();

    /**
     * @brief The shared material with the overrides applied.
     * @return Null when there are no overrides (draw with the shared material) or before update_resolved().
     */
    auto get_resolved() const -> const material::sptr&;

    /**
     * @brief shares_draw() as of the last update_resolved(): what the renderer batches by, read without reflection
     * lookups.
     */
    auto get_resolved_shares_draw() const -> bool;

    /**
     * @brief Whether the overrides feed nothing a shadow reads (render_interest::shadow_caster), as of the last
     * update_resolved(): the slot's shadow casts with the shared material, together with its other casters.
     */
    auto get_resolved_shares_shadow() const -> bool;

    /**
     * @brief Takes the resolved material of @p previous, the instance this copy replaces, when both are over the same
     * shared material. The next update_resolved() rewrites it in place, so caches of its look (the GI cards) keep
     * their key across an edit that sets a whole copy of the model.
     */
    void adopt_resolved(material_instance& previous);

    /**
     * @brief The graph overrides to save: the stored ones and, while a legacy copy waits, the parameters and textures
     * where it differs from the shared graph (waiting for the shared material to load).
     */
    void collect_graph_overrides(std::vector<graph_parameter_value>& parameters, std::vector<graph_texture_value>& textures) const;

    /**
     * @brief Takes a whole material saved by an older version, which kept a full copy instead of overrides. A PBR
     * material's copy becomes the Standard graph (upgrade_legacy_material).
     *
     * The values where it differs from the shared material become overrides once the shared material is loaded (a
     * graph's parameters and textures by name), so the look stays the same and the rest follows the shared material
     * from then on.
     */
    void set_legacy_copy(material::sptr copy);
    auto has_legacy_copy() const -> bool;

    /**
     * @brief Turns a legacy copy into overrides, waiting for the shared material when @p wait is true.
     * @return False while the shared material is still loading (nothing changed).
     */
    auto convert_legacy_copy(bool wait) -> bool;

private:
    auto find_override(entt::id_type property) -> property_override*;
    auto find_override(entt::id_type property) const -> const property_override*;
    /// The shared material as a loaded graph material, waiting for it to load when @p wait; null otherwise.
    auto get_shared_graph(bool wait) const -> std::shared_ptr<graph_material>;
    auto has_graph_overrides() const -> bool;
    auto shares_graph_draw() const -> bool;
    auto shares_shadow() const -> bool;
    /// Gives the resolved material the graph overrides.
    void apply_graph_overrides();
    auto diff_legacy_copy(const material::sptr& shared) const -> std::vector<property_override>;
    /// Adds to @p parameters and @p textures where the legacy copy's graph differs from the shared graph; nothing when
    /// either is not a graph material.
    void diff_legacy_graph_copy(const material::sptr& shared,
                                std::vector<graph_parameter_value>& parameters,
                                std::vector<graph_texture_value>& textures) const;
    void rebuild_resolved(const material::sptr& shared);
    void sync_changed_properties(const material::sptr& shared);
    /// Gives the resolved material the graph values changed since the last update_resolved(): each override's, else
    /// the shared material's back.
    void sync_changed_graph_values(const material::sptr& shared);

    /// The slot's shared material.
    asset_handle<material> shared_;
    /// The id of the reflected material type the overrides belong to.
    entt::id_type material_type_{};
    /// The overridden properties, in the order they were first overridden.
    std::vector<property_override> overrides_;
    /// A graph material's overridden parameters and textures, by name.
    std::vector<graph_parameter_value> graph_parameters_;
    std::vector<graph_texture_value> graph_textures_;
    /// A full material from an older save, waiting to be compared with the loaded shared material.
    material::sptr legacy_copy_;

    /// The shared material with the overrides applied (see update_resolved). Not copied with the instance.
    material::sptr resolved_;
    /// The shared material and its state the resolved material was last synced with.
    const material* resolved_shared_{};
    uint64_t resolved_shared_revision_{};
    bool resolved_shares_draw_{};
    bool resolved_shares_shadow_{};
    /// Properties overridden or reverted since the last update_resolved().
    std::vector<entt::id_type> changed_properties_;
    /// Graph parameters and textures overridden or reverted since the last update_resolved(), by name.
    std::vector<std::string> changed_graph_parameters_;
    std::vector<std::string> changed_graph_textures_;
    /// The graph overrides were replaced at once (a load, a copy, a revert of all): the resolved material is rebuilt.
    bool are_graph_overrides_changed_{};
};

/**
 * @brief An editable copy of what @p instance draws with: @p shared with the instance's overrides set on it.
 */
auto make_instance_values(const material_instance& instance, const material& shared) -> material::sptr;

/// The properties override_changed_values changed on an instance, and those the instance refused.
struct instance_value_changes
{
    std::vector<entt::id_type> overridden;
    std::vector<entt::id_type> refused;
};

/**
 * @brief Overrides on @p instance every property whose value in @p values differs from what the instance draws with
 * now: what an edit of a make_instance_values copy changed, setter side effects included. Property order.
 */
auto override_changed_values(material_instance& instance, const material& values) -> instance_value_changes;

} // namespace unravel
