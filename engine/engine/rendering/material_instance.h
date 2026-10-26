#pragma once
#include <engine/engine_export.h>

#include "material.h"

#include <engine/assets/asset_handle.h>

#include <reflection/registration.h>
#include <serialization/serialization.h>

#include <cstdint>
#include <vector>

namespace unravel
{

/// Calls @p visit(id, data) for every reflected property of a material type: its base types' first, then its own.
template<typename Visitor>
void for_each_material_property(const entt::meta_type& type, Visitor&& visit)
{
    for(auto [base_id, base] : type.base())
    {
        for(auto [id, data] : base.data())
        {
            visit(id, data);
        }
    }
    for(auto [id, data] : type.data())
    {
        visit(id, data);
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
 * @brief Gives @p target every reflected value of @p source, in place: @p target keeps its identity, which caches of
 * its look key on. Only values that differ are written. The two must be of the same type.
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
 * "per_instance" attribute can differ between renderers that are still drawn together (see shares_draw).
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
     * the other renderers of the shared material.
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
     * @brief Takes the resolved material of @p previous, the instance this copy replaces, when both are over the same
     * shared material. The next update_resolved() rewrites it in place, so caches of its look (the GI cards) keep
     * their key across an edit that sets a whole copy of the model.
     */
    void adopt_resolved(material_instance& previous);

    /**
     * @brief Takes a whole material saved by an older version, which kept a full copy instead of overrides.
     *
     * The properties where it differs from the shared material become overrides once the shared material is
     * loaded, so the look stays the same and the rest follows the shared material from then on.
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
    auto diff_legacy_copy(const material::sptr& shared) const -> std::vector<property_override>;
    void rebuild_resolved(const material::sptr& shared);
    void sync_changed_properties(const material::sptr& shared);

    /// The slot's shared material.
    asset_handle<material> shared_;
    /// The id of the reflected material type the overrides belong to.
    entt::id_type material_type_{};
    /// The overridden properties, in the order they were first overridden.
    std::vector<property_override> overrides_;
    /// A full material from an older save, waiting to be compared with the loaded shared material.
    material::sptr legacy_copy_;

    /// The shared material with the overrides applied (see update_resolved). Not copied with the instance.
    material::sptr resolved_;
    /// The shared material and its state the resolved material was last synced with.
    const material* resolved_shared_{};
    uint64_t resolved_shared_revision_{};
    bool resolved_shares_draw_{};
    /// Properties overridden or reverted since the last update_resolved().
    std::vector<entt::id_type> changed_properties_;
};

} // namespace unravel
