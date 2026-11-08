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

/// Removes the texture set packs (ex::get_texture_pack_file_name) of the sets `job` does not ship, left by an earlier
/// build: the page would download them and the game mount them over the protocol's pack.
void remove_unshipped_texture_packs(const protocol_job& job)
{
    fs::error_code ec;
    for(const auto* set : ex::TEXTURE_SET_NAMES)
    {
        const bool is_shipped = std::any_of(job.texture_sets.begin(),
                                            job.texture_sets.end(),
                                            [set](const texture_set_job& shipped)
                                            {
                                                return shipped.name == set;
                                            });
        if(!is_shipped)
        {
            fs::remove(job.protocol_root / ex::get_texture_pack_file_name(set), ec);
        }
    }
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
        auto inputs = collect_pack_inputs(job.compiled_root, set.cooked, {}, is_shipped_texture);
        std::erase_if(inputs,
                      [](const fs::pack_input& input)
                      {
                          return input.name == ex::get_asset_index_file_name();
                      });
        write_pack_file(job.protocol_root / ex::get_texture_pack_file_name(set.name), inputs);
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
    fs::error_code ec;
    fs::remove(job.protocol_root / ex::get_pack_file_name(), ec);
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
    mirror_or_throw(job.compiled_root,
                    destination,
                    [&](const fs::path& file)
                    {
                        return is_loose_only_file(file) && is_shipped_compiled_file(file, job.with_debug_symbols);
                    });
    // With texture sets, the protocol's pack ships no texture: each set's pack holds them.
    const compiled_file_filter is_packed = [&](const fs::path& file, const fs::path& relative)
    {
        return is_shipped(file, relative) && (job.texture_sets.empty() || !is_texture_file(job.protocol, relative));
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
    remove_unshipped_texture_packs(job);
    if(!job.is_packed)
    {
        deploy_loose(job, destination, is_shipped);
        return;
    }
    deploy_packed(job, destination, index, is_shipped);
    // The index travels inside the pack.
    fs::remove(index, ec);
}

} // namespace unravel::deployment
