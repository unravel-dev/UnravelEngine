#pragma once

#include <bgfx/bgfx.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/gpu_program.h>
#include <graphics/render_pass.h>
#include <graphics/render_view.h>
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
        gfx::texture::ptr output_cube;   ///< Cubemap the faces are copied into: the prefilter's input.
        gfx::texture::ptr output_cube_prefiltered;   ///< Destination cubemap, filtered or a plain copy of the faces.
        bool              apply_prefilter = true; ///< If false, copies the faces' mips to output_cube_prefiltered.
    };

    /// Initialize shaders. Call once after bgfx::init() and asset registration.
    auto init(rtti::context& ctx) -> bool;

    /// Execute prefilter, or copy the faces when the device cannot run it. Returns output_cube_prefiltered.
    auto run(const run_params& params) -> gfx::texture::ptr;

    /// Execute prefilter using compute shader. Returns output_cube_prefiltered.
    auto run_compute(const run_params& params) -> gfx::texture::ptr;

private:
    /// Copies the faces' mips into @p output_cube; mips the faces lack are made from the ones above them.
    void copy_faces(const run_params& params, const gfx::texture::ptr& output_cube);
    /// Makes mips @p first_mip and below of @p cube, each the box average of the mip above it.
    void generate_cube_mips(const gfx::texture::ptr& cube, std::uint8_t first_mip);

    struct cs_prog : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_env,  "s_env", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), u_data, "u_data", bgfx::UniformType::Vec4);
        }

        gfx::program::uniform_ptr s_env;  ///< samplerCube for environment
        gfx::program::uniform_ptr u_data; ///< vec4: x=mipIdx, y=unused, z=cubeSize, w=numMips
        std::unique_ptr<gpu_program> program;
    } cs_;

    /// Makes a cube mip from the mip above it, for devices that do not generate the faces' mips themselves.
    struct downsample_prog : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_source, "s_source", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), u_downsample, "u_downsample", bgfx::UniformType::Vec4);
        }

        gfx::program::uniform_ptr s_source;     ///< sampler2DArray: a copy of the cube's faces
        gfx::program::uniform_ptr u_downsample; ///< vec4: x=target mip size, y=source mip
        std::unique_ptr<gpu_program> program;
    } downsample_;

    /// The cube's faces as a 2D array the downsample samples; the size of the last cube it made mips for.
    gfx::texture::ptr mip_source_;
};

} // namespace unravel
