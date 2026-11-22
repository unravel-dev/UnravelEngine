#include "mask_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/material.h>
#include <engine/rendering/pipeline/pipeline.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>

#include <string>

namespace unravel
{

namespace
{
/// The variant of fs_mask.eshader that keeps only the surfaces the view shows.
constexpr const char* MASK_VISIBLE_VARIANT = "visible";
constexpr uint8_t MASK_SCENE_DEPTH_STAGE = 0;
constexpr uint32_t MASK_POINT_SAMPLER = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
} // namespace

auto mask_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto vs = am.get_asset<gfx::shader>("engine:/data/shaders/mask/vs_mask.eshader");
    fragment_shader_ = am.get_asset<gfx::shader>("engine:/data/shaders/mask/fs_mask.eshader");
    // Uniforms first: OpenGL wires a program to the uniforms registered when it links.
    program_.cache_uniforms();
    program_.program = std::make_unique<gpu_program>(vs, gfx::shader::DEFAULT_VARIANT, fragment_shader_);
    program_skinned_.cache_uniforms();
    program_skinned_.program = std::make_unique<gpu_program>(vs, "skinned", fragment_shader_);
    visible_program_.cache_uniforms();
    visible_program_.program = std::make_unique<gpu_program>(vs, gfx::shader::DEFAULT_VARIANT, fragment_shader_, MASK_VISIBLE_VARIANT);
    visible_program_skinned_.cache_uniforms();
    visible_program_skinned_.program = std::make_unique<gpu_program>(vs, "skinned", fragment_shader_, MASK_VISIBLE_VARIANT);
    moved_programs_.init(am.get_asset<gfx::shader>("engine:/data/shaders/deferred_geom/vs_deferred_geom.eshader"));
    return true;
}

auto mask_pass::get_program(bool is_skinned, bool is_visible_only) -> mask_program&
{
    if(is_visible_only)
    {
        return is_skinned ? visible_program_skinned_ : visible_program_;
    }
    return is_skinned ? program_skinned_ : program_;
}

auto mask_pass::run(gfx::render_view& rview,
                    const camera& cam,
                    hpp::span<const rendering::visibility_data> models,
                    const value_function& value_of,
                    const run_params& params) -> gfx::frame_buffer::ptr
{
    const auto* sizing = params.visible_depth ? params.visible_depth.get() : nullptr;
    const auto size = sizing != nullptr ? sizing->get_size() : rview.fbo_get("GBUFFER")->get_size();
    const auto& mask = rview.tex_acquire(params.name, {.size = size, .format = params.format, .flags = BGFX_TEXTURE_RT});
    const bool has_own_depth = !params.visible_depth;
    std::vector<gfx::texture::ptr> attachments{mask};
    if(has_own_depth)
    {
        const std::string depth_name = std::string(params.name) + "_DEPTH";
        attachments.push_back(rview.tex_acquire(depth_name, {.size = size, .format = bgfx::TextureFormat::D32F, .flags = BGFX_TEXTURE_RT}));
    }
    auto& fbo = rview.fbo_acquire(params.name);
    if(gfx::needs_recreate(fbo, attachments))
    {
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate(attachments);
    }

    gfx::render_pass pass(params.pass_name);
    pass.bind(fbo.get());
    pass.clear(BGFX_CLEAR_COLOR | (has_own_depth ? BGFX_CLEAR_DEPTH : 0), params.clear_rgba, gfx::camera_far_depth, 0);
    pass.set_view_proj(cam.get_view(), cam.get_projection());

    const bool is_visible_only = !has_own_depth;
    const uint64_t depth_state = has_own_depth ? (BGFX_STATE_WRITE_Z | gfx::depth_test_nearer) : 0;
    const auto camera_position = cam.get_position();
    for(const auto& element : models)
    {
        const auto& entity = element.entity;
        auto* model_comp = entity ? entity.try_get<model_component>() : nullptr;
        auto* transform_comp = entity ? entity.try_get<transform_component>() : nullptr;
        if(model_comp == nullptr || transform_comp == nullptr)
        {
            continue;
        }
        const auto value = value_of(entity);
        const auto& mdl = model_comp->get_model();
        if(!value || !mdl.is_valid())
        {
            continue;
        }
        model::submit_callbacks callbacks;
        callbacks.setup_begin = [&](const model::submit_callbacks::params& submit_params)
        {
            get_program(submit_params.skinned, is_visible_only).program->begin();
        };
        callbacks.setup_params_per_submesh = [&](const model::submit_callbacks::params& submit_params, const material& mat)
        {
            auto& prog = get_program(submit_params.skinned, is_visible_only);
            // A material whose World Position Offset moves the vertices draws with its own vertex shader.
            auto* moved = moved_programs_.begin_moved_vertices(mat,
                                                               graph_material_programs::get_position_only_variant(submit_params.skinned),
                                                               fragment_shader_,
                                                               camera_position,
                                                               is_visible_only ? MASK_VISIBLE_VARIANT : gfx::shader::DEFAULT_VARIANT);
            if(moved == nullptr && !prog.program->is_valid())
            {
                return;
            }
            gfx::set_uniform(prog.u_mask_value, *value);
            if(is_visible_only)
            {
                gfx::set_texture(prog.s_mask_scene_depth, MASK_SCENE_DEPTH_STAGE, params.visible_depth, MASK_POINT_SAMPLER);
            }
            const uint64_t cull_state = mat.get_render_states(true, false, false) & BGFX_STATE_CULL_MASK;
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | depth_state | cull_state);
            bgfx::submit(pass.id,
                         moved != nullptr ? moved->native_handle() : prog.program->native_handle(),
                         0,
                         submit_params.preserve_state ? BGFX_DISCARD_NONE : BGFX_DISCARD_ALL);
        };
        callbacks.setup_end = [&](const model::submit_callbacks::params& submit_params)
        {
            get_program(submit_params.skinned, is_visible_only).program->end();
        };
        mdl.submit(transform_comp->get_transform_global(),
                   model_comp->get_submesh_transforms(),
                   model_comp->get_bone_transforms(),
                   model_comp->get_skinning_transforms(),
                   element.lod_data.current_lod_index,
                   callbacks,
                   nullptr,
                   nullptr,
                   model_comp->get_submit_extras());
    }
    bgfx::discard();
    moved_programs_.release_unused();
    return fbo;
}

} // namespace unravel
