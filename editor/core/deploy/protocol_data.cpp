#include "protocol_data.h"
#include "files.h"
#include "releases.h"
#include "texture_variants.h"

#include <engine/assets/impl/asset_extensions.h>

#include <algorithm>

namespace unravel::deployment
{
namespace
{
/// Whether the compiled file at `relative` in the protocol's compiled folder is a texture's.
auto is_texture_file(const std::string& protocol, const fs::path& relative) -> bool
{
    return is_asset_key<gfx::texture>(get_source_key(protocol, relative));
}

/// Whether `file` is a compiled shader of one of `job`'s shader sets.
auto is_shader_set_file(const protocol_job& job, const fs::path& file) -> bool
{
    const auto extension = file.extension().generic_string();
    return std::any_of(job.shader_sets.begin(),
                       job.shader_sets.end(),
                       [&extension](const shader_set_job& set)
                       {
                           return set.renderer_extension == extension;
                       });
}

/// Removes the packs (ex::get_set_pack_file_name) of the sets among `names` that `shipped` does not list, left by an
/// earlier build: the page would download them and the game mount them over the protocol's pack.
template<typename Names, typename Sets>
void remove_unshipped_set_packs(const fs::path& protocol_root, const Names& names, const Sets& shipped)
{
    for(const auto* name : names)
    {
        const bool is_shipped = std::any_of(shipped.begin(),
                                            shipped.end(),
                                            [name](const auto& set)
                                            {
                                                return set.name == name;
                                            });
        if(!is_shipped)
        {
            remove_file_or_throw(protocol_root / ex::get_set_pack_file_name(name));
        }
    }
}

/// Writes `pack` with the shipped files `is_in_set` takes, from the compiled cache under `cooked`, without the index.
void write_set_pack(const protocol_job& job, const cooked_folders& cooked, const compiled_file_filter& is_in_set, const fs::path& pack)
{
    auto inputs = collect_pack_inputs(job.compiled_root, cooked, {}, is_in_set);
    std::erase_if(inputs,
                  [](const fs::pack_input& input)
                  {
                      return input.name == ex::get_asset_index_file_name();
                  });
    write_pack_file(pack, inputs);
}

/// Writes a pack per texture set of `job`, each holding the shipped textures as the set compiles them.
void write_texture_set_packs(const protocol_job& job, const compiled_file_filter& is_shipped)
{
    const compiled_file_filter is_shipped_texture = [&](const fs::path& file, const fs::path& relative)
    {
        return is_shipped(file, relative) && is_texture_file(job.protocol, relative);
    };
    for(const auto& set : job.texture_sets)
    {
        write_set_pack(job, set.cooked, is_shipped_texture, job.protocol_root / ex::get_set_pack_file_name(set.name));
    }
}

/// Writes a pack per shader set of `job`, each holding the shipped shaders compiled for its graphics API.
void write_shader_set_packs(const protocol_job& job, const compiled_file_filter& is_shipped)
{
    for(const auto& set : job.shader_sets)
    {
        const compiled_file_filter is_set_shader = [&](const fs::path& file, const fs::path& relative)
        {
            return is_shipped(file, relative) && file.extension().generic_string() == set.renderer_extension;
        };
        write_set_pack(job, job.cooked, is_set_shader, job.protocol_root / ex::get_set_pack_file_name(set.name));
    }
}

auto make_shipped_filter(const protocol_job& job) -> compiled_file_filter
{
    return [&job](const fs::path& file, const fs::path& relative)
    {
        return is_shipped_compiled_file(file, job.with_debug_symbols) && !is_other_shader_variant(file, job.shader_variants) &&
               !job.left_out.contains(get_source_key(job.protocol, relative));
    };
}

/// The loose build: every shipped compiled file mirrored, the cooked variants over them, no pack.
void deploy_loose(const protocol_job& job, const fs::path& destination, const compiled_file_filter& is_shipped)
{
    // A pack left from an earlier build would be mounted over the loose files.
    remove_file_or_throw(job.protocol_root / ex::get_pack_file_name());
    remove_patch_packs(job.protocol_root, 1);
    mirror_or_throw(job.compiled_root,
                    destination,
                    [&](const fs::path& file)
                    {
                        return is_shipped(file, file.lexically_relative(job.compiled_root));
                    });
    copy_cooked_or_throw(job.cooked, destination, is_shipped);
}

/// The packed build: only what the runtime loads by path stays loose; the pack reads the rest straight
/// from the compiled cache.
void deploy_packed(const protocol_job& job, const fs::path& destination, const fs::path& index, const compiled_file_filter& is_shipped)
{
    const compiled_file_filter is_loose = [&](const fs::path& file, const fs::path&)
    {
        return is_loose_only_file(file) && is_shipped_compiled_file(file, job.with_debug_symbols);
    };
    mirror_or_throw(job.compiled_root,
                    destination,
                    [&](const fs::path& file)
                    {
                        return is_loose(file, {});
                    });
    // A script library the deploy built replaces the editor's.
    copy_cooked_or_throw(job.cooked, destination, is_loose);
    // With texture or shader sets, the protocol's pack ships none of their files: each set's pack holds them.
    const compiled_file_filter is_packed = [&](const fs::path& file, const fs::path& relative)
    {
        return is_shipped(file, relative) && (job.texture_sets.empty() || !is_texture_file(job.protocol, relative)) &&
               !is_shader_set_file(job, file);
    };
    const auto inputs = collect_pack_inputs(job.compiled_root, job.cooked, index, is_packed);
    if(job.patch_release_pack.empty())
    {
        write_full_pack(job.protocol_root, inputs);
    }
    else
    {
        write_patch_over_release(job.protocol_root, inputs, job.patch_release_pack);
    }
    write_texture_set_packs(job, is_shipped);
    write_shader_set_packs(job, is_shipped);
}
} // namespace

void deploy_protocol_data(const protocol_job& job)
{
    const auto destination = job.protocol_root / ex::get_compiled_directory_no_slash();
    const auto index = job.protocol_root / ex::get_asset_index_file_name();
    fs::error_code ec;
    fs::remove(job.protocol_root / LEGACY_ASSET_INDEX_NAME, ec);
    write_asset_index(job.database,
                      index,
                      [&job](const std::string& location)
                      {
                          return !is_asset_key<script>(location) && !job.left_out.contains(location);
                      });
    const auto is_shipped = make_shipped_filter(job);
    remove_unshipped_set_packs(job.protocol_root, ex::TEXTURE_SET_NAMES, job.texture_sets);
    remove_unshipped_set_packs(job.protocol_root, ex::SHADER_SET_NAMES, job.shader_sets);
    if(!job.is_packed)
    {
        deploy_loose(job, destination, is_shipped);
        return;
    }
    deploy_packed(job, destination, index, is_shipped);
    // The index travels inside the pack; a loose one beside it is not the build's.
    remove_file_or_throw(index);
}

} // namespace unravel::deployment
