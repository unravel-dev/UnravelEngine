#pragma once
#include "entt/entity/fwd.hpp"
#include <engine/assets/asset_handle.h>
#include <engine/ecs/scene.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

#include <string_view>

namespace unravel
{

    
template<typename Entity>
struct entity_components
{
    Entity entity;
};

template<typename Entity>
struct entity_data
{
    entity_components<Entity> components;
};
enum class clone_mode_t
{
    none,
    cloning_object,
    cloning_prefab_instance,
    updating_prefab,
};
struct save_context
{
    auto get_clone_mode() const -> clone_mode_t
    {
        return clone_mode;
    }

    auto is_cloning() const -> bool
    {
        return clone_mode != clone_mode_t::none;
    }

    auto is_saving_to_prefab() const -> bool
    {
        return to_prefab;
    }
    clone_mode_t clone_mode{};
    bool to_prefab{};
    entt::const_handle save_source{};

    /**
     * @brief Marks the document as one whose nested prefab instances are already current.
     *
     * Set by the deploy bake, which resolves every nested instance against its own asset
     * before writing. A loader seeing the marker can skip that work, because it has been
     * done - see load_context::nesting_resolved.
     *
     * Deliberately a property of the *document*, not of the running mode: keying it on
     * "is this a deployed build" would give two paths whose results differ only when the
     * bake is stale, which is the failure that never shows up until it ships.
     */
    bool nesting_resolved{};
};

struct load_context
{    
    auto get_clone_mode() const -> clone_mode_t
    {
        return clone_mode;
    }

    auto is_cloning() const -> bool
    {
        return clone_mode != clone_mode_t::none;
    }

    /// Whether records are currently being applied over an existing prefab instance,
    /// i.e. matched by prefab uid rather than creating fresh entities.
    auto is_updating_prefab() const -> bool
    {
        const auto* frame = current_instance();
        return frame != nullptr && !frame->mapping_by_prefab_uid.empty();
    }

    std::vector<entity_data<entt::handle>> entities;
    clone_mode_t clone_mode{};
    entt::registry* reg{};

    /// True when the document being read declared its nested instances already resolved.
    /// Read before the context is popped; see load_from_prefab.
    bool nesting_resolved{};

    /// Set only while an entity *record*'s header is being resolved, as opposed to an
    /// entity link. Both go through the same resolver, but only a record means "the asset
    /// still contains this entity" - a link merely refers to one, and a parent's children
    /// list refers to every child it has.
    bool resolving_record{};

    /**
     * @brief The nested instance whose contents are currently being read, if any.
     *
     * A containing document stores a full snapshot of what it nests, but only the part it
     * *overrides* is its own authoring - the rest is the nested asset's, and arrives from
     * there. Replaying the whole snapshot over a live instance would revert every edit made
     * to it here, so the instance's override set decides what is allowed through.
     *
     * `pending` is set while a record's header resolves and promoted to `current` for the
     * duration of its components, since only the header knows which instance it belongs to.
     */
    entt::handle pending_nested_owner{};
    entt::handle current_nested_owner{};


    // The ids are not globally unique, so we need to map them to the handles
    std::map<entt::entity, entt::handle> mapping_by_eid;

    // The entity uids (from id_component) are globally unique, so we can use them to map entities
    std::map<hpp::uuid, entt::handle> mapping_by_uid;

    struct prefab_uid_mapping_t
    {
        entt::handle handle;

        /// How many records addressed this prefab uid. A count rather than a flag because
        /// two instances of the same prefab under one root share uids: knowing *how many*
        /// the asset still mentions is what lets the extra ones be identified.
        size_t consumed_count{};

        auto consumed() const -> bool
        {
            return consumed_count > 0;
        }

        /// Belongs to a nested instance. Records addressed at it resolve to an empty handle
        /// so the outer asset's snapshot cannot overwrite it, but the handle is still kept
        /// so cleanup can remove it if the asset has dropped that instance entirely.
        bool shadowed{};

        /// How many live entities carry this uid. More than one whenever a nested instance
        /// was duplicated - a copy keeps the original's prefab uid, so the uid alone can no
        /// longer say how much there is to protect.
        size_t shadow_count{};

        /// How many records found every live instance already claimed and were let through to
        /// create one. Distinguishes "the document has more of these than exist here" from
        /// "the document said nothing more about them", which links have to tell apart.
        size_t surplus_records{};

        /// How many of those a record has claimed so far. Once it reaches shadow_count the
        /// asset is carrying more of these than exist here, so the surplus has nothing to
        /// protect and is loaded normally - which is what creates it.
        size_t shadow_cursor{};
    };

    /**
     * @brief Per-instance state for a prefab being loaded over.
     *
     * Prefab uids are unique only *within* one prefab asset, so two instances of the same
     * asset in a single scene carry identical uids. A flat map cannot hold both - the
     * second instance's entities would resolve to the first instance's handles. Scoping
     * the map to the instance being loaded is what makes the identity well-defined.
     *
     * Nothing pushes more than one frame today; nested prefabs are what will
     * (tasks/nested_prefabs_design.md). The scoping is separated out first because it is
     * the part that has to be correct before anything can nest.
     */
    struct instance_frame
    {
        std::map<hpp::uuid, prefab_uid_mapping_t> mapping_by_prefab_uid;

        /**
         * @brief One live nested instance, addressable by the document that contains it.
         *
         * Its contents are keyed by prefab uid, which is unique inside the nested asset -
         * ambiguous across the whole load, but not within one instance. Scoping is what
         * makes a containing document able to say "this property, of this entity, of *this*
         * instance" and have it land.
         */
        struct nested_scope
        {
            entt::handle root;

            /// Entities of this instance. A null handle is one the instance removed, kept so
            /// the document's record for it is skipped rather than resurrecting it.
            std::map<hpp::uuid, entt::handle> by_prefab_uid;

            /// The document addressed this instance, so it still contains it.
            bool consumed{};
        };

        /// Keyed by the chain of instance ids leading to it. A chain, not one id, because at
        /// depth the id repeats: a prefab holding two instances of B holds two instances of
        /// whatever B nests, and those carry the same id.
        std::map<std::vector<hpp::uuid>, nested_scope> nested_scopes;

        /**
         * @brief A live nested instance with no id, and where it sits.
         *
         * The two sides of a nesting can disagree about ids: a document re-saved by a build
         * that has them, loaded over instances created before it, or the reverse. An id the
         * document knows and the instance does not means the same slot, unlabelled - so the
         * instance is adopted and labelled rather than duplicated, which is what matching by
         * id alone would do.
         */
        struct adoption_candidate
        {
            std::vector<hpp::uuid> parent_path;
            hpp::uuid prefab_uid;
            entt::handle handle;
        };
        std::vector<adoption_candidate> adoption_candidates;

        /**
         * @brief Whether this instance already carries nested instances the document can name.
         *
         * Decides whether an unnamed one may be adopted. Both a nested instance created before
         * instance ids existed and one the user cloned here have no id, and adoption exists for
         * the first - so it must not run when the second is what it would find.
         *
         * If anything here is named, this instance was produced by a document that names its
         * instances, and an unnamed one alongside them was added here.
         */
        bool has_named_instances{};

        /// Entities the containing document added inside this instance. Kept out of the uid
        /// mapping so the instance's own asset neither matches nor removes them.
        std::set<hpp::uuid> foreign_entities;

        /// Nested instances deleted here, by the same chain. There is no scope for one - it
        /// is gone - so without this the document's record for it simply creates it again,
        /// which is what a deletion looks like from the document's side.
        std::set<std::vector<hpp::uuid>> removed_instance_paths;

        /// The instance being loaded over, when there is one.
        entt::handle root;

        /// Nested instance roots that were live before the load, as (prefab uid, handle)
        /// pairs rather than a set: two instances of the same prefab under one root carry
        /// identical prefab uids, so keying by uid alone would lose one of them.
        std::vector<std::pair<hpp::uuid, entt::handle>> shadowed_roots;
    };

    /// Innermost instance being loaded over, or nullptr when entities are being created
    /// rather than matched - a plain scene load, or a fresh instantiate.
    auto current_instance() -> instance_frame*
    {
        return instance_stack.empty() ? nullptr : &instance_stack.back();
    }
    auto current_instance() const -> const instance_frame*
    {
        return instance_stack.empty() ? nullptr : &instance_stack.back();
    }

    std::vector<instance_frame> instance_stack;
};

/**
 * @brief Scopes one prefab instance's uid mapping for the duration of a load.
 *
 * Requires an active load context. Nested-safe.
 */
struct scoped_instance_frame
{
    scoped_instance_frame();
    ~scoped_instance_frame();

    scoped_instance_frame(const scoped_instance_frame&) = delete;
    scoped_instance_frame(scoped_instance_frame&&) = delete;
    auto operator=(const scoped_instance_frame&) -> scoped_instance_frame& = delete;
    auto operator=(scoped_instance_frame&&) -> scoped_instance_frame& = delete;
};

struct post_load_callbacks
{
    std::function<void(hpp::span<const entt::handle> new_entities)> callback;
};
void push_on_load_callbacks(const post_load_callbacks& callbacks);
void pop_on_load_callbacks();
auto get_post_load_callbacks() -> const post_load_callbacks*;

auto push_load_context(entt::registry& registry) -> bool;
void pop_load_context(bool push_result);
auto get_load_context() -> load_context&;

auto push_save_context() -> bool;
void pop_save_context(bool push_result);
auto get_save_context() -> save_context&;

template<typename T>
concept HasCharAndTraits = requires {
    typename T::char_type;
    typename T::traits_type;
};
template<typename T>
concept HasView = HasCharAndTraits<T> && requires(const T& t) {
    {
        t.view()
    } noexcept -> std::same_as<std::basic_string_view<typename T::char_type, typename T::traits_type>>;
};

void save_to_stream(std::ostream& stream, entt::const_handle obj);
void save_to_file(const std::string& absolute_path, entt::const_handle obj);
void save_to_stream_bin(std::ostream& stream, entt::const_handle obj);
void save_to_file_bin(const std::string& absolute_path, entt::const_handle obj);

// Non-template overloads: asset_writer::atomic_save_to_file is a template that
// calls unqualified save_to_file. Clang two-phase lookup + ADL only searches
// namespace entt for handle arguments, so the template cannot see these
// unravel overloads. Prefer these overloads over the template for handles.
namespace asset_writer
{
auto atomic_save_to_file(const fs::path& key, entt::const_handle obj) -> bool;
auto atomic_save_to_file(const fs::path& key, entt::handle obj) -> bool;
} // namespace asset_writer

void load_from_view(std::string_view view, entt::registry& obj);
void load_from_stream(std::istream& stream, entt::registry& obj);

void load_from_view(std::string_view view, entt::handle& obj);
void load_from_stream(std::istream& stream, entt::handle& obj);
void load_from_file(const std::string& absolute_path, entt::handle& obj);
void load_from_stream_bin(std::istream& stream, entt::handle& obj);
void load_from_file_bin(const std::string& absolute_path, entt::handle& obj);

auto load_from_prefab_out(const asset_handle<prefab>& pfb,
                          entt::registry& registry,
                          entt::handle& obj) -> bool;

/**
 * @brief Reloads an instance from its prefab asset, keeping what belongs to the instance.
 *
 * Its local position and rotation are preserved unconditionally - those are per-placement
 * and treated as implicit overrides - as is its parent. Everything else follows the asset
 * except properties recorded in the instance's `property_overrides`.
 *
 * Scale and skew deliberately follow the asset unless explicitly overridden; see
 * tasks/lessons.md before "fixing" that asymmetry.
 *
 * @return false if the handle is not an instance, has no source asset, or the expansion was
 *         refused because the asset is already being expanded further up (a nesting cycle).
 */
auto sync_prefab_instance(entt::handle instance) -> bool;

/**
 * @brief Syncs every prefab instance nested inside a subtree against its own asset.
 *
 * Does not descend past an instance root - each instance's own sync handles what is nested
 * inside it, so the recursion happens through sync_prefab_instance rather than here.
 */
auto sync_nested_prefab_instances(entt::handle root) -> size_t;

/**
 * @brief Syncs every prefab instance in a registry against its own asset.
 *
 * Unlike sync_nested_prefab_instances this includes instances that are themselves scene
 * roots, which is the difference between a scene and a single-rooted prefab.
 *
 * Each instance's sync cascades into whatever is nested inside it, so this walks only the
 * top level.
 */
auto sync_all_prefab_instances(entt::registry& registry) -> size_t;

auto load_from_prefab(const asset_handle<prefab>& pfb, entt::registry& registry) -> entt::handle;
auto load_from_prefab_bin(const asset_handle<prefab>& pfb, entt::registry& registry) -> entt::handle;

void clone_entity_from_stream(entt::const_handle src_obj, entt::handle& dst_obj);

void save_to_stream(std::ostream& stream, const scene& scn);
void save_to_file(const std::string& absolute_path, const scene& scn);
void save_to_stream_bin(std::ostream& stream, const scene& scn);
void save_to_file_bin(const std::string& absolute_path, const scene& scn);

void load_from_view(std::string_view view, scene& scn);
void load_from_stream(std::istream& stream, scene& scn);

void load_from_file(const std::string& absolute_path, scene& scn);
void load_from_stream_bin(std::istream& stream, scene& scn);
void load_from_file_bin(const std::string& absolute_path, scene& scn);

auto load_from_prefab(const asset_handle<scene_prefab>& pfb, scene& scn) -> bool;
auto load_from_prefab_bin(const asset_handle<scene_prefab>& pfb, scene& scn) -> bool;

void clone_scene_from_stream(const scene& src_scene, scene& dst_scene);

template<typename Stream, typename T>
void load_from(Stream& stream, T& scn)
{
    stream.seekg(0);

    if constexpr(HasView<Stream>)
    {
        load_from_view(stream.view(), scn);
    }
    else
    {
        load_from_stream(stream, scn);
    }
}

} // namespace unravel

namespace ser20
{

SAVE_EXTERN(entt::const_handle);
LOAD_EXTERN(entt::handle);

template<typename T>
struct basic_handle_link
{
    T handle{};
};
struct const_entity_handle_link : basic_handle_link<entt::const_handle>
{
};
struct entity_handle_link : basic_handle_link<entt::handle>
{
};

SAVE_EXTERN(const_entity_handle_link);
LOAD_EXTERN(entity_handle_link);

} // namespace ser20
