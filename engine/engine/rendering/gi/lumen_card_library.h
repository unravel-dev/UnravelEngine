#pragma once

#include <engine/rendering/gi/lumen_mesh_cards.h>

#include <hpp/uuid.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace unravel
{

class mesh;

/**
 * @brief Surface cache card sets per (mesh asset, submesh, two-sidedness).
 *
 * A card set depends on the submesh's triangles and on whether its material is two-sided, never on a placement, so
 * every placement of a submesh shares one set. An imported mesh carries its sets from the compiler (one per
 * submesh, for the material it imported with); the library builds a set on the thread pool only for a mesh without
 * them (procedurally created) or for a placement whose material flips the sidedness.
 */
class lumen_card_library
{
public:
    /**
     * @brief The submesh's cards, or null while they are being built.
     *
     * The mesh's compiled set when its sidedness matches (an empty set when the import settings turned cards off).
     * Otherwise the first request schedules a build; later requests return null until it finishes, then the shared
     * set. A submesh whose geometry has no triangles gets an empty set, not a retry.
     */
    auto acquire(const std::shared_ptr<mesh>& owner, const hpp::uuid& mesh_uid, uint32_t submesh_index, bool two_sided)
        -> std::shared_ptr<const lumen_mesh_cards>;

    /**
     * @brief What the build of the submesh's cards saw, for the card generation visualization, or null while it is
     *        being built: the first request builds the submesh's cards again on the thread pool, recording it.
     */
    auto acquire_build_debug(const std::shared_ptr<mesh>& owner, uint32_t submesh_index, bool two_sided)
        -> std::shared_ptr<const lumen_card_build_debug>;

    /// Drops every recorded build (later requests build again).
    void release_build_debug();

private:
    struct key
    {
        hpp::uuid mesh_uid{};
        uint32_t submesh_index = 0;
        bool two_sided = false;

        auto operator==(const key& other) const -> bool
        {
            return mesh_uid == other.mesh_uid && submesh_index == other.submesh_index &&
                   two_sided == other.two_sided;
        }
    };

    struct key_hash
    {
        auto operator()(const key& k) const -> size_t
        {
            const size_t uid_hash = std::hash<hpp::uuid>{}(k.mesh_uid);
            return uid_hash ^ (size_t(k.submesh_index) * 0x9e3779b97f4a7c15ull) ^ (k.two_sided ? 0x5bd1e995ull : 0ull);
        }
    };

    struct entry
    {
        ///< Written once by the build job, under the library's mutex.
        std::shared_ptr<const lumen_mesh_cards> cards;
        bool is_scheduled = false;
    };

    /// A recorded build per (mesh, submesh, two-sidedness); the mesh is held weakly, so a mesh loaded again at the
    /// same address builds again.
    struct debug_key
    {
        const mesh* owner = nullptr;
        uint32_t submesh_index = 0;
        bool two_sided = false;

        auto operator==(const debug_key& other) const -> bool
        {
            return owner == other.owner && submesh_index == other.submesh_index && two_sided == other.two_sided;
        }
    };

    struct debug_key_hash
    {
        auto operator()(const debug_key& k) const -> size_t
        {
            return std::hash<const mesh*>{}(k.owner) ^ (size_t(k.submesh_index) * 0x9e3779b97f4a7c15ull) ^
                   (k.two_sided ? 0x5bd1e995ull : 0ull);
        }
    };

    struct debug_entry
    {
        std::weak_ptr<mesh> owner;
        ///< Written once by the build job, under the library's mutex.
        std::shared_ptr<const lumen_card_build_debug> debug;
        bool is_scheduled = false;
    };

    std::shared_ptr<std::mutex> mutex_ = std::make_shared<std::mutex>();
    std::shared_ptr<std::unordered_map<key, entry, key_hash>> entries_ =
        std::make_shared<std::unordered_map<key, entry, key_hash>>();
    std::shared_ptr<std::unordered_map<debug_key, debug_entry, debug_key_hash>> debug_entries_ =
        std::make_shared<std::unordered_map<debug_key, debug_entry, debug_key_hash>>();
};

} // namespace unravel
