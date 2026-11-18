#include "audit_catalog.h"
#include "audit_facts.h"
#include "audit_rules.h"

#include <asset_compiler/audio_compression.h>
#include <asset_compiler/texture_target.h>
#include <deploy/texture_variants.h>
#include <engine/assets/impl/asset_extensions.h>
#include <logging/logging.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <unordered_set>

namespace unravel::audit::detail
{
namespace
{

/// A texture's size in one profile's build, from the editor's compiled size.
struct build_size
{
    std::uint32_t width{};
    std::uint32_t height{};
    /// The editor's compile limited the texture below what the profile allows, so the build's texture is at least
    /// this large.
    bool is_lower_bound{false};
};

auto get_size_pixels(texture_importer_meta::texture_size size) -> std::uint32_t
{
    switch(size)
    {
        case texture_importer_meta::texture_size::size_32:
            return 32;
        case texture_importer_meta::texture_size::size_64:
            return 64;
        case texture_importer_meta::texture_size::size_128:
            return 128;
        case texture_importer_meta::texture_size::size_256:
            return 256;
        case texture_importer_meta::texture_size::size_512:
            return 512;
        case texture_importer_meta::texture_size::size_1024:
            return 1024;
        case texture_importer_meta::texture_size::size_2048:
            return 2048;
        case texture_importer_meta::texture_size::size_4096:
            return 4096;
        case texture_importer_meta::texture_size::size_8192:
            return 8192;
        case texture_importer_meta::texture_size::size_16384:
            return 16384;
        case texture_importer_meta::texture_size::project_default:
        default:
            return 0;
    }
}

auto get_build_size(const texture_facts& facts, std::uint32_t editor_max, std::uint32_t profile_max) -> build_size
{
    const auto side = std::max(facts.width, facts.height);
    if(side == 0)
    {
        return {};
    }
    if(profile_max > 0 && side > profile_max)
    {
        const auto scale = [&](std::uint32_t value)
        {
            return std::max<std::uint32_t>(1, static_cast<std::uint32_t>((std::uint64_t(value) * profile_max + side / 2) / side));
        };
        return {scale(facts.width), scale(facts.height), false};
    }
    const bool editor_limited = editor_max > 0 && side >= editor_max && (profile_max == 0 || profile_max > editor_max);
    return {facts.width, facts.height, editor_limited};
}

/// Bytes of a texture of this size and bits per texel, its mipmap chain included when it has one.
auto get_texture_bytes(const build_size& size, std::uint32_t bits_per_pixel, bool has_mips) -> std::uint64_t
{
    const auto base = std::uint64_t(size.width) * size.height * bits_per_pixel / 8;
    return has_mips ? base * 4 / 3 : base;
}

auto describe_size(const build_size& size) -> std::string
{
    return fmt::format("{} x {}", size.width, size.height);
}

auto get_profile_texture_target(const profile_record& profile) -> asset_compiler::texture_target
{
    return deployment::get_profile_texture_target(profile.deploy, profile.resolved);
}

/// The texture's import settings, or null for another asset.
auto as_texture(const asset_record& asset) -> const texture_importer_meta*
{
    return asset.importer ? asset.importer->safe_cast<texture_importer_meta>() : nullptr;
}

auto as_audio(const asset_record& asset) -> const audio_importer_meta*
{
    return asset.importer ? asset.importer->safe_cast<audio_importer_meta>() : nullptr;
}

auto as_mesh(const asset_record& asset) -> const mesh_importer_meta*
{
    return asset.importer ? asset.importer->safe_cast<mesh_importer_meta>() : nullptr;
}

auto make_asset_subject(const asset_record& asset) -> subject
{
    return {subject_kind::asset, asset.key, {}, {}, 0};
}

void check_texture_compression(rule_context& context,
                               const asset_record& asset,
                               const texture_importer_meta& importer,
                               const texture_facts& facts,
                               std::uint32_t editor_max,
                               const profile_record& profile)
{
    const auto parameters = asset_compiler::resolve_texture_parameters(importer, get_profile_texture_target(profile));
    if(parameters.compression != texture_importer_meta::compression_quality::none)
    {
        return;
    }
    auto size = get_build_size(facts, editor_max, get_size_pixels(parameters.max_size));
    // The editor compressed it, so its uncompressed texel size is not known: one byte a texel is the least.
    const bool knows_format = !facts.is_compressed;
    const auto bits = knows_format ? facts.bits_per_pixel : 8u;
    const bool has_mips = importer.generate_mipmaps || facts.mips > 1;
    const auto bytes = get_texture_bytes(size, bits, has_mips);
    const auto limit = context.get_threshold(rules::TEXTURE_UNCOMPRESSED, thresholds::MIN_BYTES, profile.platform);
    if(static_cast<std::int64_t>(bytes) < limit)
    {
        return;
    }
    const bool is_lower_bound = size.is_lower_bound || !knows_format;
    const auto described = knows_format ? fmt::format("{} {}", describe_size(size), facts.format) : describe_size(size);
    auto item = context.make(rules::TEXTURE_UNCOMPRESSED,
                             make_asset_subject(asset),
                             fmt::format("Stored uncompressed: {}{} takes {} {} in memory.",
                                         described,
                                         has_mips ? " with mipmaps" : "",
                                         is_lower_bound ? "at least" : "about",
                                         format_bytes(bytes)));
    item.profiles.push_back(profile.name);
    item.fix = fixes::COMPRESS_TEXTURE;
    context.add(std::move(item));
}

void check_texture_side(rule_context& context,
                        const asset_record& asset,
                        const texture_importer_meta& importer,
                        const texture_facts& facts,
                        std::uint32_t editor_max,
                        const profile_record& profile)
{
    const auto parameters = asset_compiler::resolve_texture_parameters(importer, get_profile_texture_target(profile));
    const auto size = get_build_size(facts, editor_max, get_size_pixels(parameters.max_size));
    const auto side = std::max(size.width, size.height);
    const auto limit = context.get_threshold(rules::TEXTURE_OVERSIZED, thresholds::MAX_SIDE, profile.platform);
    if(limit <= 0 || static_cast<std::int64_t>(side) <= limit)
    {
        return;
    }
    auto item = context.make(rules::TEXTURE_OVERSIZED,
                             make_asset_subject(asset),
                             fmt::format("Its largest side in the build is {}{} pixels ({}), over this platform's "
                                         "{}-pixel guideline.",
                                         size.is_lower_bound ? "at least " : "",
                                         side,
                                         describe_size(size),
                                         limit));
    item.profiles.push_back(profile.name);
    item.fix = fixes::LIMIT_TEXTURE_SIZE;
    context.add(std::move(item));
}

/// The keys of the assets that some material refers to.
auto find_material_references(const input& in) -> std::unordered_set<std::string>
{
    std::unordered_set<std::string> used;
    for(const auto& reference : in.references)
    {
        if(ex::is_format<material>(fs::path(reference.from).extension().generic_string()))
        {
            used.insert(reference.to);
        }
    }
    return used;
}

void check_texture_mips(rule_context& context,
                        const asset_record& asset,
                        const texture_importer_meta& importer,
                        const std::unordered_set<std::string>& material_textures)
{
    if(importer.generate_mipmaps || importer.type == texture_importer_meta::texture_type::equirect ||
       material_textures.count(asset.key) == 0)
    {
        return;
    }
    if(asset.facts.texture && asset.facts.texture->mips > 1)
    {
        // The source carries its own mipmaps.
        return;
    }
    auto item = context.make(rules::TEXTURE_NO_MIPS,
                             make_asset_subject(asset),
                             "A material samples it, and it has no mipmaps.");
    item.fix = fixes::GENERATE_MIPS;
    context.add(std::move(item));
}

void check_textures(rule_context& context)
{
    const auto& in = context.get_input();
    const auto material_textures = find_material_references(in);
    for(const auto& asset : in.assets)
    {
        const auto* importer = as_texture(asset);
        if(importer == nullptr)
        {
            continue;
        }
        check_texture_mips(context, asset, *importer, material_textures);
        if(!asset.facts.texture)
        {
            context.count_unread();
            continue;
        }
        const auto editor_max =
            get_size_pixels(asset_compiler::resolve_texture_parameters(*importer, in.editor_texture_target).max_size);
        for(const auto& profile : in.profiles)
        {
            if(!is_shipped_by(profile, asset.key))
            {
                continue;
            }
            check_texture_compression(context, asset, *importer, *asset.facts.texture, editor_max, profile);
            check_texture_side(context, asset, *importer, *asset.facts.texture, editor_max, profile);
        }
    }
}

auto describe_channels(std::uint32_t channels) -> std::string
{
    switch(channels)
    {
        case 1:
            return "mono";
        case 2:
            return "stereo";
        default:
            return fmt::format("{}-channel", channels);
    }
}

void check_decoded_size(rule_context& context, const asset_record& asset, const audio_facts& facts)
{
    for(const auto& profile : context.get_input().profiles)
    {
        if(!is_shipped_by(profile, asset.key))
        {
            continue;
        }
        const auto limit = context.get_threshold(rules::AUDIO_LARGE_DECODED, thresholds::MAX_DECODED_BYTES, profile.platform);
        if(limit <= 0 || static_cast<std::int64_t>(facts.decoded_bytes) <= limit)
        {
            continue;
        }
        auto item = context.make(rules::AUDIO_LARGE_DECODED,
                                 make_asset_subject(asset),
                                 fmt::format("Decodes to {} when it loads: {:.1f} seconds of {} sound at {} Hz, over this "
                                             "platform's {} guideline.",
                                             format_bytes(facts.decoded_bytes),
                                             facts.seconds,
                                             describe_channels(facts.channels),
                                             facts.sample_rate,
                                             format_bytes(static_cast<std::uint64_t>(limit))));
        item.profiles.push_back(profile.name);
        if(facts.channels > 1)
        {
            item.fix = fixes::FORCE_MONO;
        }
        context.add(std::move(item));
    }
}

auto is_lossless_codec(const std::string& codec) -> bool
{
    return codec == "wav" || codec == "flac";
}

void check_audio_compression(rule_context& context, const asset_record& asset, const audio_importer_meta& importer)
{
    const auto codec = asset_compiler::get_audio_codec(asset.source_file);
    if(!is_lossless_codec(codec))
    {
        return;
    }
    fs::error_code ec;
    const auto file_bytes = fs::file_size(asset.source_file, ec);
    if(ec)
    {
        return;
    }
    for(const auto& profile : context.get_input().profiles)
    {
        if(!is_shipped_by(profile, asset.key))
        {
            continue;
        }
        const auto compression =
            asset_compiler::resolve_audio_compression(importer, profile.platform, profile.resolved.assets.audio);
        if(asset_compiler::get_stored_compression(compression, codec) != audio_importer_meta::compression_quality::none)
        {
            continue;
        }
        const auto limit = context.get_threshold(rules::AUDIO_UNCOMPRESSED, thresholds::MIN_FILE_BYTES, profile.platform);
        if(static_cast<std::int64_t>(file_bytes) < limit)
        {
            continue;
        }
        auto item = context.make(rules::AUDIO_UNCOMPRESSED,
                                 make_asset_subject(asset),
                                 fmt::format("Ships as its {} file, {}.", codec == "wav" ? "WAV" : "FLAC", format_bytes(file_bytes)));
        item.profiles.push_back(profile.name);
        item.fix = fixes::COMPRESS_AUDIO;
        context.add(std::move(item));
    }
}

void check_audio(rule_context& context)
{
    for(const auto& asset : context.get_input().assets)
    {
        const auto* importer = as_audio(asset);
        if(importer == nullptr)
        {
            continue;
        }
        check_audio_compression(context, asset, *importer);
        if(!asset.facts.audio)
        {
            context.count_unread();
            continue;
        }
        check_decoded_size(context, asset, *asset.facts.audio);
    }
}

void check_meshes(rule_context& context)
{
    for(const auto& asset : context.get_input().assets)
    {
        const auto* importer = as_mesh(asset);
        if(importer == nullptr || importer->model.generate_lods)
        {
            continue;
        }
        if(!asset.facts.mesh)
        {
            context.count_unread();
            continue;
        }
        const auto& facts = *asset.facts.mesh;
        if(facts.lods > 0)
        {
            continue;
        }
        for(const auto& profile : context.get_input().profiles)
        {
            if(!is_shipped_by(profile, asset.key))
            {
                continue;
            }
            const auto limit = context.get_threshold(rules::MESH_NO_LODS, thresholds::TRIANGLES, profile.platform);
            if(limit <= 0 || static_cast<std::int64_t>(facts.triangles) <= limit)
            {
                continue;
            }
            auto item = context.make(rules::MESH_NO_LODS,
                                     make_asset_subject(asset),
                                     fmt::format("Draws all {} triangles at every distance, over this platform's {}-triangle "
                                                 "guideline.",
                                                 facts.triangles,
                                                 limit));
            item.profiles.push_back(profile.name);
            item.fix = fixes::GENERATE_LODS;
            context.add(std::move(item));
        }
    }
}

/// Whether two files hold the same bytes.
auto have_same_bytes(const fs::path& lhs, const fs::path& rhs) -> bool
{
    std::ifstream left(lhs, std::ios::binary);
    std::ifstream right(rhs, std::ios::binary);
    if(!left.good() || !right.good())
    {
        return false;
    }
    constexpr std::size_t CHUNK = 64 * 1024;
    std::vector<char> left_bytes(CHUNK);
    std::vector<char> right_bytes(CHUNK);
    while(left && right)
    {
        left.read(left_bytes.data(), CHUNK);
        right.read(right_bytes.data(), CHUNK);
        if(left.gcount() != right.gcount() ||
           !std::equal(left_bytes.begin(), left_bytes.begin() + left.gcount(), right_bytes.begin()))
        {
            return false;
        }
    }
    return left.eof() && right.eof();
}

/// Groups of assets of one size whose bytes are the same, each sorted by key.
auto find_duplicate_groups(const std::vector<const asset_record*>& same_size) -> std::vector<std::vector<const asset_record*>>
{
    std::vector<std::vector<const asset_record*>> groups;
    std::vector<bool> grouped(same_size.size(), false);
    for(std::size_t first = 0; first < same_size.size(); ++first)
    {
        if(grouped[first])
        {
            continue;
        }
        std::vector<const asset_record*> group{same_size[first]};
        for(std::size_t other = first + 1; other < same_size.size(); ++other)
        {
            if(!grouped[other] && have_same_bytes(same_size[first]->source_file, same_size[other]->source_file))
            {
                grouped[other] = true;
                group.push_back(same_size[other]);
            }
        }
        if(group.size() > 1)
        {
            groups.push_back(std::move(group));
        }
    }
    return groups;
}

void check_duplicates(rule_context& context)
{
    // Duplicates are judged once for the project; desktop's threshold applies.
    const auto limit = context.get_threshold(rules::DUPLICATE_FILES, thresholds::MIN_FILE_BYTES, target_platform::desktop);
    std::map<std::uintmax_t, std::vector<const asset_record*>> by_size;
    for(const auto& asset : context.get_input().assets)
    {
        if(!asset.importer || asset.is_generated)
        {
            continue;
        }
        fs::error_code ec;
        const auto size = fs::file_size(asset.source_file, ec);
        if(!ec && static_cast<std::int64_t>(size) >= limit)
        {
            by_size[size].push_back(&asset);
        }
    }
    for(const auto& [size, assets] : by_size)
    {
        if(assets.size() < 2)
        {
            continue;
        }
        for(auto& group : find_duplicate_groups(assets))
        {
            std::sort(group.begin(),
                      group.end(),
                      [](const asset_record* lhs, const asset_record* rhs)
                      {
                          return lhs->key < rhs->key;
                      });
            auto item = context.make(rules::DUPLICATE_FILES,
                                     make_asset_subject(*group.front()),
                                     fmt::format("{} other {} the same {} of data, byte for byte.",
                                                 group.size() - 1,
                                                 group.size() == 2 ? "file holds" : "files hold",
                                                 format_bytes(size)));
            for(std::size_t index = 1; index < group.size(); ++index)
            {
                item.details.push_back("The same file: " + group[index]->key + ".");
            }
            context.add(std::move(item));
        }
    }
}

} // namespace

void check_assets(rule_context& context)
{
    check_textures(context);
    check_audio(context);
    check_meshes(context);
    if(context.is_enabled(rules::DUPLICATE_FILES))
    {
        check_duplicates(context);
    }
}

} // namespace unravel::audit::detail
