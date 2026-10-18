#include "protocol_data.h"
#include "files.h"
#include "releases.h"

#include <engine/assets/impl/asset_extensions.h>

namespace unravel::deployment
{
namespace
{
auto make_shipped_filter(const protocol_job& job) -> compiled_file_filter
{
    return [&job](const fs::path& file, const fs::path& relative)
    {
        return is_shipped_compiled_file(file, job.with_debug_symbols) && !is_other_shader_variant(file, job.shader_variants) &&
               !job.left_out.contains(fs::make_path_key(get_source_key(job.protocol, relative)));
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
    copy_cooked_or_throw(job.cooked_root, destination, is_shipped);
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
    const auto inputs = collect_pack_inputs(job.compiled_root, job.cooked_root, index, is_shipped);
    if(job.patch_release_pack.empty())
    {
        write_full_pack(job.protocol_root, inputs);
    }
    else
    {
        write_patch_over_release(job.protocol_root, inputs, job.patch_release_pack);
    }
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
                          return !is_script_key(location) && !job.left_out.contains(fs::make_path_key(location));
                      });
    const auto is_shipped = make_shipped_filter(job);
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
