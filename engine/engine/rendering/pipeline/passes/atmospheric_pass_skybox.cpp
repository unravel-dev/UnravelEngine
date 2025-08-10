#include "atmospheric_pass_skybox.h"
#include <engine/assets/asset_manager.h>
#include <graphics/render_pass.h>

namespace unravel
{

bool atmospheric_pass_skybox::init(rtti::context& ctx)
{
    // 1) Load your cubemap (DDS or whichever format you have)
    auto& am = ctx.get_cached<asset_manager>();

    // 2) Load skybox shaders
    auto vs_sky = am.get_asset<gfx::shader>("engine:/data/shaders/atmospherics/vs_skybox.sc");
    auto fs_sky = am.get_asset<gfx::shader>("engine:/data/shaders/atmospherics/fs_skybox.sc");
    program_.program = std::make_unique<gpu_program>(vs_sky, fs_sky);

    if(!program_.program->is_valid())
    {
        APPLOG_ERROR("Failed to load skybox program.");
        return false;
    }

    program_.cache_uniforms();

    struct pos_vertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // 8 corners
    static pos_vertex s_cubeVertices[] = {
        {-1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},
        {-1.0f, -1.0f, 1.0f},
        {1.0f, -1.0f, 1.0f},
        {-1.0f, 1.0f, -1.0f},
        {1.0f, 1.0f, -1.0f},
        {-1.0f, -1.0f, -1.0f},
        {1.0f, -1.0f, -1.0f},
    };

    // Indices for an inside-out cube (36 total).
    // You can flip the order if you see backfaces incorrectly.
    // clang-format off
    static const uint16_t s_cubeIndices[] = {
        // -Z
        2,1,0,
        2,3,1,
        // +Z
        5,6,4,
        7,6,5,
        // -Y
        2,4,0,
        6,4,2,
        // +Y
        1,5,3,
        3,5,7,
        // -X
        0,4,1,
        1,4,5,
        // +X
        3,7,2,
        2,7,6,
    };
    // clang-format on

    vb_ = std::make_unique<gfx::vertex_buffer>(gfx::copy(s_cubeVertices, sizeof(s_cubeVertices)),
                                               gfx::pos_vertex::get_layout());

    ib_ = std::make_unique<gfx::index_buffer>(gfx::copy(s_cubeIndices, sizeof(s_cubeIndices)));

    return true;
}

void atmospheric_pass_skybox::run(gfx::frame_buffer::ptr target,
                                  const camera& cam,
                                  gfx::render_view& rview,
                                  delta_t dt,
                                  const run_params& params)
{
    if(!program_.program->is_valid())
    {
        return; // Can't do much if data is invalid
    }

    auto cubemap = params.cubemap.get();

    // 1) Create a pass
    gfx::render_pass pass("atmospheric_cubemap_pass");
    pass.bind(target.get());

    // 2) We want to keep the camera's orientation but remove its translation,
    //    so the skybox doesn't move with the camera.
    auto viewMtx = cam.get_view();
    viewMtx.set_translation(0.0f, 0.0f, 0.0f);
    pass.set_view_proj(viewMtx, cam.get_projection());

    // 3) Begin program, set up the cubemap
    program_.program->begin();

    gfx::set_texture(program_.u_texCube, 0, cubemap);

    // 4) The critical part: Use DEPTH_TEST_EQUAL, so we only fill the background
    uint64_t state =
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
        BGFX_STATE_DEPTH_TEST_LEQUAL; // or CULL_NONE, depending on vertex winding

    gfx::set_state(state);

    // 5) Submit the draw
    gfx::set_vertex_buffer(0, vb_->native_handle());
    gfx::set_index_buffer(ib_->native_handle());

    gfx::submit(pass.id, program_.program->native_handle());

    // 6) Done
    program_.program->end();
    gfx::discard();
}

} // namespace unravel
