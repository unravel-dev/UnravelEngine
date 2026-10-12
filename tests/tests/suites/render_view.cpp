/*
 * Validation suite for texture descs, gfx::render_view's texture access and its transient
 * texture pool.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "render view"
 *
 * The suite brings up bgfx on the Noop renderer for its own duration: texture handles are
 * real, nothing reaches a device.
 *
 * What it pins:
 *   - a texture built from a desc reports that desc back, and the size-based constructors
 *     build the same textures through it (2D array, 3D, cube);
 *   - desc comparison is exact where bgfx's own info is not: a 1x1 texture asked for with mips
 *     has one level but still matches its desc, and a one-slice volume stays a volume;
 *   - tex_acquire(id, desc) builds once, rebuilds only when the desc changes, and runs its
 *     creation callback only on a (re)build; tex_find() sees what it built;
 *   - a texture stored in a slot is found and kept by an equal desc; framebuffers are found, and
 *     their attachment check catches a texture rebuilt at the same size;
 *   - a transient texture is lent to one borrower at a time and handed back out once dropped;
 *   - free transient textures expire after the idle window, held ones never do.
 */

#include "../tests.h"

#include <graphics/graphics.h>
#include <graphics/render_view.h>
#include <graphics/texture_pool.h>

#include <cstdio>
#include <memory>
#include <string>

using namespace unravel;

namespace
{

int g_render_view_checks = 0;
int g_render_view_failures = 0;

void render_view_check(bool condition, const std::string& what)
{
    ++g_render_view_checks;
    if(!condition)
    {
        ++g_render_view_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

/// Small enough to be cheap, large enough for a real mip chain.
constexpr std::uint32_t TEST_TEXTURE_SIZE = 64;

/// A plain compute target, the most common desc in the pipeline.
auto make_compute_desc() -> gfx::texture_desc
{
    return gfx::texture_desc{.size = {TEST_TEXTURE_SIZE, TEST_TEXTURE_SIZE},
                             .format = bgfx::TextureFormat::RGBA16F,
                             .flags = BGFX_TEXTURE_COMPUTE_WRITE};
}

void test_desc_round_trips_through_the_texture()
{
    const gfx::texture_desc desc{.size = {TEST_TEXTURE_SIZE, TEST_TEXTURE_SIZE / 2},
                                 .format = bgfx::TextureFormat::R16F,
                                 .flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP,
                                 .has_mips = true,
                                 .layers = 3};
    const gfx::texture tex(desc);
    render_view_check(tex.is_valid(), "desc texture: created");
    render_view_check(tex.get_desc() == desc, "desc texture: reports its desc");
    render_view_check(tex.info.width == TEST_TEXTURE_SIZE && tex.info.height == TEST_TEXTURE_SIZE / 2,
                      "desc texture: info size");
    render_view_check(tex.info.numLayers == 3 && tex.info.numMips > 1, "desc texture: info layers and mips");
    render_view_check(tex.info.format == bgfx::TextureFormat::R16F && tex.flags == desc.flags,
                      "desc texture: info format and flags");
}

void test_size_constructors_build_through_the_desc()
{
    const std::uint16_t size = TEST_TEXTURE_SIZE;
    const gfx::texture plane(size, size, false, 2, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT);
    const gfx::texture_desc plane_desc{.size = {size, size},
                                       .format = bgfx::TextureFormat::RGBA8,
                                       .flags = BGFX_TEXTURE_RT,
                                       .layers = 2};
    render_view_check(plane.get_desc() == plane_desc, "2D constructor: desc");
    const gfx::texture volume(size, size, 1, false, bgfx::TextureFormat::R8, BGFX_TEXTURE_COMPUTE_WRITE);
    render_view_check(volume.get_desc().kind == gfx::texture_kind::tex_3d, "3D constructor: one slice stays a volume");
    render_view_check(volume.get_desc().depth == 1 && volume.info.depth == 1, "3D constructor: depth");
    const gfx::texture cube(size, true, 1, bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT);
    render_view_check(cube.get_desc().kind == gfx::texture_kind::tex_cube, "cube constructor: kind");
    render_view_check(cube.get_desc().size == usize32_t(size, size), "cube constructor: square size");
    render_view_check(cube.info.cubeMap && cube.info.depth == 1, "cube constructor: info describes faces");
}

void test_desc_comparison_is_exact()
{
    const gfx::texture_desc single_texel{.size = {1, 1}, .format = bgfx::TextureFormat::RGBA32F, .has_mips = true};
    const auto tex = std::make_shared<gfx::texture>(single_texel);
    render_view_check(tex->info.numMips == 1, "1x1 with mips: bgfx gives one level");
    render_view_check(!gfx::needs_recreate(tex, single_texel), "1x1 with mips: still matches its desc");
    auto other_format = single_texel;
    other_format.format = bgfx::TextureFormat::RGBA16F;
    render_view_check(gfx::needs_recreate(tex, other_format), "needs_recreate: format change");
    auto other_flags = single_texel;
    other_flags.flags = BGFX_TEXTURE_COMPUTE_WRITE;
    render_view_check(gfx::needs_recreate(tex, other_flags), "needs_recreate: flags change");
    render_view_check(gfx::needs_recreate(nullptr, single_texel), "needs_recreate: missing texture");
}

void test_acquire_rebuilds_only_on_desc_change()
{
    gfx::render_view rview;
    int created = 0;
    const auto count_creation = [&](const gfx::texture::ptr& /*tex*/)
    {
        ++created;
    };
    const auto desc = make_compute_desc();
    render_view_check(rview.tex_find("TARGET") == nullptr, "tex_find: missing key is null");
    const gfx::texture::ptr first = rview.tex_acquire("TARGET", desc, count_creation);
    render_view_check(first && first->get_desc() == desc && created == 1, "tex_acquire: builds from the desc");
    const gfx::texture::ptr again = rview.tex_acquire("TARGET", desc, count_creation);
    render_view_check(again == first && created == 1, "tex_acquire: same desc keeps the texture");
    render_view_check(rview.tex_find("TARGET") == first, "tex_find: sees the acquired texture");
    auto resized = desc;
    resized.size = {TEST_TEXTURE_SIZE / 2, TEST_TEXTURE_SIZE / 2};
    const gfx::texture::ptr rebuilt = rview.tex_acquire("TARGET", resized, count_creation);
    render_view_check(rebuilt != first && rebuilt->get_desc() == resized, "tex_acquire: new desc rebuilds");
    render_view_check(created == 2, "tex_acquire: callback runs once per build");
    render_view_check(rview.tex_find("TARGET") == rebuilt, "tex_acquire: the slot holds the rebuilt texture");
}

void test_slots_and_lookups()
{
    gfx::render_view rview;
    const auto stored = std::make_shared<gfx::texture>(make_compute_desc());
    rview.tex_acquire("ALIAS") = stored;
    render_view_check(rview.tex_find("ALIAS") == stored && rview.tex_get("ALIAS") == stored,
                      "tex_find / tex_get: see a texture stored in the slot");
    render_view_check(rview.tex_acquire("ALIAS", make_compute_desc()) == stored,
                      "tex_acquire: a stored texture with an equal desc is kept");
    rview.tex_remove("ALIAS");
    render_view_check(rview.tex_find("ALIAS") == nullptr, "tex_remove: the name is gone");
    render_view_check(rview.fbo_find("MISSING") == nullptr, "fbo_find: missing key is null");
    auto target_desc = make_compute_desc();
    target_desc.flags = BGFX_TEXTURE_RT;
    const gfx::texture::ptr target = rview.tex_acquire("TARGET", target_desc);
    auto fbo = std::make_shared<gfx::frame_buffer>();
    fbo->populate({target});
    rview.fbo_acquire("TARGET") = fbo;
    render_view_check(rview.fbo_find("TARGET") == fbo && rview.fbo_get("TARGET") == fbo,
                      "fbo_find / fbo_get: see the stored framebuffer");
    render_view_check(!gfx::needs_recreate(fbo, {target}), "fbo attachments: same textures keep the fbo");
    target_desc.format = bgfx::TextureFormat::RGBA8;
    const gfx::texture::ptr rebuilt = rview.tex_acquire("TARGET", target_desc);
    render_view_check(gfx::needs_recreate(fbo, {rebuilt}), "fbo attachments: a same-size rebuild is caught");
    render_view_check(gfx::needs_recreate(nullptr, {rebuilt}), "fbo attachments: missing fbo");
}

void test_transient_textures_lend_one_borrower_at_a_time()
{
    gfx::render_view rview;
    const auto desc = make_compute_desc();
    gfx::texture::ptr first = rview.tex_acquire_transient(desc);
    const gfx::texture::ptr second = rview.tex_acquire_transient(desc);
    render_view_check(first && second && first != second, "transient: a held texture is not lent twice");
    render_view_check(first->get_desc() == desc, "transient: built from the desc");
    const gfx::texture* first_raw = first.get();
    first.reset();
    const gfx::texture::ptr reused = rview.tex_acquire_transient(desc);
    render_view_check(reused.get() == first_raw, "transient: a dropped texture is lent again");
    auto other = desc;
    other.format = bgfx::TextureFormat::R32F;
    const gfx::texture::ptr different = rview.tex_acquire_transient(other);
    render_view_check(different != reused && different != second, "transient: another desc gets another texture");
}

void test_transient_textures_expire_when_free()
{
    gfx::texture_pool pool;
    const auto desc = make_compute_desc();
    const std::uint32_t frame = gfx::get_render_frame();
    const auto window = gfx::texture_pool::default_max_idle_frames;
    const gfx::texture::ptr held = pool.acquire(desc);
    pool.acquire(desc);
    render_view_check(pool.get_entry_count() == 2, "pool: two entries");
    pool.release_unused(frame + window);
    render_view_check(pool.get_entry_count() == 2, "pool: free entry survives the window");
    pool.release_unused(frame + window + 1);
    render_view_check(pool.get_entry_count() == 1, "pool: free entry expires past the window");
    pool.release_unused(frame + window * 100);
    render_view_check(pool.get_entry_count() == 1, "pool: held entry never expires");
    gfx::render_view rview;
    const std::weak_ptr<gfx::texture> dropped = rview.tex_acquire_transient(desc);
    rview.release_unused(frame + window);
    render_view_check(!dropped.expired(), "render_view: free transient texture survives the window");
    rview.release_unused(frame + window + 1);
    render_view_check(dropped.expired(), "render_view: release_unused drops expired transient textures");
}

} // namespace

// ---------------------------------------------------------------------------------

auto run_render_view_suite(rtti::context& /*ctx*/) -> int
{
    bgfx::Init init;
    init.type = bgfx::RendererType::Noop;
    if(!gfx::init(init))
    {
        std::printf("  FAIL: bgfx did not initialise on the Noop renderer\n");
        gfx::shutdown();
        return 1;
    }
    test_desc_round_trips_through_the_texture();
    test_size_constructors_build_through_the_desc();
    test_desc_comparison_is_exact();
    test_acquire_rebuilds_only_on_desc_change();
    test_slots_and_lookups();
    test_transient_textures_lend_one_borrower_at_a_time();
    test_transient_textures_expire_when_free();
    gfx::shutdown();
    std::printf("\n%d checks, %d failures\n", g_render_view_checks, g_render_view_failures);
    return g_render_view_failures;
}

REGISTER_TEST_SUITE("render view", run_render_view_suite)
