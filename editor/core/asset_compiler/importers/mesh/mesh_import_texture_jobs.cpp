#include "mesh_import_texture_jobs.h"

#include "mesh_import_image.h"
#include "mesh_import_paths.h"

#include <concurrency/parallel.h>
#include <graphics/utils/bgfx_utils.h>
#include <logging/logging.h>

#include <assimp/scene.h>

#include <algorithm>
#include <numeric>

namespace unravel
{
namespace importer
{
namespace mesh_import
{
namespace
{

auto get_texture_job_output_paths(const texture_job& job) -> std::vector<std::string>
{
    std::vector<std::string> paths;
    switch(job.type)
    {
        case texture_job_type::embedded_extract:
            if(!job.desc.name.empty())
            {
                paths.push_back(job.desc.name);
            }
            break;
        case texture_job_type::external_convert:
            paths.push_back(make_converted_texture_name(job.desc.name, job.desc.semantic));
            break;
        default:
            break;
    }
    return paths;
}

struct texture_job_disjoint_set
{
    explicit texture_job_disjoint_set(size_t count) : parent_(count)
    {
        std::iota(parent_.begin(), parent_.end(), size_t{0});
    }

    auto find(size_t index) -> size_t
    {
        while(parent_[index] != index)
        {
            parent_[index] = parent_[parent_[index]];
            index = parent_[index];
        }
        return index;
    }

    void unite(size_t a, size_t b)
    {
        a = find(a);
        b = find(b);
        if(a != b)
        {
            parent_[b] = a;
        }
    }

    std::vector<size_t> parent_;
};

auto build_texture_job_composite_groups(const std::vector<texture_job>& jobs) -> std::vector<std::vector<size_t>>
{
    if(jobs.empty())
    {
        return {};
    }

    texture_job_disjoint_set disjoint_set(jobs.size());
    std::unordered_map<std::string, size_t> path_to_job_index;

    for(size_t job_index = 0; job_index < jobs.size(); ++job_index)
    {
        for(const auto& output_path : get_texture_job_output_paths(jobs[job_index]))
        {
            if(output_path.empty())
            {
                continue;
            }

            const auto existing = path_to_job_index.find(output_path);
            if(existing == path_to_job_index.end())
            {
                path_to_job_index.emplace(output_path, job_index);
            }
            else
            {
                disjoint_set.unite(job_index, existing->second);
            }
        }
    }

    std::unordered_map<size_t, std::vector<size_t>> groups_by_root;
    groups_by_root.reserve(jobs.size());
    for(size_t job_index = 0; job_index < jobs.size(); ++job_index)
    {
        groups_by_root[disjoint_set.find(job_index)].push_back(job_index);
    }

    std::vector<std::vector<size_t>> composites;
    composites.reserve(groups_by_root.size());
    for(auto& kvp : groups_by_root)
    {
        auto& group = kvp.second;
        std::sort(group.begin(), group.end());
        composites.push_back(std::move(group));
    }

    std::sort(composites.begin(),
              composites.end(),
              [](const std::vector<size_t>& lhs, const std::vector<size_t>& rhs)
              {
                  return lhs.front() < rhs.front();
              });

    return composites;
}

void execute_texture_job(const texture_job& job,
                         const fs::path& filename,
                         const fs::path& output_dir,
                         const aiScene* scene,
                         texture_catalog& catalog,
                         std::unordered_set<int>& consumed_embedded)
{
    switch(job.type)
    {
        case texture_job_type::embedded_extract:
        {
            imported_texture result = job.desc;
            std::vector<imported_texture> scratch;
            scratch.push_back(result);
            const auto* embedded = scene->mTextures[result.embedded_index];
            process_embedded_texture(embedded, static_cast<size_t>(result.embedded_index), filename, output_dir, scratch);
            result = scratch.back();
            catalog.register_entry(job.desc, result);
            consumed_embedded.insert(result.embedded_index);
            break;
        }
        case texture_job_type::external_convert:
        {
            imported_texture result = job.desc;
            fs::path original_file = output_dir / result.name;
            const auto converted_name = make_converted_texture_name(result.name, result.semantic);
            fs::path converted_file = output_dir / converted_name;
            bimg::ImageContainer* image = imageLoad(bx::FilePath(original_file.string().c_str()));
            if(image)
            {
                apply_texture_conversion(image, result.semantic, result.inverse);
                atomic_image_save(converted_file, image);
                bimg::imageFree(image);
                result.name = converted_name;
                APPLOG_TRACE("Mesh Importer: Applied {} conversion to external texture: {}", result.semantic, result.name);
            }
            catalog.register_entry(job.desc, result);
            break;
        }
        default:
            break;
    }
}

void execute_texture_job_composite(const std::vector<texture_job>& jobs,
                                 const std::vector<size_t>& job_indices,
                                 const fs::path& filename,
                                 const fs::path& output_dir,
                                 const aiScene* scene,
                                 texture_catalog& catalog,
                                 std::unordered_set<int>& consumed_embedded)
{
    for(const size_t job_index : job_indices)
    {
        execute_texture_job(jobs[job_index], filename, output_dir, scene, catalog, consumed_embedded);
    }
}

} // namespace

auto make_texture_catalog_key(const imported_texture& tex) -> std::string
{
    if(tex.embedded_index >= 0)
    {
        return fmt::format("e:{}:{}", tex.embedded_index, tex.semantic);
    }
    return fmt::format("x:{}:{}", normalize_material_texture_path(normalize_assimp_path(tex.name)), tex.semantic);
}

auto needs_external_texture_conversion(const imported_texture& tex) -> bool
{
    return tex.embedded_index < 0 && is_pixel_conversion(tex.semantic, tex.inverse);
}

auto make_texture_job_key(const texture_job& job) -> std::string
{
    switch(job.type)
    {
        case texture_job_type::embedded_extract:
        case texture_job_type::external_convert:
            return make_texture_catalog_key(job.desc);
        default:
            return {};
    }
}

void sort_imported_textures(std::vector<imported_texture>& textures)
{
    std::sort(textures.begin(),
              textures.end(),
              [](const imported_texture& lhs, const imported_texture& rhs)
              {
                  if(lhs.embedded_index != rhs.embedded_index)
                  {
                      return lhs.embedded_index < rhs.embedded_index;
                  }
                  if(lhs.semantic != rhs.semantic)
                  {
                      return lhs.semantic < rhs.semantic;
                  }
                  return lhs.name < rhs.name;
              });
}

void run_texture_jobs_parallel(texture_job_store& store,
                              const fs::path& filename,
                              const fs::path& output_dir,
                              const aiScene* scene,
                              texture_catalog& catalog,
                              std::unordered_set<int>& consumed_embedded)
{
    const auto& jobs = store.jobs();
    if(jobs.empty())
    {
        return;
    }

    const auto composites = build_texture_job_composite_groups(jobs);
    APPLOG_TRACE("Mesh Importer: Running {} texture job composites ({} jobs) in parallel",
                 composites.size(),
                 jobs.size());

    struct composite_result_t
    {
        texture_catalog catalog;
        std::unordered_set<int> consumed_embedded;
    };

    std::vector<composite_result_t> composite_results(composites.size());

    poolstl::for_each_par_if(true,
                  poolstl::iota_iter<size_t>(0),
                  poolstl::iota_iter<size_t>(composites.size()),
                  [&](const size_t composite_index)
                  {
                      auto& result = composite_results[composite_index];
                      execute_texture_job_composite(jobs,
                                                    composites[composite_index],
                                                    filename,
                                                    output_dir,
                                                    scene,
                                                    result.catalog,
                                                    result.consumed_embedded);
                  });

    for(auto& result : composite_results)
    {
        catalog.merge_from(result.catalog);
        consumed_embedded.insert(result.consumed_embedded.begin(), result.consumed_embedded.end());
    }
}

void mark_embedded_consumed_from_textures(const std::vector<imported_texture>& textures,
                                          std::unordered_set<int>& consumed_embedded)
{
    for(const auto& tex : textures)
    {
        if(tex.embedded_index >= 0 && tex.process_count > 0)
        {
            consumed_embedded.insert(tex.embedded_index);
        }
    }
}

void collect_orphan_embedded_texture_jobs(const aiScene* scene,
                                          const fs::path& filename,
                                          texture_job_store& store,
                                          const std::unordered_set<int>& consumed_embedded)
{
    for(size_t i = 0; i < scene->mNumTextures; ++i)
    {
        if(consumed_embedded.count(static_cast<int>(i)) > 0)
        {
            continue;
        }

        imported_texture tex{};
        tex.embedded_index = static_cast<int>(i);
        tex.semantic = "Texture";
        tex.name = get_embedded_texture_name(scene->mTextures[i], i, filename, tex.semantic, false);

        texture_job job{};
        job.type = texture_job_type::embedded_extract;
        job.desc = tex;
        store.try_add(std::move(job));
    }
}

} // namespace mesh_import
} // namespace importer
} // namespace unravel
