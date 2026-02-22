#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gpu_program.h>

#include <graphics/index_buffer.h>
#include <graphics/render_view.h>
#include <graphics/vertex_buffer.h>
#include <graphics/vertex_decl.h>

namespace unravel
{

class atmospheric_pass_skybox
{
public:
    struct run_params
    {
        asset_handle<gfx::texture> cubemap;
        /// Sky brightness multiplier (1.0 = neutral). Affects visible sky and irradiance.
        float sky_brightness = 1.0f;
    };
    bool init(rtti::context& ctx);

    // Run after geometry
    void run(gfx::frame_buffer::ptr target, const camera& cam, gfx::render_view& rview, delta_t dt, const run_params& params);

private:
    struct skybox_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_texCube, "s_texCube", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_sky_brightness, "u_sky_brightness", gfx::uniform_type::Vec4);
        }

        gfx::program::uniform_ptr u_texCube;
        gfx::program::uniform_ptr u_sky_brightness;
        std::unique_ptr<gpu_program> program;
    } program_;

    // A simple inside-out cube
    std::unique_ptr<gfx::vertex_buffer> vb_;
    std::unique_ptr<gfx::index_buffer> ib_;
};
} // namespace unravel
