#include "assao_pass.h"
#include "pass_helpers.h"
#include "graphics/graphics.h"
#include <engine/assets/asset_manager.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>
#include <algorithm>
#include <array>
#include <string>

namespace unravel
{

namespace
{
constexpr uint32_t SAMPLER_POINT_CLAMP = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
constexpr uint32_t SAMPLER_POINT_MIRROR = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_MIRROR;
constexpr uint32_t SAMPLER_LINEAR_CLAMP = BGFX_SAMPLER_UVW_CLAMP;

constexpr uint8_t SSAO_DEPTH_MIP_LEVELS = 4;
constexpr int32_t MAX_BLUR_PASS_COUNT = 6;
/// Threads per group along each axis of the compute passes.
constexpr uint32_t GROUP_SIZE = 8;

void set_vec2(float* _v, float _x, float _y)
{
    _v[0] = _x;
    _v[1] = _y;
}

void set_vec4(float* _v, float _x, float _y, float _z, float _w)
{
    _v[0] = _x;
    _v[1] = _y;
    _v[2] = _z;
    _v[3] = _w;
}

void set_vec4i(int32_t* _v, int32_t _x, int32_t _y, int32_t _z, int32_t _w)
{
    _v[0] = _x;
    _v[1] = _y;
    _v[2] = _z;
    _v[3] = _w;
}
} // namespace

auto assao_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();

    auto load_program = [&](gpu_program::ptr& program,
                            const std::string& cs,
                            const std::string& variant = gfx::shader::DEFAULT_VARIANT)
    {
        auto cs_shader = am.get_asset<gfx::shader>("engine:/data/shaders/assao/" + cs + ".eshader");
        program = std::make_shared<gpu_program>(cs_shader, variant);
    };

    // Create uniforms
    u_rect = bgfx::createUniform("u_rect", bgfx::UniformType::Vec4); // viewport/scissor rect for compute
    m_uniforms.init();

    // Create texture sampler uniforms (used when we bind textures)
    // clang-format off
    s_normalmapSource            = bgfx::createUniform("s_normalmapSource", bgfx::UniformType::Sampler);
    s_blurInput                  = bgfx::createUniform("s_blurInput", bgfx::UniformType::Sampler);
    s_finalSSAO                  = bgfx::createUniform("s_finalSSAO", bgfx::UniformType::Sampler);
    s_depthSource                = bgfx::createUniform("s_depthSource", bgfx::UniformType::Sampler);
    s_viewspaceDepthSource       = bgfx::createUniform("s_viewspaceDepthSource", bgfx::UniformType::Sampler);
    s_viewspaceDepthSourceMirror = bgfx::createUniform("s_viewspaceDepthSourceMirror", bgfx::UniformType::Sampler);
    s_importanceMap              = bgfx::createUniform("s_importanceMap", bgfx::UniformType::Sampler);
    // clang-format on

    // Create program from shaders.

    // clang-format off
    load_program(m_prepareDepthsProgram,               "cs_assao_prepare_depths");
    load_program(m_prepareNormalsProgram,              "cs_assao_prepare_normals");
    load_program(m_prepareDepthsHalfProgram,           "cs_assao_prepare_depths_half");
    load_program(m_prepareDepthsAndNormalsHalfProgram, "cs_assao_prepare_depths_and_normals_half");
    load_program(m_prepareDepthMipProgram,             "cs_assao_prepare_depth_mip");
    load_program(m_generateQ0Program,                  "cs_assao_generate");
    load_program(m_generateQ1Program,                  "cs_assao_generate", "q1");
    load_program(m_generateQ2Program,                  "cs_assao_generate", "q2");
    load_program(m_generateQ3Program,                  "cs_assao_generate", "q3");
    load_program(m_generateQ3BaseProgram,              "cs_assao_generate", "q3base");
    load_program(m_generateQ0GBufferProgram,           "cs_assao_generate", "q0_gbuffer_normals");
    load_program(m_generateQ1GBufferProgram,           "cs_assao_generate", "q1_gbuffer_normals");
    load_program(m_generateQ2GBufferProgram,           "cs_assao_generate", "q2_gbuffer_normals");
    load_program(m_generateQ3GBufferProgram,           "cs_assao_generate", "q3_gbuffer_normals");
    load_program(m_generateQ3BaseGBufferProgram,       "cs_assao_generate", "q3base_gbuffer_normals");
    load_program(m_smartBlurProgram,                   "cs_assao_smart_blur");
    load_program(m_smartBlurWideProgram,               "cs_assao_smart_blur_wide");
    load_program(m_nonSmartBlurProgram,                "cs_assao_non_smart_blur");
    load_program(m_applyProgram,                       "cs_assao_apply");
    load_program(m_nonSmartApplyProgram,               "cs_assao_non_smart_apply");
    load_program(m_nonSmartHalfApplyProgram,           "cs_assao_non_smart_half_apply");
    load_program(m_generateImportanceMapProgram,       "cs_assao_generate_importance_map");
    load_program(m_postprocessImportanceMapAProgram,   "cs_assao_postprocess_importance_map_a");
    load_program(m_postprocessImportanceMapBProgram,   "cs_assao_postprocess_importance_map_b");
    load_program(m_loadCounterClearProgram,            "cs_assao_load_counter_clear");
    // clang-format on

    m_loadCounter = bgfx::createDynamicIndexBuffer(1, BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_INDEX32);

    return has_programs();
}

auto assao_pass::get_programs() const -> std::array<gpu_program*, PROGRAM_COUNT>
{
    return {m_prepareDepthsProgram.get(),
            m_prepareNormalsProgram.get(),
            m_prepareDepthsHalfProgram.get(),
            m_prepareDepthsAndNormalsHalfProgram.get(),
            m_prepareDepthMipProgram.get(),
            m_generateQ0Program.get(),
            m_generateQ1Program.get(),
            m_generateQ2Program.get(),
            m_generateQ3Program.get(),
            m_generateQ3BaseProgram.get(),
            m_generateQ0GBufferProgram.get(),
            m_generateQ1GBufferProgram.get(),
            m_generateQ2GBufferProgram.get(),
            m_generateQ3GBufferProgram.get(),
            m_generateQ3BaseGBufferProgram.get(),
            m_smartBlurProgram.get(),
            m_smartBlurWideProgram.get(),
            m_nonSmartBlurProgram.get(),
            m_applyProgram.get(),
            m_nonSmartApplyProgram.get(),
            m_nonSmartHalfApplyProgram.get(),
            m_generateImportanceMapProgram.get(),
            m_postprocessImportanceMapAProgram.get(),
            m_postprocessImportanceMapBProgram.get(),
            m_loadCounterClearProgram.get()};
}

auto assao_pass::has_programs() const -> bool
{
    const auto programs = get_programs();
    return std::all_of(programs.begin(),
                       programs.end(),
                       [](const gpu_program* program)
                       {
                           return program != nullptr && program->is_valid();
                       });
}

auto assao_pass::begin_programs() -> bool
{
    const auto programs = get_programs();
    return std::all_of(programs.begin(), programs.end(), begin_program<gpu_program*>);
}

void assao_pass::run(const camera& cam, gfx::render_view& rview, const run_params& params)
{
    // The depth constants and the view-space reconstruction of the shaders assume a perspective projection.
    if(!begin_programs() || cam.get_projection_mode() != projection_mode::perspective)
    {
        release_resources(rview);
        return;
    }
    m_settings = params.params;

    const auto size = params.depth->get_size();
    const int32_t width = static_cast<int32_t>(size.width);
    const int32_t height = static_cast<int32_t>(size.height);

    dimensions dims;
    dims.size[0] = width;
    dims.size[1] = height;
    dims.halfSize[0] = (dims.size[0] + 1) / 2;
    dims.halfSize[1] = (dims.size[1] + 1) / 2;
    dims.quarterSize[0] = (dims.halfSize[0] + 1) / 2;
    dims.quarterSize[1] = (dims.halfSize[1] + 1) / 2;
    set_vec4i(dims.fullResOutScissorRect, 0, 0, width, height);
    set_vec4i(dims.halfResOutScissorRect,
             dims.fullResOutScissorRect[0] / 2,
             dims.fullResOutScissorRect[1] / 2,
             (dims.fullResOutScissorRect[2] + 1) / 2,
             (dims.fullResOutScissorRect[3] + 1) / 2);
    const int32_t blurEnlarge =
        MAX_BLUR_PASS_COUNT + bx::max(0, MAX_BLUR_PASS_COUNT - 2);
    set_vec4i(dims.halfResOutScissorRect,
             bx::max(0, dims.halfResOutScissorRect[0] - blurEnlarge),
             bx::max(0, dims.halfResOutScissorRect[1] - blurEnlarge),
             bx::min(dims.halfSize[0], dims.halfResOutScissorRect[2] + blurEnlarge),
             bx::min(dims.halfSize[1], dims.halfResOutScissorRect[3] + blurEnlarge));

    create_or_update_frame_buffers(rview, dims);

    const float* viewMtx = cam.get_view();
    float projMtx[16];

    float n = cam.get_near_clip();
    float f = cam.get_far_clip();

    bx::mtxProj(projMtx, cam.get_fov(), float(dims.size[0]) / float(dims.size[1]), n, f, false);

    const auto& halfDepths0 = rview.tex_get("ASSAO_HALF_DEPTH_0");
    const auto& halfDepths1 = rview.tex_get("ASSAO_HALF_DEPTH_1");
    const auto& halfDepths2 = rview.tex_get("ASSAO_HALF_DEPTH_2");
    const auto& halfDepths3 = rview.tex_get("ASSAO_HALF_DEPTH_3");
    const gfx::texture::ptr halfDepthsArr[4] = {halfDepths0, halfDepths1, halfDepths2, halfDepths3};
    const auto& pingPongA = rview.tex_get("ASSAO_PING_PONG_A");
    const auto& pingPongB = rview.tex_get("ASSAO_PING_PONG_B");
    const auto& finalResults = rview.tex_get("ASSAO_FINAL_RESULTS");
    const auto& normals = rview.tex_get("ASSAO_NORMALS");
    const auto& importanceMap = rview.tex_get("ASSAO_IMPORTANCE_MAP");
    const auto& importanceMapPong = rview.tex_get("ASSAO_IMPORTANCE_MAP_PONG");
    const auto& aoMap = rview.tex_get("ASSAO_AO_MAP");

    update_uniforms(0, viewMtx, projMtx, dims);

    gfx::render_pass assao_render_pass("ASSAO/Pass");
    auto view = assao_render_pass.id;

    {
        bgfx::setTexture(0, s_depthSource, params.depth->native_handle(), SAMPLER_POINT_CLAMP);
        m_uniforms.submit();

        if(m_settings.quality_level < 0)
        {
            if(m_settings.generate_normals)
            {
                bgfx::setImage(5, normals->native_handle(), 0, bgfx::Access::Write);
            }

            for(int32_t j = 0; j < 2; ++j)
            {
                bgfx::setImage((uint8_t)(j + 1),
                              (j == 0 ? halfDepths0 : halfDepths3)->native_handle(),
                              0,
                              bgfx::Access::Write);
            }

            gfx::dispatch(view,
                           (m_settings.generate_normals ? m_prepareDepthsAndNormalsHalfProgram
                                                        : m_prepareDepthsHalfProgram)
                               ->native_handle(),
                           compute_group_count(dims.halfSize[0], GROUP_SIZE),
                           compute_group_count(dims.halfSize[1], GROUP_SIZE));
        }
        else
        {
            bgfx::setImage(1, halfDepths0->native_handle(), 0, bgfx::Access::Write);
            bgfx::setImage(2, halfDepths1->native_handle(), 0, bgfx::Access::Write);
            bgfx::setImage(3, halfDepths2->native_handle(), 0, bgfx::Access::Write);
            bgfx::setImage(4, halfDepths3->native_handle(), 0, bgfx::Access::Write);

            gfx::dispatch(view,
                           m_prepareDepthsProgram->native_handle(),
                           compute_group_count(dims.halfSize[0], GROUP_SIZE),
                           compute_group_count(dims.halfSize[1], GROUP_SIZE));
            // The normals in a pass of their own: the depths take the four storage textures a compute stage may
            // write on every WebGPU device.
            if(m_settings.generate_normals)
            {
                bgfx::setTexture(0, s_depthSource, params.depth->native_handle(), SAMPLER_POINT_CLAMP);
                m_uniforms.submit();
                bgfx::setImage(5, normals->native_handle(), 0, bgfx::Access::Write);
                gfx::dispatch(view,
                               m_prepareNormalsProgram->native_handle(),
                               compute_group_count(dims.halfSize[0], GROUP_SIZE),
                               compute_group_count(dims.halfSize[1], GROUP_SIZE));
            }
        }
    }

    // only do mipmaps for higher quality levels (not beneficial on quality level 1, and detrimental on quality level 0)
    if(m_settings.quality_level > 1)
    {
        uint16_t mipWidth = (uint16_t)dims.halfSize[0];
        uint16_t mipHeight = (uint16_t)dims.halfSize[1];

        // A tiny target has fewer mips than the generate pass could read; its sampler clamps to the last one.
        const uint8_t mip_levels = std::min<uint8_t>(SSAO_DEPTH_MIP_LEVELS, halfDepths0->info.numMips);
        for(uint8_t i = 1; i < mip_levels; i++)
        {
            mipWidth = (uint16_t)bx::max(1, mipWidth >> 1);
            mipHeight = (uint16_t)bx::max(1, mipHeight >> 1);

            bgfx::setImage(0, halfDepths0->native_handle(), i - 1, bgfx::Access::Read);
            bgfx::setImage(4, halfDepths0->native_handle(), i, bgfx::Access::Write);
            bgfx::setImage(1, halfDepths1->native_handle(), i - 1, bgfx::Access::Read);
            bgfx::setImage(5, halfDepths1->native_handle(), i, bgfx::Access::Write);
            bgfx::setImage(2, halfDepths2->native_handle(), i - 1, bgfx::Access::Read);
            bgfx::setImage(6, halfDepths2->native_handle(), i, bgfx::Access::Write);
            bgfx::setImage(3, halfDepths3->native_handle(), i - 1, bgfx::Access::Read);
            bgfx::setImage(7, halfDepths3->native_handle(), i, bgfx::Access::Write);

            m_uniforms.submit();
            float rect[4] = {0.0f, 0.0f, (float)mipWidth, (float)mipHeight};
            bgfx::setUniform(u_rect, rect);

            gfx::dispatch(view, m_prepareDepthMipProgram->native_handle(), compute_group_count(mipWidth, GROUP_SIZE), compute_group_count(mipHeight, GROUP_SIZE));
        }
    }

    // for adaptive quality, importance map pass
    for(int32_t ssaoPass = 0; ssaoPass < 2; ++ssaoPass)
    {
        if(ssaoPass == 0 && m_settings.quality_level < 3)
        {
            continue;
        }

        if(ssaoPass == 1 && m_settings.quality_level == 3)
        {
            gfx::render_pass importance_render_pass(fmt::format("ASSAO/Importance Map Pass {}", ssaoPass).c_str());
            view = importance_render_pass.id;
        }

        bool adaptiveBasePass = (ssaoPass == 0);

        int32_t passCount = 4;

        const uint32_t halfResNumX = compute_group_count(dims.halfResOutScissorRect[2] - dims.halfResOutScissorRect[0], GROUP_SIZE);
        const uint32_t halfResNumY = compute_group_count(dims.halfResOutScissorRect[3] - dims.halfResOutScissorRect[1], GROUP_SIZE);
        float halfResRect[4] = {(float)dims.halfResOutScissorRect[0],
                                (float)dims.halfResOutScissorRect[1],
                                (float)dims.halfResOutScissorRect[2],
                                (float)dims.halfResOutScissorRect[3]};

        for(int32_t pass = 0; pass < passCount; pass++)
        {
            if(m_settings.quality_level < 0 && (pass == 1 || pass == 2))
            {
                continue;
            }

            int32_t blurPasses = m_settings.blur_pass_count;
            blurPasses = bx::min(blurPasses, MAX_BLUR_PASS_COUNT);

            if(m_settings.quality_level == 3)
            {
                // if adaptive, at least one blur pass needed as the first pass needs to read the final texture results
                // - kind of awkward
                if(adaptiveBasePass)
                {
                    blurPasses = 0;
                }
                else
                {
                    blurPasses = bx::max(1, blurPasses);
                }
            }
            else if(m_settings.quality_level <= 0)
            {
                // just one blur pass allowed for minimum quality
                blurPasses = bx::min(1, m_settings.blur_pass_count);
            }

            update_uniforms(pass, viewMtx, projMtx, dims);

            bgfx::TextureHandle pPingRT = pingPongA->native_handle();
            bgfx::TextureHandle pPongRT = pingPongB->native_handle();

            // Generate
            {
                bgfx::setImage(6,
                              blurPasses == 0 ? finalResults->native_handle() : pPingRT,
                              0,
                              bgfx::Access::Write);

                bgfx::setUniform(u_rect, halfResRect);

                bgfx::setTexture(0,
                                 s_viewspaceDepthSource,
                                 halfDepthsArr[pass]->native_handle(),
                                 SAMPLER_POINT_CLAMP);
                bgfx::setTexture(1,
                                 s_viewspaceDepthSourceMirror,
                                 halfDepthsArr[pass]->native_handle(),
                                 SAMPLER_POINT_MIRROR);
                bgfx::setTexture(2,
                                 s_normalmapSource,
                                 m_settings.generate_normals ? normals->native_handle() : params.normal->native_handle(),
                                 SAMPLER_POINT_CLAMP);

                if(!adaptiveBasePass && (m_settings.quality_level == 3))
                {
                    bgfx::setBuffer(3, m_loadCounter, bgfx::Access::Read);
                    bgfx::setTexture(4, s_importanceMap, importanceMap->native_handle(), SAMPLER_LINEAR_CLAMP);
                    bgfx::setImage(5, finalResults->native_handle(), 0, bgfx::Access::Read);
                }

                // G-buffer normals are octahedral; the generated ones are unorm xyz.
                const gpu_program* generated_normals_programs[5] = {m_generateQ0Program.get(),
                                                                    m_generateQ1Program.get(),
                                                                    m_generateQ2Program.get(),
                                                                    m_generateQ3Program.get(),
                                                                    m_generateQ3BaseProgram.get()};
                const gpu_program* gbuffer_normals_programs[5] = {m_generateQ0GBufferProgram.get(),
                                                                  m_generateQ1GBufferProgram.get(),
                                                                  m_generateQ2GBufferProgram.get(),
                                                                  m_generateQ3GBufferProgram.get(),
                                                                  m_generateQ3BaseGBufferProgram.get()};
                const gpu_program* const* programs =
                    m_settings.generate_normals ? generated_normals_programs : gbuffer_normals_programs;

                int32_t programIndex = bx::max(0, (!adaptiveBasePass) ? (m_settings.quality_level) : (4));

                m_uniforms.m_layer = blurPasses == 0 ? (float)pass : 0.0f;
                m_uniforms.submit();
                gfx::dispatch(view, programs[programIndex]->native_handle(), halfResNumX, halfResNumY);
            }

            // Blur
            if(blurPasses > 0)
            {
                int32_t wideBlursRemaining = bx::max(0, blurPasses - 2);

                for(int32_t i = 0; i < blurPasses; i++)
                {
                    m_uniforms.m_layer = ((i == (blurPasses - 1)) ? (float)pass : 0.0f);
                    m_uniforms.submit();

                    bgfx::setUniform(u_rect, halfResRect);

                    bgfx::setImage(0,
                                   i == (blurPasses - 1) ? finalResults->native_handle() : pPongRT,
                                   0,
                                   bgfx::Access::Write);
                    bgfx::setTexture(1,
                                     s_blurInput,
                                     pPingRT,
                                     m_settings.quality_level > 0 ? SAMPLER_POINT_MIRROR : SAMPLER_LINEAR_CLAMP);

                    if(m_settings.quality_level > 0)
                    {
                        if(wideBlursRemaining > 0)
                        {
                            gfx::dispatch(view, m_smartBlurWideProgram->native_handle(), halfResNumX, halfResNumY);
                            wideBlursRemaining--;
                        }
                        else
                        {
                            gfx::dispatch(view, m_smartBlurProgram->native_handle(), halfResNumX, halfResNumY);
                        }
                    }
                    else
                    {
                        gfx::dispatch(view,
                                       m_nonSmartBlurProgram->native_handle(),
                                       halfResNumX,
                                       halfResNumY); // just for quality level 0 (and -1)
                    }

                    bgfx::TextureHandle temp = pPingRT;
                    pPingRT = pPongRT;
                    pPongRT = temp;
                }
            }
        }

        if(ssaoPass == 0 && m_settings.quality_level == 3)
        { // Generate importance map
            m_uniforms.submit();
            bgfx::setImage(0, importanceMap->native_handle(), 0, bgfx::Access::Write);
            bgfx::setTexture(1, s_finalSSAO, finalResults->native_handle(), SAMPLER_POINT_CLAMP);
            gfx::dispatch(view,
                           m_generateImportanceMapProgram->native_handle(),
                           compute_group_count(dims.quarterSize[0], GROUP_SIZE),
                           compute_group_count(dims.quarterSize[1], GROUP_SIZE));

            m_uniforms.submit();
            bgfx::setImage(0, importanceMapPong->native_handle(), 0, bgfx::Access::Write);
            bgfx::setTexture(1, s_importanceMap, importanceMap->native_handle());
            gfx::dispatch(view,
                           m_postprocessImportanceMapAProgram->native_handle(),
                           compute_group_count(dims.quarterSize[0], GROUP_SIZE),
                           compute_group_count(dims.quarterSize[1], GROUP_SIZE));

            bgfx::setBuffer(0, m_loadCounter, bgfx::Access::ReadWrite);
            gfx::dispatch(view, m_loadCounterClearProgram->native_handle(), 1, 1);

            m_uniforms.submit();
            bgfx::setImage(0, importanceMap->native_handle(), 0, bgfx::Access::Write);
            bgfx::setTexture(1, s_importanceMap, importanceMapPong->native_handle());
            bgfx::setBuffer(2, m_loadCounter, bgfx::Access::ReadWrite);
            gfx::dispatch(view,
                           m_postprocessImportanceMapBProgram->native_handle(),
                           compute_group_count(dims.quarterSize[0], GROUP_SIZE),
                           compute_group_count(dims.quarterSize[1], GROUP_SIZE));
        }
    }

    // Apply
    {
        bgfx::setImage(0, aoMap->native_handle(), 0, bgfx::Access::Write);
        bgfx::setTexture(1, s_finalSSAO, finalResults->native_handle());

        m_uniforms.submit();

        float rect[4] = {(float)dims.fullResOutScissorRect[0],
                         (float)dims.fullResOutScissorRect[1],
                         (float)dims.fullResOutScissorRect[2],
                         (float)dims.fullResOutScissorRect[3]};
        bgfx::setUniform(u_rect, rect);

        const gpu_program* program = m_applyProgram.get();
        if(m_settings.quality_level < 0)
            program = m_nonSmartHalfApplyProgram.get();
        else if(m_settings.quality_level == 0)
            program = m_nonSmartApplyProgram.get();
        gfx::dispatch(view,
                       program->native_handle(),
                       compute_group_count(dims.fullResOutScissorRect[2] - dims.fullResOutScissorRect[0], GROUP_SIZE),
                       compute_group_count(dims.fullResOutScissorRect[3] - dims.fullResOutScissorRect[1], GROUP_SIZE));
    }

    bgfx::discard();
}

auto assao_pass::get_ao_texture(gfx::render_view& rview) const -> gfx::texture::ptr
{
    return rview.tex_find("ASSAO_AO_MAP");
}

auto assao_pass::shutdown() -> int32_t
{
    // Cleanup.

    m_uniforms.destroy();

    bgfx::destroy(u_rect);

    bgfx::destroy(s_normalmapSource);
    bgfx::destroy(s_blurInput);
    bgfx::destroy(s_finalSSAO);
    bgfx::destroy(s_depthSource);
    bgfx::destroy(s_viewspaceDepthSource);
    bgfx::destroy(s_viewspaceDepthSourceMirror);
    bgfx::destroy(s_importanceMap);

    bgfx::destroy(m_loadCounter);

    m_prepareDepthsProgram.reset();
    m_prepareNormalsProgram.reset();
    m_prepareDepthsHalfProgram.reset();
    m_prepareDepthsAndNormalsHalfProgram.reset();
    m_prepareDepthMipProgram.reset();
    m_generateQ0Program.reset();
    m_generateQ1Program.reset();
    m_generateQ2Program.reset();
    m_generateQ3Program.reset();
    m_generateQ3BaseProgram.reset();
    m_generateQ0GBufferProgram.reset();
    m_generateQ1GBufferProgram.reset();
    m_generateQ2GBufferProgram.reset();
    m_generateQ3GBufferProgram.reset();
    m_generateQ3BaseGBufferProgram.reset();
    m_smartBlurProgram.reset();
    m_smartBlurWideProgram.reset();
    m_nonSmartBlurProgram.reset();
    m_applyProgram.reset();
    m_nonSmartApplyProgram.reset();
    m_nonSmartHalfApplyProgram.reset();
    m_generateImportanceMapProgram.reset();
    m_postprocessImportanceMapAProgram.reset();
    m_postprocessImportanceMapBProgram.reset();
    m_loadCounterClearProgram.reset();

    return 0;
}

void assao_pass::create_or_update_frame_buffers(gfx::render_view& rview, const dimensions& dims)
{
    const uint64_t halfDepthFlags = BGFX_TEXTURE_COMPUTE_WRITE | SAMPLER_POINT_CLAMP;
    const usize32_t halfSz{static_cast<uint32_t>(dims.halfSize[0]),
                          static_cast<uint32_t>(dims.halfSize[1])};
    const usize32_t fullSz{static_cast<uint32_t>(dims.size[0]),
                          static_cast<uint32_t>(dims.size[1])};
    const usize32_t quarterSz{static_cast<uint32_t>(dims.quarterSize[0]),
                              static_cast<uint32_t>(dims.quarterSize[1])};

    for(int32_t i = 0; i < 4; i++)
    {
        rview.tex_acquire(
            "ASSAO_HALF_DEPTH_" + std::to_string(i),
            {.size = halfSz, .format = bgfx::TextureFormat::R16F, .flags = halfDepthFlags, .has_mips = true});
    }

    const gfx::texture_desc pingPongDesc{.size = halfSz,
                                         .format = bgfx::TextureFormat::RG8,
                                         .flags = BGFX_TEXTURE_COMPUTE_WRITE,
                                         .layers = 2};
    rview.tex_acquire("ASSAO_PING_PONG_A", pingPongDesc);
    rview.tex_acquire("ASSAO_PING_PONG_B", pingPongDesc);

    rview.tex_acquire("ASSAO_FINAL_RESULTS",
                      {.size = halfSz,
                       .format = bgfx::TextureFormat::RG8,
                       .flags = BGFX_TEXTURE_COMPUTE_WRITE | SAMPLER_LINEAR_CLAMP,
                       .layers = 4});

    rview.tex_acquire("ASSAO_NORMALS",
                      {.size = fullSz, .format = bgfx::TextureFormat::RGBA8, .flags = BGFX_TEXTURE_COMPUTE_WRITE});

    const gfx::texture_desc importanceDesc{.size = quarterSz,
                                           .format = bgfx::TextureFormat::R8,
                                           .flags = BGFX_TEXTURE_COMPUTE_WRITE | SAMPLER_LINEAR_CLAMP};
    rview.tex_acquire("ASSAO_IMPORTANCE_MAP", importanceDesc);
    rview.tex_acquire("ASSAO_IMPORTANCE_MAP_PONG", importanceDesc);

    // RGBA8 with the AO in every channel: the lighting reads the screen-space AO from alpha,
    // the layout GTAO's output has.
    rview.tex_acquire("ASSAO_AO_MAP",
                      {.size = fullSz,
                       .format = bgfx::TextureFormat::RGBA8,
                       .flags = BGFX_TEXTURE_COMPUTE_WRITE | SAMPLER_POINT_CLAMP});
}

void assao_pass::update_uniforms(int32_t _pass, const float* view, const float* proj, const dimensions& dims)
{
    set_vec2(m_uniforms.m_viewportPixelSize, 1.0f / (float)dims.size[0], 1.0f / (float)dims.size[1]);
    set_vec2(m_uniforms.m_halfViewportPixelSize, 1.0f / (float)dims.halfSize[0], 1.0f / (float)dims.halfSize[1]);

    set_vec2(m_uniforms.m_viewport2xPixelSize,
            m_uniforms.m_viewportPixelSize[0] * 2.0f,
            m_uniforms.m_viewportPixelSize[1] * 2.0f);
    set_vec2(m_uniforms.m_viewport2xPixelSize_x_025,
            m_uniforms.m_viewport2xPixelSize[0] * 0.25f,
            m_uniforms.m_viewport2xPixelSize[1] * 0.25f);

    float depthLinearizeMul =
        -proj[3 * 4 + 2]; // float depthLinearizeMul = ( clipFar * clipNear ) / ( clipFar - clipNear );
    float depthLinearizeAdd =
        proj[2 * 4 + 2]; // float depthLinearizeAdd = clipFar / ( clipFar - clipNear );
                         // correct the handedness issue. need to make sure this below is correct, but I think it is.

    if(depthLinearizeMul * depthLinearizeAdd < 0)
    {
        depthLinearizeAdd = -depthLinearizeAdd;
    }

    set_vec2(m_uniforms.m_depthUnpackConsts, depthLinearizeMul, depthLinearizeAdd);

    float tanHalfFOVY = 1.0f / proj[1 * 4 + 1]; // = tan(vertical fov / 2)
    float tanHalfFOVX = 1.0F / proj[0];         // = tanHalfFOVY * aspect

    // Texel rows run bottom-up where render targets keep their origin at the bottom left
    // (OpenGL and OpenGL ES), so view-space y grows with the texel row there.
    if(bgfx::getCaps()->originBottomLeft)
    {
        set_vec2(m_uniforms.m_ndcToViewMul, tanHalfFOVX * 2.0f, tanHalfFOVY * 2.0f);
        set_vec2(m_uniforms.m_ndcToViewAdd, tanHalfFOVX * -1.0f, tanHalfFOVY * -1.0f);
    }
    else
    {
        set_vec2(m_uniforms.m_ndcToViewMul, tanHalfFOVX * 2.0f, tanHalfFOVY * -2.0f);
        set_vec2(m_uniforms.m_ndcToViewAdd, tanHalfFOVX * -1.0f, tanHalfFOVY * 1.0f);
    }

    m_uniforms.m_effectRadius = bx::clamp(m_settings.radius, 0.0f, 100000.0f);
    m_uniforms.m_effectShadowStrength = bx::clamp(m_settings.shadow_multiplier * 4.3f, 0.0f, 10.0f);
    m_uniforms.m_effectShadowPow = bx::clamp(m_settings.shadow_power, 0.0f, 10.0f);
    m_uniforms.m_effectShadowClamp = bx::clamp(m_settings.shadow_clamp, 0.0f, 1.0f);
    m_uniforms.m_effectFadeOutMul = -1.0f / (m_settings.fade_out_to - m_settings.fade_out_from);
    m_uniforms.m_effectFadeOutAdd =
        m_settings.fade_out_from / (m_settings.fade_out_to - m_settings.fade_out_from) + 1.0f;
    m_uniforms.m_effectHorizonAngleThreshold = bx::clamp(m_settings.horizon_angle_threshold, 0.0f, 1.0f);

    // 1.2 seems to be around the best trade off - 1.0 means on-screen radius will stop/slow growing when the camera is
    // at 1.0 distance, so, depending on FOV, basically filling up most of the screen This setting is
    // viewspace-dependent and not screen size dependent intentionally, so that when you change FOV the effect stays
    // (relatively) similar.
    float effectSamplingRadiusNearLimit = (m_settings.radius * 1.2f);

    // if the depth precision is switched to 32bit float, this can be set to something closer to 1 (0.9999 is fine)
    m_uniforms.m_depthPrecisionOffsetMod = 0.9992f;

    // used to get average load per pixel; 9.0 is there to compensate for only doing every 9th atomic add in
    // cs_assao_postprocess_importance_map_b for performance reasons
    m_uniforms.m_loadCounterAvgDiv = 9.0f / (float)(dims.quarterSize[0] * dims.quarterSize[1] * 255.0);

    // Special settings for lowest quality level - just nerf the effect a tiny bit
    if(m_settings.quality_level <= 0)
    {
        effectSamplingRadiusNearLimit *= 1.50f;

        if(m_settings.quality_level < 0)
        {
            m_uniforms.m_effectRadius *= 0.8f;
        }
    }

    effectSamplingRadiusNearLimit /= tanHalfFOVY; // to keep the effect same regardless of FOV

    m_uniforms.m_effectSamplingRadiusNearLimitRec = 1.0f / effectSamplingRadiusNearLimit;

    m_uniforms.m_adaptiveSampleCountLimit = m_settings.adaptive_quality_limit;

    m_uniforms.m_negRecEffectRadius = -1.0f / m_uniforms.m_effectRadius;

    if(bgfx::getCaps()->originBottomLeft)
    {
        set_vec2(m_uniforms.m_perPassFullResCoordOffset, (float)(_pass % 2), 1.0f - (float)(_pass / 2));
        set_vec2(m_uniforms.m_perPassFullResUVOffset,
                ((_pass % 2) - 0.0f) / dims.size[0],
                (1.0f - ((_pass / 2) - 0.0f)) / dims.size[1]);
    }
    else
    {
        set_vec2(m_uniforms.m_perPassFullResCoordOffset, (float)(_pass % 2), (float)(_pass / 2));
        set_vec2(m_uniforms.m_perPassFullResUVOffset,
                ((_pass % 2) - 0.0f) / dims.size[0],
                ((_pass / 2) - 0.0f) / dims.size[1]);
    }

    m_uniforms.m_invSharpness = bx::clamp(1.0f - m_settings.sharpness, 0.0f, 1.0f);
    m_uniforms.m_passIndex = (float)_pass;
    set_vec2(m_uniforms.m_quarterResPixelSize, 1.0f / (float)dims.quarterSize[0], 1.0f / (float)dims.quarterSize[1]);

    float additionalAngleOffset =
        m_settings.temporal_supersampling_angle_offset; // per-frame sample rotation for temporal supersampling
    float additionalRadiusScale =
        m_settings.temporal_supersampling_radius_offset; // per-frame sample radius scale for temporal supersampling
    const int32_t subPassCount = 5;
    for(int32_t subPass = 0; subPass < subPassCount; subPass++)
    {
        int32_t a = _pass;

        int32_t spmap[5]{0, 1, 4, 3, 2};
        int32_t b = spmap[subPass];

        float ca, sa;
        float angle0 = ((float)a + (float)b / (float)subPassCount) * (3.1415926535897932384626433832795f) * 0.5f;
        angle0 += additionalAngleOffset;

        ca = bx::cos(angle0);
        sa = bx::sin(angle0);

        float scale = 1.0f + (a - 1.5f + (b - (subPassCount - 1.0f) * 0.5f) / (float)subPassCount) * 0.07f;
        scale *= additionalRadiusScale;

        set_vec4(m_uniforms.m_patternRotScaleMatrices[subPass], scale * ca, scale * -sa, -scale * sa, -scale * ca);
    }

    m_uniforms.m_normalsUnpackMul = 2.0f;
    m_uniforms.m_normalsUnpackAdd = -1.0f;

    m_uniforms.m_detailAOStrength = m_settings.detail_shadow_strength;

    if(m_settings.generate_normals)
    {
        bx::mtxIdentity(m_uniforms.m_normalsWorldToViewspaceMatrix);
    }
    else
    {
        bx::mtxTranspose(m_uniforms.m_normalsWorldToViewspaceMatrix, view);
    }
}

void assao_pass::release_resources(gfx::render_view& rview)
{
    for(int i = 0; i < 4; ++i)
    {
        rview.tex_remove("ASSAO_HALF_DEPTH_" + std::to_string(i));
    }
    rview.tex_remove("ASSAO_PING_PONG_A");
    rview.tex_remove("ASSAO_PING_PONG_B");
    rview.tex_remove("ASSAO_FINAL_RESULTS");
    rview.tex_remove("ASSAO_NORMALS");
    rview.tex_remove("ASSAO_IMPORTANCE_MAP");
    rview.tex_remove("ASSAO_IMPORTANCE_MAP_PONG");
    rview.tex_remove("ASSAO_AO_MAP");
}

} // namespace unravel
