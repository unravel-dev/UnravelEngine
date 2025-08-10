#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gpu_program.h>
#include <graphics/render_view.h>

namespace unravel
{

enum class tonemapping_method : uint8_t
{
    none = 0,
    exponential,
    reinhard,
    reinhard_lum,
    hable,
    duiker,
    aces,
    aces_lum,
    filmic
};

class tonemapping_pass
{
public:
    struct settings
    {
        float exposure = 1.0f;
        tonemapping_method method = tonemapping_method::aces;
    };

    struct run_params
    {
        gfx::frame_buffer::ptr input;
        gfx::frame_buffer::ptr output;

        settings config{};
    };

    auto init(rtti::context& ctx) -> bool;
    auto run(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr;

private:
    auto create_or_update_output_fb(const gfx::frame_buffer::ptr& input, const gfx::frame_buffer::ptr& output) -> gfx::frame_buffer::ptr;

    struct tonemapping_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_tonemapping, "u_tonemapping", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_input, "s_input", gfx::uniform_type::Sampler);
        }

        gfx::program::uniform_ptr u_tonemapping;
        gfx::program::uniform_ptr s_input;

        std::unique_ptr<gpu_program> program;

    } tonemapping_program_;

    gfx::frame_buffer::ptr output_;

};
} // namespace unravel
