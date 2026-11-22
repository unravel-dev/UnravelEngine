#include "post_material_pass.h"

#include "blit_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/material_graph/graph_material.h>
#include <engine/rendering/material_graph/material_graph_library.h>
#include <engine/rendering/pipeline/pre_exposure.h>
#include <engine/rendering/pipeline/volume_resolver.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>

#include <string>

namespace unravel
{

namespace
{
/// Stages of the view textures fs_material_graph_post.sh reads (mg_post.sh).
constexpr uint8_t POST_COLOR_STAGE = 7;
constexpr uint8_t POST_DEPTH_STAGE = 9;
constexpr uint8_t POST_GBUFFER0_STAGE = 10;
constexpr uint8_t POST_GBUFFER1_STAGE = 11;
constexpr uint8_t POST_GBUFFER2_STAGE = 12;
constexpr uint8_t POST_VELOCITY_STAGE = 13;
constexpr uint8_t POST_CUSTOM_DEPTH_STAGE = 8;
constexpr uint8_t POST_CUSTOM_MASK_STAGE = 14;
constexpr uint8_t POST_PRE_EXPOSURE_STAGE = 15;
constexpr uint32_t POST_POINT_SAMPLER = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
/// The colour reads between pixels blend: a material sampling a shifted UV (distortion, blur) reads smoothly.
constexpr uint32_t POST_COLOR_SAMPLER = BGFX_SAMPLER_UVW_CLAMP;

/// A target of the location's colour's size and format, named for the location, with its frame buffer.
auto acquire_post_target(gfx::render_view& rview, const std::string& name, const usize32_t& size, bgfx::TextureFormat::Enum format)
    -> const gfx::frame_buffer::ptr&
{
    const auto& texture = rview.tex_acquire(name, {.size = size, .format = format, .flags = BGFX_TEXTURE_RT});
    auto& fbo = rview.fbo_acquire(name);
    if(gfx::needs_recreate(fbo, {texture}))
    {
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({texture});
    }
    return fbo;
}
} // namespace

auto post_material_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    // Before any program: OpenGL wires a program to the uniforms registered when it links.
    uniforms_.cache_uniforms();
    programs_.init(am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.eshader"), material_domain::post_process);
    return true;
}

auto post_material_pass::run(gfx::render_view& rview,
                             const camera& cam,
                             const std::vector<post_process_material_draw>& draws,
                             const run_params& params,
                             blit_pass& blit) -> uint32_t
{
    if(!params.color)
    {
        return 0;
    }
    // The draws of this location whose programs run: a material still compiling is skipped this frame.
    std::vector<std::pair<const post_process_material_draw*, gpu_program*>> runnable;
    for(const auto& draw : draws)
    {
        if(draw.location != params.location || !draw.material)
        {
            continue;
        }
        if(auto* program = programs_.begin(*draw.material, gfx::shader::DEFAULT_VARIANT, gfx::shader::DEFAULT_VARIANT))
        {
            runnable.emplace_back(&draw, program);
        }
    }
    if(runnable.empty())
    {
        programs_.release_unused();
        return 0;
    }

    const auto& color = params.color->get_texture(0);
    const auto size = color->get_size();
    const auto format = bgfx::TextureFormat::Enum(color->info.format);
    // Locations run one after the other: those of one format and size share the two targets.
    const std::string target_key = fmt::format("{}_{}x{}", int(format), size.width, size.height);
    const auto& ping = acquire_post_target(rview, "POST_MATERIAL_A_" + target_key, size, format);
    const auto& pong = acquire_post_target(rview, "POST_MATERIAL_B_" + target_key, size, format);
    const std::string suffix = get_material_post_process_location_key(params.location);
    // The last material writes the location's colour itself, through a frame buffer of that texture alone: a depth the
    // location's frame buffer holds is one the materials read.
    auto& output = rview.fbo_acquire("POST_MATERIAL_OUT_" + suffix);
    if(gfx::needs_recreate(output, {color}))
    {
        output = std::make_shared<gfx::frame_buffer>();
        output->populate({color});
    }
    blit.run(rview, {.input = params.color, .output = ping});

    const math::mat4 inverse_view_proj = math::mat4(glm::inverse(glm::dmat4(cam.get_view_projection_relative().get_matrix())));
    const auto& black = default_textures::get().black_texture();
    const std::string pass_name = std::string("Post Materials/") + get_material_post_process_location_label(params.location);
    gfx::frame_buffer::ptr source = ping;
    for(size_t i = 0; i < runnable.size(); ++i)
    {
        const auto& [draw, program] = runnable[i];
        const bool is_last = i + 1 == runnable.size();
        const gfx::frame_buffer::ptr target = is_last ? output : (source == ping ? pong : ping);
        gfx::render_pass pass(pass_name.c_str());
        pass.bind(target.get());
        pass.set_view_proj(cam.get_view(), cam.get_projection());
        programs_.submit_values(draw->registers, draw->material->get_textures());
        programs_.submit_camera_position(cam.get_position());
        gfx::set_uniform(uniforms_.u_post_inv_view_proj, inverse_view_proj);
        gfx::set_uniform(uniforms_.u_post_params, math::vec4(draw->weight, 0.0f, 0.0f, 0.0f));
        gfx::set_texture(uniforms_.s_post_color, POST_COLOR_STAGE, source->get_texture(0), POST_COLOR_SAMPLER);
        const auto& far_depth = default_textures::get().camera_far_depth_texture();
        gfx::set_texture(uniforms_.s_post_depth, POST_DEPTH_STAGE, params.depth ? params.depth : far_depth, POST_POINT_SAMPLER);
        gfx::set_texture(uniforms_.s_post_gbuffer0, POST_GBUFFER0_STAGE, params.gbuffer0 ? params.gbuffer0 : black, POST_POINT_SAMPLER);
        gfx::set_texture(uniforms_.s_post_gbuffer1, POST_GBUFFER1_STAGE, params.gbuffer1 ? params.gbuffer1 : black, POST_POINT_SAMPLER);
        gfx::set_texture(uniforms_.s_post_gbuffer2, POST_GBUFFER2_STAGE, params.gbuffer2 ? params.gbuffer2 : black, POST_POINT_SAMPLER);
        gfx::set_texture(uniforms_.s_post_velocity, POST_VELOCITY_STAGE, params.velocity ? params.velocity : black, POST_POINT_SAMPLER);
        const auto scene_textures = draw->material->get_scene_textures();
        if((scene_textures & material_scene_texture_custom_mask) != 0)
        {
            gfx::set_texture(uniforms_.s_post_custom_mask, POST_CUSTOM_MASK_STAGE, params.custom_mask ? params.custom_mask : black, POST_POINT_SAMPLER);
        }
        if((scene_textures & material_scene_texture_custom_depth) != 0)
        {
            gfx::set_texture(uniforms_.s_post_custom_depth, POST_CUSTOM_DEPTH_STAGE, params.custom_depth ? params.custom_depth : far_depth, POST_POINT_SAMPLER);
        }
        if(params.pre_exposure != nullptr)
        {
            const auto pre_exposure = params.pre_exposure->to_uniform();
            gfx::set_uniform(uniforms_.u_pre_exposure, math::vec4(pre_exposure[0], pre_exposure[1], pre_exposure[2], pre_exposure[3]));
            params.pre_exposure->bind(POST_PRE_EXPOSURE_STAGE);
        }
        else
        {
            gfx::set_uniform(uniforms_.u_pre_exposure, math::vec4(1.0f));
            gfx::set_texture(uniforms_.s_pre_exposure, POST_PRE_EXPOSURE_STAGE, default_textures::get().white_texture(), POST_POINT_SAMPLER);
        }
        bgfx::setState(gfx::clip_quad(1.0f) | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(pass.id, program->native_handle());
        source = target;
    }
    programs_.release_unused();
    return uint32_t(runnable.size());
}

} // namespace unravel
