#pragma once

#include "gizmos/gizmos.h"
#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <editor/editing/editing_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/ecs/ecs.h>

namespace unravel
{
class camera;
class gpu_program;
class gizmos_renderer
{
public:
    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    void on_frame_render(rtti::context& ctx, scene& scn, entt::handle camera_entity);

private:
    void draw_grid(uint32_t pass_id, const camera& cam, const editing_manager::grid& grid);
    void draw_selection_gizmos(rtti::context& ctx, const camera& camera, gfx::dd_raii& dd);
    void draw_selection_outlines(rtti::context& ctx, uint32_t pass_id, const camera& camera, const gfx::frame_buffer::ptr& obuffer);
    void draw_icon_gizmos(rtti::context& ctx, scene& scn, const camera& camera, gfx::dd_raii& dd);
    void draw_selection_mask_pass(rtti::context& ctx, const camera& camera, const gfx::frame_buffer::ptr& selection_mask);
    void draw_outline_pass(uint32_t pass_id, const gfx::frame_buffer::ptr& selection_mask, const gfx::frame_buffer::ptr& obuffer);
    ///
    std::unique_ptr<gpu_program> wireframe_program_;
    std::unique_ptr<gpu_program> grid_program_;

    struct flat_to_r_program : uniforms_cache
    {
        void cache_uniforms()
        {
        }
        std::unique_ptr<gpu_program> program;

    } outline_mask_program_, outline_mask_program_skinned_;

    struct outline_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_tex, "s_tex", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_data, "u_data", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_outline_color, "u_outline_color", gfx::uniform_type::Vec4);
        }

        gfx::program::uniform_ptr s_tex;
        gfx::program::uniform_ptr u_data;
        gfx::program::uniform_ptr u_outline_color;

        std::unique_ptr<gpu_program> program;

    } outline_program_;

    void resize_selection_mask_rt(uint16_t width, uint16_t height)
    {
        if(selection_mask_ && selection_mask_->get_size().width == width &&
           selection_mask_->get_size().height == height)
        {
            return;
        }

        auto tex = std::make_shared<gfx::texture>(width,
                                                  height,
                                                  false, // no mip
                                                  1,     // single layer
                                                  bgfx::TextureFormat::R8,
                                                  BGFX_TEXTURE_RT);

        std::vector<gfx::texture::ptr> attachments{tex};
        selection_mask_ = std::make_shared<gfx::frame_buffer>(attachments);
    }

    gfx::frame_buffer::ptr selection_mask_;

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
};
} // namespace unravel
