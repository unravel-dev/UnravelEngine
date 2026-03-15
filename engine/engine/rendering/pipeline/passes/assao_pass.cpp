#include "assao_pass.h"
#include "graphics/graphics.h"
#include <engine/assets/asset_manager.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>
#include <string>

namespace unravel
{

#define SAMPLER_POINT_CLAMP  (BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP)
#define SAMPLER_POINT_MIRROR (BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_MIRROR)
#define SAMPLER_LINEAR_CLAMP (BGFX_SAMPLER_UVW_CLAMP)

#define SSAO_DEPTH_MIP_LEVELS 4

void vec2Set(float* _v, float _x, float _y)
{
    _v[0] = _x;
    _v[1] = _y;
}

void vec4Set(float* _v, float _x, float _y, float _z, float _w)
{
    _v[0] = _x;
    _v[1] = _y;
    _v[2] = _z;
    _v[3] = _w;
}

void vec4iSet(int32_t* _v, int32_t _x, int32_t _y, int32_t _z, int32_t _w)
{
    _v[0] = _x;
    _v[1] = _y;
    _v[2] = _z;
    _v[3] = _w;
}

static const int32_t cMaxBlurPassCount = 6;

auto assao_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();

    auto loadProgram = [&](const std::string& cs)
    {
        auto cs_shader = am.get_asset<gfx::shader>("engine:/data/shaders/assao/" + cs + ".sc");

        auto prog = std::make_shared<gpu_program>(cs_shader);
        m_programs.emplace_back(prog);
        return prog->native_handle();
    };

    // Create uniforms
    u_rect = bgfx::createUniform("u_rect", bgfx::UniformType::Vec4); // viewport/scissor rect for compute
    m_uniforms.init();

    // Create texture sampler uniforms (used when we bind textures)
    s_normal = bgfx::createUniform("s_normal", bgfx::UniformType::Sampler); // Normal gbuffer
    s_depth = bgfx::createUniform("s_depth", bgfx::UniformType::Sampler);   // Depth gbuffer

    // clang-format off
    s_ao                         = bgfx::createUniform("s_ao", bgfx::UniformType::Sampler);
    s_blurInput                  = bgfx::createUniform("s_blurInput", bgfx::UniformType::Sampler);
    s_finalSSAO                  = bgfx::createUniform("s_finalSSAO", bgfx::UniformType::Sampler);
    s_depthSource                = bgfx::createUniform("s_depthSource", bgfx::UniformType::Sampler);
    s_viewspaceDepthSource       = bgfx::createUniform("s_viewspaceDepthSource", bgfx::UniformType::Sampler);
    s_viewspaceDepthSourceMirror = bgfx::createUniform("s_viewspaceDepthSourceMirror", bgfx::UniformType::Sampler);
    s_importanceMap              = bgfx::createUniform("s_importanceMap", bgfx::UniformType::Sampler);
    // clang-format on

    // Create program from shaders.

    // clang-format off
    m_prepareDepthsProgram               = loadProgram("cs_assao_prepare_depths");
    m_prepareDepthsAndNormalsProgram     = loadProgram("cs_assao_prepare_depths_and_normals");
    m_prepareDepthsHalfProgram           = loadProgram("cs_assao_prepare_depths_half");
    m_prepareDepthsAndNormalsHalfProgram = loadProgram("cs_assao_prepare_depths_and_normals_half");
    m_prepareDepthMipProgram             = loadProgram("cs_assao_prepare_depth_mip");
    m_generateQ0Program                  = loadProgram("cs_assao_generate_q0");
    m_generateQ1Program                  = loadProgram("cs_assao_generate_q1");
    m_generateQ2Program                  = loadProgram("cs_assao_generate_q2");
    m_generateQ3Program                  = loadProgram("cs_assao_generate_q3");
    m_generateQ3BaseProgram              = loadProgram("cs_assao_generate_q3base");
    m_generateQ0ProgramRgba16f           = loadProgram("cs_assao_generate_q0_normal_rgba16f");
    m_generateQ1ProgramRgba16f           = loadProgram("cs_assao_generate_q1_normal_rgba16f");
    m_generateQ2ProgramRgba16f           = loadProgram("cs_assao_generate_q2_normal_rgba16f");
    m_generateQ3ProgramRgba16f           = loadProgram("cs_assao_generate_q3_normal_rgba16f");
    m_generateQ3BaseProgramRgba16f       = loadProgram("cs_assao_generate_q3base_normal_rgba16f");
    m_smartBlurProgram                   = loadProgram("cs_assao_smart_blur");
    m_smartBlurWideProgram               = loadProgram("cs_assao_smart_blur_wide");
    m_nonSmartBlurProgram                = loadProgram("cs_assao_non_smart_blur");
    m_applyProgram                       = loadProgram("cs_assao_apply");
    m_nonSmartApplyProgram               = loadProgram("cs_assao_non_smart_apply");
    m_nonSmartHalfApplyProgram           = loadProgram("cs_assao_non_smart_half_apply");
    m_generateImportanceMapProgram       = loadProgram("cs_assao_generate_importance_map");
    m_postprocessImportanceMapAProgram   = loadProgram("cs_assao_postprocess_importance_map_a");
    m_postprocessImportanceMapBProgram   = loadProgram("cs_assao_postprocess_importance_map_b");
    m_loadCounterClearProgram            = loadProgram("cs_assao_load_counter_clear");
    m_updateGBufferProgram               = loadProgram("cs_assao_update_g_buffer");
    // clang-format on

    m_loadCounter = bgfx::createDynamicIndexBuffer(1, BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_INDEX32);

    return true;
}

void assao_pass::run(const camera& cam, gfx::render_view& rview, const run_params& params)
{
    m_settings = params.params;

    const auto size = params.depth->get_size();
    const int32_t width = static_cast<int32_t>(size.width);
    const int32_t height = static_cast<int32_t>(size.height);

    dimensions dims;
    const int32_t border = 0;
    dims.size[0] = width + 2 * border;
    dims.size[1] = height + 2 * border;
    dims.halfSize[0] = (dims.size[0] + 1) / 2;
    dims.halfSize[1] = (dims.size[1] + 1) / 2;
    dims.quarterSize[0] = (dims.halfSize[0] + 1) / 2;
    dims.quarterSize[1] = (dims.halfSize[1] + 1) / 2;
    vec4iSet(dims.fullResOutScissorRect, border, border, width + border, height + border);
    vec4iSet(dims.halfResOutScissorRect,
             dims.fullResOutScissorRect[0] / 2,
             dims.fullResOutScissorRect[1] / 2,
             (dims.fullResOutScissorRect[2] + 1) / 2,
             (dims.fullResOutScissorRect[3] + 1) / 2);
    const int32_t blurEnlarge =
        cMaxBlurPassCount + bx::max(0, cMaxBlurPassCount - 2);
    vec4iSet(dims.halfResOutScissorRect,
             bx::max(0, dims.halfResOutScissorRect[0] - blurEnlarge),
             bx::max(0, dims.halfResOutScissorRect[1] - blurEnlarge),
             bx::min(dims.halfSize[0], dims.halfResOutScissorRect[2] + blurEnlarge),
             bx::min(dims.halfSize[1], dims.halfResOutScissorRect[3] + blurEnlarge));

    create_or_update_frame_buffers(rview, dims);

    const float* viewMtx = cam.get_view();
    float projMtx[16];

    float n = cam.get_near_clip();
    float f = cam.get_far_clip();

    if(cam.get_projection_mode() == projection_mode::perspective)
    {
        auto fovy = cam.get_fov();
        bx::mtxProj(projMtx, fovy, float(dims.size[0]) / float(dims.size[1]), n, f, false);
    }
    else
    {
        float zoom = cam.get_zoom_factor();
        const frect_t rect = {-(float(dims.size[0]) / 2.0f) * zoom,
                              (float(dims.size[1]) / 2.0f) * zoom,
                              (float(dims.size[0]) / 2.0f) * zoom,
                              -(float(dims.size[1]) / 2.0f) * zoom};

        bx::mtxOrtho(projMtx, rect.left, rect.right, rect.bottom, rect.top, n, f, 0, false);
    }

    // ASSAO passes
#if USE_ASSAO == 0
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

    gfx::render_pass pass("ASSAO Pass");
    auto view = pass.id;

    {
        bgfx::setTexture(0, s_depthSource, params.depth->native_handle(), SAMPLER_POINT_CLAMP);
        m_uniforms.submit();

        if(m_settings.generate_normals)
        {
            bgfx::setImage(5, normals->native_handle(), 0, bgfx::Access::Write);
        }

        if(m_settings.quality_level < 0)
        {
            for(int32_t j = 0; j < 2; ++j)
            {
                bgfx::setImage((uint8_t)(j + 1),
                              (j == 0 ? halfDepths0 : halfDepths3)->native_handle(),
                              0,
                              bgfx::Access::Write);
            }

            bgfx::dispatch(view,
                           m_settings.generate_normals ? m_prepareDepthsAndNormalsHalfProgram
                                                       : m_prepareDepthsHalfProgram,
                           (dims.halfSize[0] + 7) / 8,
                           (dims.halfSize[1] + 7) / 8);
        }
        else
        {
            bgfx::setImage(1, halfDepths0->native_handle(), 0, bgfx::Access::Write);
            bgfx::setImage(2, halfDepths1->native_handle(), 0, bgfx::Access::Write);
            bgfx::setImage(3, halfDepths2->native_handle(), 0, bgfx::Access::Write);
            bgfx::setImage(4, halfDepths3->native_handle(), 0, bgfx::Access::Write);

            bgfx::dispatch(view,
                           m_settings.generate_normals ? m_prepareDepthsAndNormalsProgram : m_prepareDepthsProgram,
                           (dims.halfSize[0] + 7) / 8,
                           (dims.halfSize[1] + 7) / 8);
        }
    }

    // only do mipmaps for higher quality levels (not beneficial on quality level 1, and detrimental on quality level 0)
    if(m_settings.quality_level > 1)
    {
        uint16_t mipWidth = (uint16_t)dims.halfSize[0];
        uint16_t mipHeight = (uint16_t)dims.halfSize[1];

        for(uint8_t i = 1; i < SSAO_DEPTH_MIP_LEVELS; i++)
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

            bgfx::dispatch(view, m_prepareDepthMipProgram, (mipWidth + 7) / 8, (mipHeight + 7) / 8);
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
            gfx::render_pass pass(fmt::format("Importance Map Pass {}", ssaoPass).c_str());
            view = pass.id;
        }

        bool adaptiveBasePass = (ssaoPass == 0);

        BX_UNUSED(adaptiveBasePass);

        int32_t passCount = 4;

        int32_t halfResNumX = (dims.halfResOutScissorRect[2] - dims.halfResOutScissorRect[0] + 7) / 8;
        int32_t halfResNumY = (dims.halfResOutScissorRect[3] - dims.halfResOutScissorRect[1] + 7) / 8;
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
            blurPasses = bx::min(blurPasses, cMaxBlurPassCount);

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
                if(m_settings.generate_normals)
                {
                    bgfx::setImage(2, normals->native_handle(), 0, bgfx::Access::Read);
                }
                else
                {
                    bgfx::setImage(2, params.normal->native_handle(), 0, bgfx::Access::Read);
                }

                if(!adaptiveBasePass && (m_settings.quality_level == 3))
                {
                    bgfx::setBuffer(3, m_loadCounter, bgfx::Access::Read);
                    bgfx::setTexture(4, s_importanceMap, importanceMap->native_handle(), SAMPLER_LINEAR_CLAMP);
                    bgfx::setImage(5, finalResults->native_handle(), 0, bgfx::Access::Read);
                }

                bgfx::ProgramHandle programsNormal[5] = {m_generateQ0Program,
                                                         m_generateQ1Program,
                                                         m_generateQ2Program,
                                                         m_generateQ3Program,
                                                         m_generateQ3BaseProgram};

                bgfx::ProgramHandle programsRgba16f[5] = {m_generateQ0ProgramRgba16f,
                                                          m_generateQ1ProgramRgba16f,
                                                          m_generateQ2ProgramRgba16f,
                                                          m_generateQ3ProgramRgba16f,
                                                          m_generateQ3BaseProgramRgba16f};

                bgfx::ProgramHandle* programs = programsNormal;

                if(!m_settings.generate_normals)
                {
                    programs = programsRgba16f;
                }

                int32_t programIndex = bx::max(0, (!adaptiveBasePass) ? (m_settings.quality_level) : (4));

                m_uniforms.m_layer = blurPasses == 0 ? (float)pass : 0.0f;
                m_uniforms.submit();
                bgfx::dispatch(view, programs[programIndex], halfResNumX, halfResNumY);
            }

            // Blur
            if(blurPasses > 0)
            {
                int32_t wideBlursRemaining = bx::max(0, blurPasses - 2);

                for(int32_t i = 0; i < blurPasses; i++)
                {
                    bgfx::setViewFrameBuffer(view, BGFX_INVALID_HANDLE);
                    bgfx::touch(view);

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
                            bgfx::dispatch(view, m_smartBlurWideProgram, halfResNumX, halfResNumY);
                            wideBlursRemaining--;
                        }
                        else
                        {
                            bgfx::dispatch(view, m_smartBlurProgram, halfResNumX, halfResNumY);
                        }
                    }
                    else
                    {
                        bgfx::dispatch(view,
                                       m_nonSmartBlurProgram,
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
            bgfx::dispatch(view,
                           m_generateImportanceMapProgram,
                           (dims.quarterSize[0] + 7) / 8,
                           (dims.quarterSize[1] + 7) / 8);

            m_uniforms.submit();
            bgfx::setImage(0, importanceMapPong->native_handle(), 0, bgfx::Access::Write);
            bgfx::setTexture(1, s_importanceMap, importanceMap->native_handle());
            bgfx::dispatch(view,
                           m_postprocessImportanceMapAProgram,
                           (dims.quarterSize[0] + 7) / 8,
                           (dims.quarterSize[1] + 7) / 8);

            bgfx::setBuffer(0, m_loadCounter, bgfx::Access::ReadWrite);
            bgfx::dispatch(view, m_loadCounterClearProgram, 1, 1);

            m_uniforms.submit();
            bgfx::setImage(0, importanceMap->native_handle(), 0, bgfx::Access::Write);
            bgfx::setTexture(1, s_importanceMap, importanceMapPong->native_handle());
            bgfx::setBuffer(2, m_loadCounter, bgfx::Access::ReadWrite);
            bgfx::dispatch(view,
                           m_postprocessImportanceMapBProgram,
                           (dims.quarterSize[0] + 7) / 8,
                           (dims.quarterSize[1] + 7) / 8);
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

        bgfx::ProgramHandle program;
        if(m_settings.quality_level < 0)
            program = m_nonSmartHalfApplyProgram;
        else if(m_settings.quality_level == 0)
            program = m_nonSmartApplyProgram;
        else
            program = m_applyProgram;
        bgfx::dispatch(view,
                       program,
                       (dims.fullResOutScissorRect[2] - dims.fullResOutScissorRect[0] + 7) / 8,
                       (dims.fullResOutScissorRect[3] - dims.fullResOutScissorRect[1] + 7) / 8);
    }

#endif

    {
        gfx::render_pass pass("Update G-Buffer AO Pass");
        const auto& aoMapTex = rview.tex_get("ASSAO_AO_MAP");
        bgfx::setImage(0, params.color_ao->native_handle(), 0, bgfx::Access::ReadWrite);
        bgfx::setImage(1, aoMapTex->native_handle(), 0, bgfx::Access::Read);

        bgfx::dispatch(pass.id, m_updateGBufferProgram, (dims.size[0] + 7) / 8, (dims.size[1] + 7) / 8);
    }

    gfx::discard();
}

auto assao_pass::shutdown() -> int32_t
{
    // Cleanup.

    m_uniforms.destroy();

    bgfx::destroy(u_rect);

    bgfx::destroy(s_normal);
    bgfx::destroy(s_depth);
    bgfx::destroy(s_ao);
    bgfx::destroy(s_blurInput);
    bgfx::destroy(s_finalSSAO);
    bgfx::destroy(s_depthSource);
    bgfx::destroy(s_viewspaceDepthSource);
    bgfx::destroy(s_viewspaceDepthSourceMirror);
    bgfx::destroy(s_importanceMap);

    bgfx::destroy(m_loadCounter);

    m_programs.clear();

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
        auto key = "ASSAO_HALF_DEPTH_" + std::to_string(i);
        auto& tex = rview.tex_get_or_emplace(key);
        if(!tex || tex->get_size() != halfSz)
        {
            tex = std::make_shared<gfx::texture>(uint16_t(dims.halfSize[0]),
                                                 uint16_t(dims.halfSize[1]),
                                                 true,
                                                 1,
                                                 gfx::texture_format::R16F,
                                                 halfDepthFlags);
        }
    }

    auto& pingA = rview.tex_get_or_emplace("ASSAO_PING_PONG_A");
    if(!pingA || pingA->get_size() != halfSz)
    {
        pingA = std::make_shared<gfx::texture>(uint16_t(dims.halfSize[0]),
                                               uint16_t(dims.halfSize[1]),
                                               false,
                                               2,
                                               gfx::texture_format::RG8,
                                               BGFX_TEXTURE_COMPUTE_WRITE);
    }
    auto& pingB = rview.tex_get_or_emplace("ASSAO_PING_PONG_B");
    if(!pingB || pingB->get_size() != halfSz)
    {
        pingB = std::make_shared<gfx::texture>(uint16_t(dims.halfSize[0]),
                                               uint16_t(dims.halfSize[1]),
                                               false,
                                               2,
                                               gfx::texture_format::RG8,
                                               BGFX_TEXTURE_COMPUTE_WRITE);
    }

    auto& finalRes = rview.tex_get_or_emplace("ASSAO_FINAL_RESULTS");
    if(!finalRes || finalRes->get_size() != halfSz)
    {
        finalRes = std::make_shared<gfx::texture>(uint16_t(dims.halfSize[0]),
                                                  uint16_t(dims.halfSize[1]),
                                                  false,
                                                  4,
                                                  gfx::texture_format::RG8,
                                                  BGFX_TEXTURE_COMPUTE_WRITE | SAMPLER_LINEAR_CLAMP);
    }

    auto& normals = rview.tex_get_or_emplace("ASSAO_NORMALS");
    if(!normals || normals->get_size() != fullSz)
    {
        normals = std::make_shared<gfx::texture>(uint16_t(dims.size[0]),
                                                 uint16_t(dims.size[1]),
                                                 false,
                                                 1,
                                                 gfx::texture_format::RGBA8,
                                                 BGFX_TEXTURE_COMPUTE_WRITE);
    }

    auto& importanceMap = rview.tex_get_or_emplace("ASSAO_IMPORTANCE_MAP");
    if(!importanceMap || importanceMap->get_size() != quarterSz)
    {
        importanceMap = std::make_shared<gfx::texture>(uint16_t(dims.quarterSize[0]),
                                                       uint16_t(dims.quarterSize[1]),
                                                       false,
                                                       1,
                                                       gfx::texture_format::R8,
                                                       BGFX_TEXTURE_COMPUTE_WRITE | SAMPLER_LINEAR_CLAMP);
    }
    auto& importanceMapPong = rview.tex_get_or_emplace("ASSAO_IMPORTANCE_MAP_PONG");
    if(!importanceMapPong || importanceMapPong->get_size() != quarterSz)
    {
        importanceMapPong = std::make_shared<gfx::texture>(uint16_t(dims.quarterSize[0]),
                                                          uint16_t(dims.quarterSize[1]),
                                                           false,
                                                           1,
                                                           gfx::texture_format::R8,
                                                           BGFX_TEXTURE_COMPUTE_WRITE | SAMPLER_LINEAR_CLAMP);
    }

    auto& aoMap = rview.tex_get_or_emplace("ASSAO_AO_MAP");
    if(!aoMap || aoMap->get_size() != fullSz)
    {
        aoMap = std::make_shared<gfx::texture>(uint16_t(dims.size[0]),
                                               uint16_t(dims.size[1]),
                                               false,
                                               1,
                                               gfx::texture_format::R8,
                                               BGFX_TEXTURE_COMPUTE_WRITE | SAMPLER_POINT_CLAMP);
    }
}

void assao_pass::update_uniforms(int32_t _pass, const float* view, const float* proj, const dimensions& dims)
{
    vec2Set(m_uniforms.m_viewportPixelSize, 1.0f / (float)dims.size[0], 1.0f / (float)dims.size[1]);
    vec2Set(m_uniforms.m_halfViewportPixelSize, 1.0f / (float)dims.halfSize[0], 1.0f / (float)dims.halfSize[1]);

    vec2Set(m_uniforms.m_viewport2xPixelSize,
            m_uniforms.m_viewportPixelSize[0] * 2.0f,
            m_uniforms.m_viewportPixelSize[1] * 2.0f);
    vec2Set(m_uniforms.m_viewport2xPixelSize_x_025,
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

    vec2Set(m_uniforms.m_depthUnpackConsts, depthLinearizeMul, depthLinearizeAdd);

    float tanHalfFOVY = 1.0f / proj[1 * 4 + 1]; // = tanf( drawContext.Camera.GetYFOV( ) * 0.5f );
    float tanHalfFOVX = 1.0F / proj[0];         // = tanHalfFOVY * drawContext.Camera.GetAspect( );

    if(bgfx::getRendererType() == bgfx::RendererType::OpenGL)
    {
        vec2Set(m_uniforms.m_ndcToViewMul, tanHalfFOVX * 2.0f, tanHalfFOVY * 2.0f);
        vec2Set(m_uniforms.m_ndcToViewAdd, tanHalfFOVX * -1.0f, tanHalfFOVY * -1.0f);
    }
    else
    {
        vec2Set(m_uniforms.m_ndcToViewMul, tanHalfFOVX * 2.0f, tanHalfFOVY * -2.0f);
        vec2Set(m_uniforms.m_ndcToViewAdd, tanHalfFOVX * -1.0f, tanHalfFOVY * 1.0f);
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

    // used to get average load per pixel; 9.0 is there to compensate for only doing every 9th InterlockedAdd in
    // PSPostprocessImportanceMapB for performance reasons
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
        vec2Set(m_uniforms.m_perPassFullResCoordOffset, (float)(_pass % 2), 1.0f - (float)(_pass / 2));
        vec2Set(m_uniforms.m_perPassFullResUVOffset,
                ((_pass % 2) - 0.0f) / dims.size[0],
                (1.0f - ((_pass / 2) - 0.0f)) / dims.size[1]);
    }
    else
    {
        vec2Set(m_uniforms.m_perPassFullResCoordOffset, (float)(_pass % 2), (float)(_pass / 2));
        vec2Set(m_uniforms.m_perPassFullResUVOffset,
                ((_pass % 2) - 0.0f) / dims.size[0],
                ((_pass / 2) - 0.0f) / dims.size[1]);
    }

    m_uniforms.m_invSharpness = bx::clamp(1.0f - m_settings.sharpness, 0.0f, 1.0f);
    m_uniforms.m_passIndex = (float)_pass;
    vec2Set(m_uniforms.m_quarterResPixelSize, 1.0f / (float)dims.quarterSize[0], 1.0f / (float)dims.quarterSize[1]);

    float additionalAngleOffset =
        m_settings.temporal_supersampling_angle_offset; // if using temporal supersampling approach (like "Progressive
                                                        // Rendering Using Multi-frame Sampling" from GPU Pro 7, etc.)
    float additionalRadiusScale =
        m_settings.temporal_supersampling_radius_offset; // if using temporal supersampling approach (like "Progressive
                                                         // Rendering Using Multi-frame Sampling" from GPU Pro 7, etc.)
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

        vec4Set(m_uniforms.m_patternRotScaleMatrices[subPass], scale * ca, scale * -sa, -scale * sa, -scale * ca);
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
