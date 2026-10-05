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

auto spec_gloss_factors_key(const spec_gloss_factors_t& factors) -> std::string
{
    return fmt::format("{:.4f}_{:.4f}_{:.4f}_{:.4f}_{:.4f}_{:.4f}_{:.4f}_{:.4f}",
                       factors.diffuse_r,
                       factors.diffuse_g,
                       factors.diffuse_b,
                       factors.diffuse_a,
                       factors.specular_r,
                       factors.specular_g,
                       factors.specular_b,
                       factors.glossiness);
}

auto load_image_for_texture_desc(const aiScene* scene,
                                 const fs::path& output_dir,
                                 const imported_texture& tex,
                                 const char* assimp_path_cstr = nullptr) -> bimg::ImageContainer*
{
    if(tex.embedded_index >= 0 && tex.embedded_index < static_cast<int>(scene->mNumTextures))
    {
        const auto* embedded = scene->mTextures[tex.embedded_index];
        if(embedded->pcData && embedded->mHeight == 0)
        {
            return imageLoad(embedded->pcData, static_cast<uint32_t>(embedded->mWidth));
        }
        return nullptr;
    }

    if(assimp_path_cstr != nullptr && assimp_path_cstr[0] != '\0')
    {
        const fs::path relative = resolve_external_texture_path(output_dir, normalize_assimp_path(assimp_path_cstr));
        return imageLoad(bx::FilePath((output_dir / relative).string().c_str()));
    }

    const fs::path relative = resolve_external_texture_path(output_dir, normalize_assimp_path(tex.name));
    return imageLoad(bx::FilePath((output_dir / relative).string().c_str()));
}

void mark_embedded_consumed_index(int idx, std::unordered_set<int>& consumed, texture_catalog& catalog)
{
    if(idx < 0)
    {
        return;
    }
    consumed.insert(idx);
    imported_texture entry{};
    entry.embedded_index = idx;
    entry.process_count = 1;
    catalog.register_entry(entry, entry);
}

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
        case texture_job_type::spec_gloss_pair:
            if(!job.output_base_color_relative.empty())
            {
                paths.push_back(job.output_base_color_relative);
            }
            if(!job.output_mr_relative.empty())
            {
                paths.push_back(job.output_mr_relative);
            }
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
        case texture_job_type::spec_gloss_pair:
        {
            bimg::ImageContainer* diffuse_img = load_image_for_texture_desc(scene, output_dir, job.desc);
            bimg::ImageContainer* specular_img =
                load_image_for_texture_desc(scene, output_dir, job.specular_desc, nullptr);

            if(!diffuse_img)
            {
                APPLOG_WARNING("Mesh Importer: Spec-gloss pair job could not load diffuse texture: {}",
                               job.desc.name);
            }
            if(!specular_img)
            {
                APPLOG_WARNING("Mesh Importer: Spec-gloss pair job could not load specular texture: {}",
                               job.specular_desc.name);
            }

            if(diffuse_img && specular_img)
            {
                auto conv = convert_spec_gloss_to_pbr_textures(output_dir,
                                                               job.output_base_color_relative,
                                                               job.output_mr_relative,
                                                               diffuse_img,
                                                               specular_img,
                                                               job.spec_gloss_factors,
                                                               job.bake_base_color);
                if(conv.diffuse_converted)
                {
                    imported_texture base_result = job.desc;
                    base_result.name = conv.base_color_relative;
                    catalog.register_entry(job.desc, base_result);
                    mark_embedded_consumed_index(job.desc.embedded_index, consumed_embedded, catalog);
                }
                catalog.register_pair_outputs(make_texture_job_key(job), conv);
                if(!conv.mr_relative.empty())
                {
                    imported_texture mr_lookup = job.specular_desc;
                    imported_texture mr_result = job.specular_desc;
                    mr_result.name = conv.mr_relative;
                    catalog.register_entry(mr_lookup, mr_result);
                    mark_embedded_consumed_index(job.specular_desc.embedded_index, consumed_embedded, catalog);
                    APPLOG_TRACE("Mesh Importer: Wrote pair metallic-roughness: {}", conv.mr_relative);
                }
                else
                {
                    APPLOG_WARNING("Mesh Importer: Spec-gloss pair job produced no metallic-roughness output ({} + {})",
                                   job.desc.name,
                                   job.specular_desc.name);
                }
            }
            if(specular_img)
            {
                bimg::imageFree(specular_img);
            }
            if(diffuse_img)
            {
                bimg::imageFree(diffuse_img);
            }
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

/// "<dir>/<stem>_<hash>.png" for "<dir>/<stem>.png".
auto append_job_hash(const std::string& relative, const std::string& job_key) -> std::string
{
    uint32_t hash = 2166136261u;
    for(const char c : job_key)
    {
        hash = (hash ^ static_cast<uint8_t>(c)) * 16777619u;
    }
    const fs::path path(relative);
    const std::string stem = fmt::format("{}_{:08x}", path.stem().string(), hash);
    return (path.parent_path() / (stem + path.extension().string())).generic_string();
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
        case texture_job_type::spec_gloss_pair:
            return fmt::format("sg:{}:{}:{}:{}",
                               make_texture_catalog_key(job.desc),
                               make_texture_catalog_key(job.specular_desc),
                               spec_gloss_factors_key(job.spec_gloss_factors),
                               job.bake_base_color ? "bc" : "mr");
        default:
            return {};
    }
}

auto try_synchronous_spec_gloss_pair_mr(const fs::path& output_dir,
                                        const aiScene* scene,
                                        const imported_texture& pair_source,
                                        const imported_texture& specular_tex,
                                        const spec_gloss_factors_t& factors,
                                        const std::string& expected_mr,
                                        bool bake_base_color) -> std::string
{
    bimg::ImageContainer* diffuse_img = load_image_for_texture_desc(scene, output_dir, pair_source);
    bimg::ImageContainer* specular_img = load_image_for_texture_desc(scene, output_dir, specular_tex);

    if(!diffuse_img)
    {
        APPLOG_WARNING("Mesh Importer: Bind-time pair MR could not load diffuse: {}", pair_source.name);
    }
    if(!specular_img)
    {
        APPLOG_WARNING("Mesh Importer: Bind-time pair MR could not load specular: {}", specular_tex.name);
    }

    if(!diffuse_img || !specular_img)
    {
        if(diffuse_img)
        {
            bimg::imageFree(diffuse_img);
        }
        if(specular_img)
        {
            bimg::imageFree(specular_img);
        }
        return {};
    }

    auto conv = convert_spec_gloss_to_pbr_textures(output_dir,
                                                   std::string{},
                                                   expected_mr,
                                                   diffuse_img,
                                                   specular_img,
                                                   factors,
                                                   bake_base_color);
    bimg::imageFree(diffuse_img);
    bimg::imageFree(specular_img);

    if(!conv.mr_relative.empty())
    {
        APPLOG_TRACE("Mesh Importer: Bind-time pair metallic-roughness bake: {}", conv.mr_relative);
    }
    return conv.mr_relative;
}

void disambiguate_shared_pair_outputs(texture_job_store& store)
{
    std::unordered_map<std::string, size_t> writers_per_output;
    for(const auto& job : store.jobs())
    {
        if(job.type != texture_job_type::spec_gloss_pair)
        {
            continue;
        }
        for(const auto& output : get_texture_job_output_paths(job))
        {
            ++writers_per_output[output];
        }
    }
    for(auto& job : store.jobs())
    {
        if(job.type != texture_job_type::spec_gloss_pair)
        {
            continue;
        }
        const std::string job_key = make_texture_job_key(job);
        for(std::string* output : {&job.output_base_color_relative, &job.output_mr_relative})
        {
            if(!output->empty() && writers_per_output[*output] > 1)
            {
                *output = append_job_hash(*output, job_key);
            }
        }
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
