#pragma once

#include <engine/rendering/gpu_program.h>
#include <graphics/render_view.h>

#include <hpp/string_view.hpp>

#include <cstdint>

namespace unravel
{

/**
 * @brief Copies a depth texture into a texture of the render view, for passes that sample the depth while the
 * original is attached or rewritten.
 *
 * A blit where the device blits depth (gfx::device_caps::has_depth_blit); otherwise a draw that writes each texel's
 * depth into the copy, attached as the depth of a framebuffer of its own.
 */
class depth_copy_pass
{
public:
    struct run_params
    {
        /// The render view's name for the copy, and for the framebuffer the draw copy renders into.
        hpp::string_view name;
        /// The depth to copy.
        gfx::texture::ptr source;
        /// BGFX_SAMPLER_* defaults of the copy.
        std::uint64_t sampler_flags{};
        /// The copy's pass name in captures and the profiler.
        const char* pass_name{"Depth Copy/Pass"};
    };

    /// Loads the program; called once, after bgfx and the asset manager are up.
    auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Copies the source into the view's texture of that name, in a view of its own: after the views made
     * before this call, before the views made after it.
     * @return The copy, or null when there is no source or the draw copy has no program.
     */
    auto run(gfx::render_view& rview, const run_params& params) -> gfx::texture::ptr;

private:
    /// Draws the source's depth into @p copy.
    auto draw_copy(gfx::render_view& rview, const run_params& params, const gfx::texture::ptr& copy) -> bool;

    struct depth_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_depth, "s_depth", bgfx::UniformType::Sampler);
        }

        gfx::program::uniform_ptr s_depth;
        std::unique_ptr<gpu_program> program;
    } depth_program_;
};

} // namespace unravel
