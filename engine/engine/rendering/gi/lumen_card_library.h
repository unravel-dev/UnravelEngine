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
 * @brief Lumen card sets per (mesh asset, submesh, two-sidedness), built once on the thread pool.
 *
 * UE builds a mesh's cards offline next to its distance field; here they are built on first use from
 * the mesh's CPU buffers, so a mesh compiled before cards existed still gets them. A card set depends
 * on the submesh's triangles and on whether its material is two-sided, never on a placement, so every
 * placement of a submesh shares one set.
 */
class lumen_card_library
{
public:
    /**
     * @brief The submesh's cards, or null while they are being built.
     *
     * The first request schedules the build; later requests return null until it finishes, then the
     * shared set. A submesh whose geometry has no triangles gets an empty set, not a retry.
     */
    auto acquire(const std::shared_ptr<mesh>& owner, const hpp::uuid& mesh_uid, uint32_t submesh_index, bool two_sided)
        -> std::shared_ptr<const lumen_mesh_cards>;

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

    std::shared_ptr<std::mutex> mutex_ = std::make_shared<std::mutex>();
    std::shared_ptr<std::unordered_map<key, entry, key_hash>> entries_ =
        std::make_shared<std::unordered_map<key, entry, key_hash>>();
};

} // namespace unravel
