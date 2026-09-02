#pragma once

#include <engine/rendering/gpu_program.h>

#include <graphics/render_view.h>

namespace unravel
{
class camera;

/**
 * @brief Snapshots the scene-referred HDR target into @c PREV_SCENE_HDR for next frame's
 *        feedback readers (the GI gather's screen tier and far field, SSR), with each pixel's
 *        view depth in alpha so a reader can validate its reprojection against the surface the
 *        pixel was rendered from (fs_gi_scene_snapshot.sc).
 *
 * Replaces a plain blit: same cost (one full-screen pass over the same target), one more
 * channel of meaning.
 */
class scene_history_pass
{
public:
    auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Writes the snapshot. @p source is the post-TAA scene target, @p depth this frame's
     *        G-buffer depth, @p cam the camera the frame was rendered with.
     * @return The history texture, null when nothing could be written.
     */
    auto run(gfx::render_view& rview,
             const gfx::frame_buffer::ptr& source,
             const gfx::texture::ptr& depth,
             const camera& cam) -> gfx::texture::ptr;

    auto is_valid() const -> bool
    {
        return program_.program && program_.program->is_valid();
    }

private:
    struct snapshot_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_scene, "s_scene", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_depth, "s_depth", gfx::uniform_type::Sampler);
        }

        gfx::program::uniform_ptr s_scene;
        gfx::program::uniform_ptr s_depth;
        std::unique_ptr<gpu_program> program;
    } program_;
};

} // namespace unravel
