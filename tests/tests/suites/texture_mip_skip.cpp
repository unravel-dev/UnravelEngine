/*
 * Validation suite for creating a texture without its top mips from a parsed image
 * (engine/core/graphics/utils/bgfx_utils.h: imageGetTextureInfo, imageGetSkippedRange,
 * imageCopySkipped, createTextureFromImage) and for the level that keeps a texture's tail
 * (gfx::residency::calc_tail_level).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "texture mip skip"
 *
 * The creation cases bring up bgfx on the Noop renderer: handles are real and, in Debug, bgfx checks
 * that the memory handed to it matches the texture's storage size.
 *
 * What it pins:
 *   - at skip N a texture has the image's size shifted by N and N fewer mips, for RGBA8, BC1 and BC3
 *     images, single 2D, 2D arrays and cubes;
 *   - the bytes handed to bgfx are mips N and below of every side, side after side;
 *   - a single side is one range of the image (no copy), as is every side at skip 0; more sides at a
 *     skip are gathered;
 *   - a skip that leaves no mip is refused everywhere, and a short destination copies nothing;
 *   - a texture made from a range keeps the image alive until bgfx has uploaded it, and only then;
 *   - a texture loaded from a KTX2 file while eviction runs (the suite activates the engine's
 *     registry with a budget) reports its sizes per level and its tail, goes to its tail with a live
 *     handle, serves sampled reads there, comes back whole for a full read and leaves the registry
 *     when it goes;
 *   - a texture loads whole while that keeps the GPU under the soft budget, at its tail past it, with
 *     no GPU copy when not even the tail fits under the hard limit (a sampled draw then creates the
 *     tail), whole under the hard limit with paging off, and a manual budget judges the engine's own
 *     bytes, not the device's;
 *   - a texture the GPU writes (blit destination) stays resident and untracked, loaded from a file or
 *     made with contents, while a texture made with contents registers and hands its admitted bytes over;
 *   - a texture made with contents is created even past the budget, so bgfx takes and frees the memory
 *     handed to it (its release callback runs), and it comes back from its own copy after an eviction;
 *   - with paging off, making room for a render target takes nothing; with paging on it reduces idle textures;
 *   - a thread reading a texture's handle while the API thread changes its level always gets a live handle;
 *   - the tail level keeps the mips at or below the tail size, never drops a texture that is all
 *     tail, keeps one mip, and steps back where a block-compressed top mip would stop being whole
 *     blocks.
 */

#include "../tests.h"

#include <graphics/eviction_registry.h>
#include <graphics/graphics.h>
#include <graphics/residency_planner.h>
#include <graphics/texture.h>
#include <graphics/utils/bgfx_utils.h>

#include <bx/allocator.h>
#include <bx/readerwriter.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace unravel::test;

namespace
{

auto get_test_allocator() -> bx::AllocatorI*
{
    static bx::DefaultAllocator allocator;
    return &allocator;
}

/// A mipped image of @p format, every byte of every mip of every side set from its position.
auto make_image(bimg::TextureFormat::Enum format, std::uint32_t size, std::uint16_t layers, bool cube) -> ImagePtr
{
    bimg::ImageContainer* image = bimg::imageAlloc(get_test_allocator(), format, size, size, 1, layers, cube, true);
    if(image == nullptr)
    {
        return {};
    }
    const std::uint16_t sides = static_cast<std::uint16_t>(image->m_numLayers * (image->m_cubeMap ? 6 : 1));
    for(std::uint16_t side = 0; side < sides; ++side)
    {
        for(std::uint8_t lod = 0; lod < image->m_numMips; ++lod)
        {
            bimg::ImageMip mip;
            if(bimg::imageGetRawData(*image, side, lod, image->m_data, image->m_size, mip))
            {
                auto* bytes = const_cast<std::uint8_t*>(mip.m_data);
                for(std::uint32_t i = 0; i < mip.m_size; ++i)
                {
                    bytes[i] = static_cast<std::uint8_t>(side * 31 + lod * 7 + i);
                }
            }
        }
    }
    return ImagePtr(image, bimg::imageFree);
}

/// Mips @p skip and below of every side of @p image, side after side, read mip by mip.
auto expected_bytes(const bimg::ImageContainer& image, std::uint8_t skip) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> bytes;
    const std::uint16_t sides = static_cast<std::uint16_t>(image.m_numLayers * (image.m_cubeMap ? 6 : 1));
    for(std::uint16_t side = 0; side < sides; ++side)
    {
        for(std::uint8_t lod = skip; lod < image.m_numMips; ++lod)
        {
            bimg::ImageMip mip;
            if(bimg::imageGetRawData(image, side, lod, image.m_data, image.m_size, mip))
            {
                bytes.insert(bytes.end(), mip.m_data, mip.m_data + mip.m_size);
            }
        }
    }
    return bytes;
}

struct image_case
{
    bimg::TextureFormat::Enum format;
    std::uint16_t layers;
    bool cube;
    const char* name;
};

auto get_image_cases() -> std::vector<image_case>
{
    return {
        {bimg::TextureFormat::RGBA8, 1, false, "RGBA8 2D"},
        {bimg::TextureFormat::BC1, 1, false, "BC1 2D"},
        {bimg::TextureFormat::BC3, 1, false, "BC3 2D"},
        {bimg::TextureFormat::BC3, 3, false, "BC3 2D array"},
        {bimg::TextureFormat::RGBA8, 1, true, "RGBA8 cube"},
        {bimg::TextureFormat::BC1, 2, true, "BC1 cube array"},
    };
}

void check_skip(const image_case& c, const ImagePtr& image, std::uint8_t skip)
{
    const std::string what = std::string(c.name) + " at skip " + std::to_string(skip);
    const bool is_single_side = c.layers == 1 && !c.cube;

    bgfx::TextureInfo info{};
    require(imageGetTextureInfo(*image, skip, info), what + ": has a texture info");
    check_eq(info.width, std::max<std::uint32_t>(1, image->m_width >> skip), what + ": width");
    check_eq(info.height, std::max<std::uint32_t>(1, image->m_height >> skip), what + ": height");
    check_eq(info.numMips, std::size_t(image->m_numMips - skip), what + ": mip count");
    check_eq(info.numLayers, c.layers, what + ": layers");
    check(info.cubeMap == c.cube, what + ": cube flag");

    const auto expected = expected_bytes(*image, skip);
    check_eq(imageGetSkippedSize(*image, skip), expected.size(), what + ": size matches the mips it keeps");
    check_eq(info.storageSize, expected.size(), what + ": bgfx's storage size matches the mips it keeps");

    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    const bool has_range = imageGetSkippedRange(*image, skip, offset, size);
    if(is_single_side || skip == 0)
    {
        require(has_range, what + ": is one range of the image");
        check_eq(size, expected.size(), what + ": range size");
        const auto* base = static_cast<const std::uint8_t*>(image->m_data) + offset;
        check(std::memcmp(base, expected.data(), expected.size()) == 0, what + ": range holds the kept mips");
    }
    else
    {
        check(!has_range, what + ": is not one range");
    }

    std::vector<std::uint8_t> gathered(expected.size());
    check_eq(imageCopySkipped(*image, skip, gathered.data(), static_cast<std::uint32_t>(gathered.size())),
             expected.size(),
             what + ": copy writes every kept byte");
    check(gathered == expected, what + ": copy holds the kept mips side after side");
    std::vector<std::uint8_t> short_dst(expected.size() - 1);
    check_eq(imageCopySkipped(*image, skip, short_dst.data(), static_cast<std::uint32_t>(short_dst.size())),
             0,
             what + ": a short destination gets nothing");
}

void test_skipped_layout_matches_the_kept_mips()
{
    for(const auto& c : get_image_cases())
    {
        const ImagePtr image = make_image(c.format, 64, c.layers, c.cube);
        require(image != nullptr, std::string(c.name) + ": image allocates");
        require(image->m_numMips == 7, std::string(c.name) + ": a 64 texture has 7 mips");
        for(std::uint8_t skip = 0; skip < image->m_numMips; ++skip)
        {
            check_skip(c, image, skip);
        }
    }
}

void test_skip_past_the_chain_is_refused()
{
    const ImagePtr image = make_image(bimg::TextureFormat::BC3, 64, 1, false);
    require(image != nullptr, "image allocates");
    bgfx::TextureInfo info{};
    check(!imageGetTextureInfo(*image, image->m_numMips, info), "no info past the last mip");
    check_eq(imageGetSkippedSize(*image, image->m_numMips), 0, "no size past the last mip");
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    check(!imageGetSkippedRange(*image, image->m_numMips, offset, size), "no range past the last mip");
    std::vector<std::uint8_t> dst(image->m_size);
    check_eq(imageCopySkipped(*image, image->m_numMips, dst.data(), image->m_size), 0, "no copy past the last mip");
}

void test_textures_create_at_every_skip()
{
    for(const auto& c : get_image_cases())
    {
        const ImagePtr image = make_image(c.format, 64, c.layers, c.cube);
        require(image != nullptr, std::string(c.name) + ": image allocates");
        for(std::uint8_t skip = 0; skip < image->m_numMips; ++skip)
        {
            const std::string what = std::string(c.name) + " at skip " + std::to_string(skip);
            const bgfx::TextureHandle handle = createTextureFromImage(image, BGFX_TEXTURE_NONE, skip);
            check(bgfx::isValid(handle), what + ": creates");
            if(bgfx::isValid(handle))
            {
                bgfx::destroy(handle);
            }
        }
        check(!bgfx::isValid(createTextureFromImage(image, BGFX_TEXTURE_NONE, image->m_numMips)),
              std::string(c.name) + ": a skip past the chain creates nothing");
        gfx::frames(2);
    }
}

void test_a_referenced_image_lives_until_uploaded()
{
    const ImagePtr image = make_image(bimg::TextureFormat::BC3, 256, 1, false);
    require(image != nullptr, "image allocates");
    gfx::frames(2);
    const bgfx::TextureHandle handle = createTextureFromImage(image, BGFX_TEXTURE_NONE, 2);
    require(bgfx::isValid(handle), "creates at skip 2");
    check_eq(static_cast<std::size_t>(image.use_count()), 2, "bgfx holds the image while it uploads");
    gfx::frames(2);
    check_eq(static_cast<std::size_t>(image.use_count()), 1, "bgfx lets the image go once uploaded");
    bgfx::destroy(handle);

    const ImagePtr array = make_image(bimg::TextureFormat::BC3, 256, 2, false);
    require(array != nullptr, "array image allocates");
    const bgfx::TextureHandle gathered = createTextureFromImage(array, BGFX_TEXTURE_NONE, 2);
    require(bgfx::isValid(gathered), "array creates at skip 2");
    check_eq(static_cast<std::size_t>(array.use_count()), 1, "a gathered copy holds no reference");
    bgfx::destroy(gathered);
    gfx::frames(2);
}

/// The bytes of a KTX2 file holding @p image.
auto write_ktx2(const bimg::ImageContainer& image) -> std::vector<std::uint8_t>
{
    bx::MemoryBlock block(get_test_allocator());
    bx::MemoryWriter writer(&block);
    bx::Error err;
    auto copy = image;
    bimg::imageWriteKtx2(&writer, copy, image.m_data, image.m_size, &err);
    const auto size = static_cast<std::size_t>(writer.seek(0, bx::Whence::Current));
    const auto* bytes = static_cast<const std::uint8_t*>(block.more(0));
    return err.isOk() ? std::vector<std::uint8_t>(bytes, bytes + size) : std::vector<std::uint8_t>{};
}

/// Turns the engine's registry off when a case that turned it on ends, passed or not.
struct registry_shutdown_guard
{
    registry_shutdown_guard() = default;
    registry_shutdown_guard(const registry_shutdown_guard&) = delete;
    auto operator=(const registry_shutdown_guard&) -> registry_shutdown_guard& = delete;
    registry_shutdown_guard(registry_shutdown_guard&&) = delete;
    auto operator=(registry_shutdown_guard&&) -> registry_shutdown_guard& = delete;
    ~registry_shutdown_guard()
    {
        gfx::eviction_registry::instance().shutdown();
    }
};

void test_loaded_texture_changes_levels()
{
    auto& registry = gfx::eviction_registry::instance();
    const registry_shutdown_guard guard;
    gfx::eviction::budget_state budget;
    budget.hard_limit_bytes = std::uint64_t(1) << 32;
    budget.soft_budget_bytes = budget.hard_limit_bytes;
    budget.target_bytes = budget.hard_limit_bytes;
    registry.activate({}, budget, 0);
    gfx::eviction::residency_options options;
    options.tail_size = 64;
    registry.set_residency_options(options);

    const ImagePtr image = make_image(bimg::TextureFormat::BC3, 256, 1, false);
    require(image != nullptr, "image allocates");
    const auto file = write_ktx2(*image);
    require(!file.empty(), "the image writes as KTX2");
    {
        gfx::texture texture(file.data(), static_cast<std::uint32_t>(file.size()), BGFX_TEXTURE_NONE, 0, nullptr, "ktx2 test");
        require(texture.is_valid(), "the texture loads with eviction on");
        check(texture.get_evict_state() == gfx::evict_state::resident, "it registers full");
        check_eq(texture.info.width, 256, "info describes the full texture");
        check_eq(texture.get_tail_level(64), 2, "a 256 texture keeps its mips from 64 down");
        check(texture.gpu_size_at(0) > texture.gpu_size_at(1) && texture.gpu_size_at(1) > texture.gpu_size_at(2),
              "each level is smaller");
        check_eq(texture.gpu_size_at(texture.info.numMips), 0, "a level past the chain has no size");
        check_eq(registry.get_stats().resident_bytes, texture.gpu_size_at(0), "the registry counts its full size");

        const auto full_handle = texture.native_handle();
        registry.evict_all();
        check(texture.get_evict_state() == gfx::evict_state::reduced && texture.get_level() == 2,
              "evicting it keeps its tail");
        const auto tail_handle = texture.native_handle(gfx::texture_read::any_level);
        check(bgfx::isValid(tail_handle) && tail_handle.idx != full_handle.idx, "the tail is a new live handle");
        check(texture.get_evict_state() == gfx::evict_state::reduced, "a sampled read uses the tail as it is");
        check_eq(registry.get_stats().reduced_bytes, texture.gpu_size_at(2), "the registry counts the tail");
        check_eq(texture.info.width, 256, "info still describes the full texture");

        const auto raised = texture.native_handle(gfx::texture_read::full);
        check(texture.get_evict_state() == gfx::evict_state::resident && bgfx::isValid(raised),
              "a full read brings it back whole");
        gfx::frames(2);
    }
    {
        // ASTC 6x6 pads 1024 to 1026, whose halves are not whole blocks: only BC is held to blocks.
        const ImagePtr astc = make_image(bimg::TextureFormat::ASTC6x6, 1024, 1, false);
        require(astc != nullptr, "an ASTC 6x6 image allocates");
        const auto astc_file = write_ktx2(*astc);
        require(!astc_file.empty(), "the ASTC image writes as KTX2");
        gfx::texture texture(astc_file.data(), static_cast<std::uint32_t>(astc_file.size()), BGFX_TEXTURE_NONE, 0, nullptr, "astc test");
        require(texture.is_valid(), "the ASTC texture loads");
        check_eq(texture.get_tail_level(64), 4, "an ASTC 6x6 texture keeps a tail");
        gfx::frames(2);
    }
    check_eq(registry.get_stats().registered_count, 0, "a destroyed texture leaves the registry");
    gfx::frames(2);
}

/// Turns the engine's registry on with @p budget, tails of 64, and loads over the soft budget starting
/// at their tails while @p paging_enabled.
void activate_engine_registry(const gfx::eviction::budget_state& budget, bool paging_enabled)
{
    auto& registry = gfx::eviction_registry::instance();
    registry.activate({}, budget, 0);
    gfx::eviction::residency_options options;
    options.tail_size = 64;
    options.paging_enabled = paging_enabled;
    registry.set_residency_options(options);
}

/// A budget of @p soft bytes under a hard limit of @p hard, with no safety margin.
auto make_budget(std::uint64_t soft, std::uint64_t hard, bool counts_engine_bytes) -> gfx::eviction::budget_state
{
    gfx::eviction::budget_state budget;
    budget.hard_limit_bytes = hard;
    budget.soft_budget_bytes = soft;
    budget.target_bytes = soft;
    budget.counts_engine_bytes = counts_engine_bytes;
    return budget;
}

void test_loads_start_where_the_budget_allows()
{
    auto& registry = gfx::eviction_registry::instance();
    const registry_shutdown_guard guard;
    const ImagePtr image = make_image(bimg::TextureFormat::BC3, 256, 1, false);
    require(image != nullptr, "image allocates");
    const auto file = write_ktx2(*image);
    require(!file.empty(), "the image writes as KTX2");
    const auto load = [&](const char* name)
    {
        return std::make_unique<gfx::texture>(file.data(), static_cast<std::uint32_t>(file.size()), BGFX_TEXTURE_NONE, 0, nullptr, name);
    };
    // Lets the creates so far leave the frames whose memory bgfx's stats may not show yet.
    const auto settle = []
    {
        gfx::frames(4);
    };
    constexpr std::uint64_t k_roomy = std::uint64_t(1) << 32;

    activate_engine_registry(make_budget(k_roomy, k_roomy, false), true);
    std::uint64_t full = 0;
    std::uint64_t tail = 0;
    {
        const auto probe = load("roomy load");
        require(probe->is_valid(), "the texture loads");
        check(probe->get_evict_state() == gfx::evict_state::resident, "with room a texture loads whole");
        full = probe->gpu_size_at(0);
        tail = probe->gpu_size_at(2);
    }
    require(full > tail && tail > 0, "the full texture is larger than its tail");
    settle();

    registry.set_budget(make_budget(full - 1, k_roomy, false), 0);
    {
        const std::uint64_t reductions = registry.get_stats().total_reductions;
        const auto texture = load("over soft");
        check(texture->is_valid() && texture->get_evict_state() == gfx::evict_state::reduced && texture->get_level() == 2,
              "past the soft budget a texture loads at its tail");
        check_eq(registry.get_stats().total_reductions, reductions, "it starts there: no drop took its top mips");
        check_eq(registry.peek_admitted_bytes(), 0, "its admission is handed over when it registers");
        const auto raised = texture->native_handle(gfx::texture_read::full);
        check(bgfx::isValid(raised) && texture->get_evict_state() == gfx::evict_state::resident,
              "a full read brings it back whole");
    }
    settle();

    registry.set_budget(make_budget(tail - 1, tail - 1, false), 0);
    {
        const auto texture = load("no room");
        check(!texture->has_gpu_copy() && texture->get_evict_state() == gfx::evict_state::evicted,
              "with no room for its tail a texture loads without a GPU copy");
        check(texture->is_valid(), "a texture without its GPU copy is still valid: a draw creates the copy");
        const auto sampled = texture->native_handle(gfx::texture_read::any_level);
        check(bgfx::isValid(sampled) && texture->get_evict_state() == gfx::evict_state::reduced && texture->get_level() == 2,
              "a sampled draw creates its tail");
    }
    settle();

    activate_engine_registry(make_budget(full - 1, k_roomy, false), false);
    {
        const auto texture = load("paging off");
        check(texture->get_evict_state() == gfx::evict_state::resident,
              "with paging off a texture loads whole under the hard limit");
    }
    settle();

    activate_engine_registry(make_budget(full + tail, full + tail, true), true);
    {
        const auto first = load("first of a manual budget");
        check(first->get_evict_state() == gfx::evict_state::resident, "a manual budget with room loads whole");
        settle();
        const auto second = load("second of a manual budget");
        check(second->get_evict_state() == gfx::evict_state::reduced,
              "a manual budget counts the engine's bytes: the second texture starts at its tail");
        registry.set_budget(make_budget(full + tail, full + tail, false), 0);
        const auto third = load("third of a device budget");
        check(third->get_evict_state() == gfx::evict_state::resident,
              "the device's usage, which the test backend reports empty, has room for it whole");
    }
    settle();
}

void test_gpu_written_textures_stay_untracked()
{
    auto& registry = gfx::eviction_registry::instance();
    const registry_shutdown_guard guard;
    constexpr std::uint64_t k_roomy = std::uint64_t(1) << 32;
    activate_engine_registry(make_budget(k_roomy, k_roomy, false), true);
    const ImagePtr image = make_image(bimg::TextureFormat::RGBA8, 64, 1, false);
    require(image != nullptr, "image allocates");
    const auto file = write_ktx2(*image);
    require(!file.empty(), "the image writes as KTX2");
    {
        gfx::texture loaded(file.data(), static_cast<std::uint32_t>(file.size()), BGFX_TEXTURE_BLIT_DST, 0, nullptr, "blit target");
        check(loaded.is_valid(), "a loaded blit destination is created");
        check(loaded.get_evict_class() == gfx::evict_class::non_evictable, "and stays out of the eviction system");

        const std::uint32_t bytes = 64 * 64 * 4;
        const bgfx::Memory* written = bgfx::alloc(bytes);
        std::memset(written->data, 0x7F, bytes);
        gfx::texture made(64, 64, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_BLIT_DST, written);
        check(made.is_valid() && made.get_evict_class() == gfx::evict_class::non_evictable,
              "a blit destination made with contents stays out too");

        const bgfx::Memory* plain_contents = bgfx::alloc(bytes);
        std::memset(plain_contents->data, 0x7F, bytes);
        gfx::texture plain(64, 64, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE, plain_contents);
        check(plain.is_valid() && plain.get_evict_class() == gfx::evict_class::evictable,
              "a texture made with contents is tracked");
        check_eq(registry.peek_admitted_bytes(), 0, "and its admitted bytes are handed over");
        check_eq(registry.get_stats().registered_count, 1, "only that one is registered");
    }
    gfx::frames(2);
}

void test_contents_past_the_budget_are_taken()
{
    auto& registry = gfx::eviction_registry::instance();
    const registry_shutdown_guard guard;
    constexpr std::uint64_t k_tiny = 1024;
    activate_engine_registry(make_budget(k_tiny, k_tiny, false), true);
    const std::uint32_t bytes = 64 * 64 * 4;
    std::vector<std::uint8_t> contents(bytes, 0x40);
    bool released = false;
    const bgfx::Memory* mem = bgfx::makeRef(
        contents.data(),
        bytes,
        [](void* /*ptr*/, void* user)
        {
            *static_cast<bool*>(user) = true;
        },
        &released);
    {
        gfx::texture texture(64, 64, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE, mem);
        check(texture.has_gpu_copy() && texture.get_evict_state() == gfx::evict_state::resident,
              "a texture made with contents is created past the budget");
        check_eq(registry.peek_admitted_bytes(), 0, "and holds no admission");
        gfx::frames(2);
        check(released, "bgfx took the memory handed to it and freed it, its release callback included");
        contents.assign(bytes, 0);
        registry.evict_all();
        check(texture.get_evict_state() == gfx::evict_state::evicted, "it can be evicted");
        check(!texture.has_gpu_copy() && texture.is_valid(),
              "evicted, it has no GPU copy and stays valid, so code that checks before it draws still reads it");
        const auto handle = texture.native_handle();
        check(bgfx::isValid(handle) && texture.get_evict_state() == gfx::evict_state::resident,
              "and a draw brings it back from its own copy of the contents");
    }
    gfx::frames(2);
}

void test_paging_off_takes_nothing_for_render_targets()
{
    auto& registry = gfx::eviction_registry::instance();
    const registry_shutdown_guard guard;
    const std::uint64_t frame = gfx::detail::eviction_frame();
    constexpr std::uint64_t k_roomy = std::uint64_t(1) << 32;
    activate_engine_registry(make_budget(k_roomy, k_roomy, false), false);
    const ImagePtr image = make_image(bimg::TextureFormat::BC3, 256, 1, false);
    require(image != nullptr, "image allocates");
    const auto file = write_ktx2(*image);
    require(!file.empty(), "the image writes as KTX2");
    {
        gfx::texture texture(file.data(), static_cast<std::uint32_t>(file.size()), BGFX_TEXTURE_NONE, 0, nullptr, "idle");
        require(texture.get_evict_state() == gfx::evict_state::resident, "the texture loads whole");
        gfx::eviction::set_frame(frame + 1000);
        // Any render target is over a soft budget of one byte, under a roomy hard limit.
        registry.set_budget(make_budget(1, k_roomy, false), 0);

        const auto off = gfx::eviction::reclaim_for(4096, gfx::eviction::reclaim_kind::immediate);
        check(off == gfx::eviction::reclaim_result::headroom, "with paging off a render target that fits has room");
        check(texture.get_evict_state() == gfx::evict_state::resident, "and nothing is taken from an idle texture");

        activate_engine_registry(make_budget(1, k_roomy, false), true);
        gfx::eviction::reclaim_for(4096, gfx::eviction::reclaim_kind::immediate);
        check(texture.get_evict_state() == gfx::evict_state::reduced,
              "with paging on making room for it reduces the idle texture");
    }
    gfx::eviction::set_frame(frame);
    gfx::frames(2);
}

void test_handles_read_on_another_thread_stay_live()
{
    auto& registry = gfx::eviction_registry::instance();
    const registry_shutdown_guard guard;
    constexpr std::uint64_t k_roomy = std::uint64_t(1) << 32;
    activate_engine_registry(make_budget(k_roomy, k_roomy, false), true);
    const ImagePtr image = make_image(bimg::TextureFormat::BC3, 256, 1, false);
    require(image != nullptr, "image allocates");
    const auto file = write_ktx2(*image);
    require(!file.empty(), "the image writes as KTX2");
    {
        gfx::texture texture(file.data(), static_cast<std::uint32_t>(file.size()), BGFX_TEXTURE_NONE, 0, nullptr, "shared");
        require(texture.is_valid(), "the texture loads");
        std::atomic<bool> stop{false};
        std::atomic<std::uint64_t> reads{0};
        std::atomic<std::uint64_t> dead{0};
        std::thread reader(
            [&]
            {
                while(!stop.load())
                {
                    const auto handle = texture.native_handle(gfx::texture_read::any_level);
                    dead.fetch_add(bgfx::isValid(handle) ? 0 : 1);
                    reads.fetch_add(1);
                }
            });
        // Level changes go on until the other thread has read through many of them, however the two are
        // scheduled.
        for(int i = 0; i < 50 || (reads.load() < 1000 && i < 100000); ++i)
        {
            registry.evict_all();
            texture.native_handle(gfx::texture_read::full);
            gfx::frames(1);
        }
        stop.store(true);
        reader.join();
        check(reads.load() > 0, "the other thread read while the levels changed");
        check_eq(dead.load(), 0, "every handle it read was a live copy or the fallback");
    }
    gfx::frames(2);
}

void test_tail_level()
{
    using gfx::residency::calc_tail_level;
    check_eq(calc_tail_level(1024, 1024, 11, 64), 4, "1024 keeps mips from 64 down");
    check_eq(calc_tail_level(1024, 1024, 11, 128), 3, "a 128 tail keeps one more mip");
    check_eq(calc_tail_level(1024, 512, 11, 64), 4, "the longer side decides");
    check_eq(calc_tail_level(4096, 4096, 13, 64), 6, "4096 keeps mips from 64 down");
    check_eq(calc_tail_level(64, 64, 7, 64), 0, "a texture that is all tail has no tail level");
    check_eq(calc_tail_level(128, 128, 8, 64), 1, "a texture one mip above the tail drops one");
    check_eq(calc_tail_level(1024, 1024, 3, 64), 0, "a short chain that never reaches the tail has none");
    check_eq(calc_tail_level(1024, 1024, 1, 64), 0, "a texture without mips has none");
    check_eq(calc_tail_level(1024, 1024, 11, 0), 0, "a tail size of 0 keeps no tail");
    check_eq(calc_tail_level(8, 8, 4, 0), 0, "a tail size of 0 keeps no tail at any size");
    check_eq(calc_tail_level(1024, 1024, 11, 1), 10, "a 1 pixel tail keeps the last mip");
    check_eq(calc_tail_level(100, 100, 7, 64, 4, 4), 0, "100 halves to 50, not whole 4x4 blocks: no tail");
    check_eq(calc_tail_level(96, 96, 7, 8, 4, 4), 3, "96 at a tail of 8 steps back to 12, whole blocks");
    check_eq(calc_tail_level(100, 100, 7, 64), 1, "without blocks 100 halves to 50");
    check_eq(calc_tail_level(1023, 1023, 10, 64, 4, 4), 4, "a top mip that was never whole blocks is not held to them");
    check_eq(calc_tail_level(1026, 1026, 11, 64), 4, "without a block rule a padded 1026 keeps its tail");
}

} // namespace

void run_texture_mip_skip_suite(rtti::context& /*ctx*/)
{
    RUN_CASE(test_skipped_layout_matches_the_kept_mips());
    RUN_CASE(test_skip_past_the_chain_is_refused());
    RUN_CASE(test_tail_level());

    bgfx::Init init;
    init.type = bgfx::RendererType::Noop;
    if(!gfx::init(init))
    {
        check(false, "bgfx initialises on the Noop renderer");
        gfx::shutdown();
        return;
    }
    RUN_CASE(test_textures_create_at_every_skip());
    RUN_CASE(test_a_referenced_image_lives_until_uploaded());
    RUN_CASE(test_loaded_texture_changes_levels());
    RUN_CASE(test_loads_start_where_the_budget_allows());
    RUN_CASE(test_gpu_written_textures_stay_untracked());
    RUN_CASE(test_contents_past_the_budget_are_taken());
    RUN_CASE(test_paging_off_takes_nothing_for_render_targets());
    RUN_CASE(test_handles_read_on_another_thread_stay_live());
    gfx::shutdown();
}

REGISTER_TEST_SUITE("texture mip skip", run_texture_mip_skip_suite)
