#pragma once

#include "mesh_import_texture_conversion.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct aiScene;

namespace unravel
{
namespace importer
{
namespace mesh_import
{

/// "e:<embedded index>:<semantic>" or "x:<normalized path>:<semantic>".
auto make_texture_catalog_key(const imported_texture& tex) -> std::string;

/// External texture whose texels are rewritten before use (shininess/specular/inverted maps).
auto needs_external_texture_conversion(const imported_texture& tex) -> bool;

/**
 * @brief What each requested (texture, semantic) resolved to once its job ran: the file the
 * material binds, keyed by make_texture_catalog_key of the request.
 */
class texture_catalog
{
public:
    auto resolve(imported_texture& tex) const -> bool
    {
        const auto it = entries_.find(make_texture_catalog_key(tex));
        if(it == entries_.end())
        {
            return false;
        }
        tex.name = it->second.name;
        tex.flags = it->second.flags;
        tex.inverse = it->second.inverse;
        tex.process_count = it->second.process_count;
        tex.semantic = it->second.semantic;
        tex.embedded_index = it->second.embedded_index;
        return true;
    }

    void register_entry(const imported_texture& lookup_key, imported_texture result)
    {
        entries_[make_texture_catalog_key(lookup_key)] = std::move(result);
    }

    void append_to_manifest(std::vector<imported_texture>& textures) const
    {
        for(const auto& kvp : entries_)
        {
            const auto& entry = kvp.second;
            const auto exists = std::find_if(textures.begin(),
                                             textures.end(),
                                             [&](const imported_texture& rhs)
                                             {
                                                 return rhs.embedded_index == entry.embedded_index
                                                     && rhs.name == entry.name && rhs.semantic == entry.semantic;
                                             });
            if(exists == textures.end())
            {
                textures.push_back(entry);
            }
        }
    }

    void merge_from(const texture_catalog& other)
    {
        for(const auto& kvp : other.entries_)
        {
            entries_[kvp.first] = kvp.second;
        }
    }

private:
    std::unordered_map<std::string, imported_texture> entries_;
};

enum class texture_job_type : uint8_t
{
    embedded_extract,
    external_convert,
};

struct texture_job
{
    texture_job_type type{};
    imported_texture desc{};
};

/// Identity of a texture job: its source texture and semantic.
auto make_texture_job_key(const texture_job& job) -> std::string;

/// Texture jobs collected from every material, each distinct job once.
class texture_job_store
{
public:
    auto jobs() const -> const std::vector<texture_job>&
    {
        return jobs_;
    }

    auto jobs() -> std::vector<texture_job>&
    {
        return jobs_;
    }

    auto try_add(texture_job job) -> bool
    {
        if(!dedupe_keys_.insert(make_texture_job_key(job)).second)
        {
            return false;
        }
        jobs_.push_back(std::move(job));
        return true;
    }

private:
    std::vector<texture_job> jobs_;
    std::unordered_set<std::string> dedupe_keys_;
};

/**
 * @brief Run every job of @p store on the thread pool. Jobs writing a common output file run
 * serially in one group; results merge into @p catalog and @p consumed_embedded in job order.
 */
void run_texture_jobs_parallel(texture_job_store& store,
                               const fs::path& filename,
                               const fs::path& output_dir,
                               const aiScene* scene,
                               texture_catalog& catalog,
                               std::unordered_set<int>& consumed_embedded);

/// Deterministic manifest order: embedded index, semantic, name.
void sort_imported_textures(std::vector<imported_texture>& textures);

void mark_embedded_consumed_from_textures(const std::vector<imported_texture>& textures,
                                          std::unordered_set<int>& consumed_embedded);

/// Extract jobs for embedded textures no material referenced.
void collect_orphan_embedded_texture_jobs(const aiScene* scene,
                                          const fs::path& filename,
                                          texture_job_store& store,
                                          const std::unordered_set<int>& consumed_embedded);

} // namespace mesh_import
} // namespace importer
} // namespace unravel
