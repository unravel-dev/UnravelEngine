// prefilter_pass.h
#pragma once

#include <bgfx/bgfx.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/gpu_program.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>

namespace unravel
{
/// Performs GGX prefiltering on six 2D face textures to produce a filtered cubemap.
class prefilter_pass
{
public:
    struct run_params
    {
        std::array<gfx::texture::ptr, 6> input_faces;
        gfx::texture::ptr output_cube;   ///< Optional destination cubemap (will be created if null).
        gfx::texture::ptr output_cube_prefiltered;   ///< Optional destination cubemap (will be created if null).
        bool              apply_prefilter = true; ///< If false, copies mips from input to output.
    };

    /// Initialize shaders. Call once after bgfx::init() and asset registration.
    auto init(rtti::context& ctx) -> bool;

    /// Execute prefilter. Returns the filtered cubemap (output_cube or created internally).
    auto run(const run_params& params) -> gfx::texture::ptr;

    /// Execute prefilter using compute shader. Returns the filtered cubemap.
    auto run_compute(const run_params& params) -> gfx::texture::ptr;

private:

    struct cs_prog : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_env,  "s_env", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_data, "u_data", gfx::uniform_type::Vec4);
        }

        gfx::program::uniform_ptr s_env;  ///< samplerCube for environment
        gfx::program::uniform_ptr u_data; ///< vec4: x=mipIdx, y=faceIdx, z=cubeSize, w=numMips
        std::unique_ptr<gpu_program> program;
    } cs_;

    gfx::texture::ptr output_cube_; ///< Cached output cubemap
};

} // namespace unravel
