#include "lumen_card_library.h"

#include "mesh_sdf_source.h"

#include <engine/engine.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/mesh.h>
#include <engine/threading/threader.h>

#include <logging/logging.h>

#include <chrono>

namespace unravel
{
namespace
{

/// UE's default MaxLumenMeshCards (FMeshBuildSettings): the card budget of one mesh.
constexpr uint32_t k_max_lumen_mesh_cards = 12;

auto build_submesh_cards(mesh& source, uint32_t submesh_index, bool two_sided) -> std::shared_ptr<const lumen_mesh_cards>
{
    APP_SCOPE_PERF("GI/Lumen/Build Mesh Cards");
    auto cards = std::make_shared<lumen_mesh_cards>();
    const auto* submesh = source.get_submesh(submesh_index);
    const uint32_t* indices = source.get_system_ib();
    if(submesh == nullptr || indices == nullptr)
    {
        return cards;
    }
    const auto start = std::chrono::steady_clock::now();
    sdf_source_geometry geometry;
    bool has_geometry = false;
    if(const sdf_source_geometry* gi_geometry = source.get_gi_source_geometry())
    {
        geometry = *gi_geometry;
        has_geometry = true;
    }
    else
    {
        has_geometry = extract_sdf_source_geometry(source.get_system_vb(),
                                                   source.get_vertex_count(),
                                                   source.get_vertex_format(),
                                                   indices + size_t(submesh->face_start) * 3u,
                                                   uint32_t(submesh->face_count),
                                                   geometry);
    }
    if(has_geometry)
    {
        build_lumen_mesh_cards(geometry, two_sided, k_max_lumen_mesh_cards, *cards);
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
    APPLOG_INFO("[Lumen] mesh cards: submesh {} ({} triangles{}) -> {} cards in {:.1f} ms",
                submesh_index,
                geometry.get_triangle_count(),
                two_sided ? ", two-sided" : "",
                cards->cards.size(),
                elapsed.count());
    return cards;
}

} // namespace

auto lumen_card_library::acquire(const std::shared_ptr<mesh>& owner,
                                 const hpp::uuid& mesh_uid,
                                 uint32_t submesh_index,
                                 bool two_sided) -> std::shared_ptr<const lumen_mesh_cards>
{
    const key k{mesh_uid, submesh_index, two_sided};
    std::lock_guard<std::mutex> lock(*mutex_);
    entry& e = (*entries_)[k];
    if(e.cards || e.is_scheduled || !owner)
    {
        return e.cards;
    }
    e.is_scheduled = true;
    // The job owns the mesh, the table and the lock, so it may outlive this library.
    auto& pool = *engine::context().get_cached<threader>().pool;
    pool.schedule("Lumen/Build Mesh Cards",
                  [owner, k, mutex = mutex_, entries = entries_]()
                  {
                      auto cards = build_submesh_cards(*owner, k.submesh_index, k.two_sided);
                      std::lock_guard<std::mutex> job_lock(*mutex);
                      (*entries)[k].cards = std::move(cards);
                  });
    return nullptr;
}

} // namespace unravel
