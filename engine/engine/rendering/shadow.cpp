#include "shadow.h"

#include <engine/assets/asset_manager.h>
#include <engine/rendering/pipeline/deferred/pipeline.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/model.h>
#include <engine/rendering/renderer.h>

#include <graphics/index_buffer.h>
#include <graphics/render_pass.h>
#include <graphics/render_view.h>
#include <graphics/texture.h>
#include <graphics/vertex_buffer.h>

namespace unravel
{
namespace shadow
{

namespace
{

// clang-format off
RenderState render_states[RenderState::Count] =
    {
        { // Default
            0
                | BGFX_STATE_WRITE_RGB
                | BGFX_STATE_WRITE_A
                | BGFX_STATE_DEPTH_TEST_LESS
                | BGFX_STATE_WRITE_Z
                | BGFX_STATE_CULL_CCW
                | BGFX_STATE_MSAA
            , UINT32_MAX
            , BGFX_STENCIL_NONE
            , BGFX_STENCIL_NONE
        },
        { // ShadowMap_PackDepth
            0
                | BGFX_STATE_WRITE_RGB
                | BGFX_STATE_WRITE_A
                | BGFX_STATE_WRITE_Z
                | BGFX_STATE_DEPTH_TEST_LESS
                | BGFX_STATE_CULL_CCW
                | BGFX_STATE_MSAA
            , UINT32_MAX
            , BGFX_STENCIL_NONE
            , BGFX_STENCIL_NONE
        },
        { // ShadowMap_PackDepthHoriz
            0
                | BGFX_STATE_WRITE_RGB
                | BGFX_STATE_WRITE_A
                | BGFX_STATE_WRITE_Z
                | BGFX_STATE_DEPTH_TEST_LESS
                | BGFX_STATE_CULL_CCW
                | BGFX_STATE_MSAA
            , UINT32_MAX
            , BGFX_STENCIL_TEST_EQUAL
                | BGFX_STENCIL_FUNC_REF(1)
                | BGFX_STENCIL_FUNC_RMASK(0xff)
                | BGFX_STENCIL_OP_FAIL_S_KEEP
                | BGFX_STENCIL_OP_FAIL_Z_KEEP
                | BGFX_STENCIL_OP_PASS_Z_KEEP
            , BGFX_STENCIL_NONE
        },
        { // ShadowMap_PackDepthVert
            0
                | BGFX_STATE_WRITE_RGB
                | BGFX_STATE_WRITE_A
                | BGFX_STATE_WRITE_Z
                | BGFX_STATE_DEPTH_TEST_LESS
                | BGFX_STATE_CULL_CCW
                | BGFX_STATE_MSAA
            , UINT32_MAX
            , BGFX_STENCIL_TEST_EQUAL
                | BGFX_STENCIL_FUNC_REF(0)
                | BGFX_STENCIL_FUNC_RMASK(0xff)
                | BGFX_STENCIL_OP_FAIL_S_KEEP
                | BGFX_STENCIL_OP_FAIL_Z_KEEP
                | BGFX_STENCIL_OP_PASS_Z_KEEP
            , BGFX_STENCIL_NONE
        },
        { // Custom_BlendLightTexture
            BGFX_STATE_WRITE_RGB
                | BGFX_STATE_WRITE_A
                | BGFX_STATE_WRITE_Z
                | BGFX_STATE_DEPTH_TEST_LESS
                | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_COLOR, BGFX_STATE_BLEND_INV_SRC_COLOR)
                | BGFX_STATE_CULL_CCW
                | BGFX_STATE_MSAA
            , UINT32_MAX
            , BGFX_STENCIL_NONE
            , BGFX_STENCIL_NONE
        },
        { // Custom_DrawPlaneBottom
            BGFX_STATE_WRITE_RGB
                | BGFX_STATE_CULL_CW
                | BGFX_STATE_MSAA
            , UINT32_MAX
            , BGFX_STENCIL_NONE
            , BGFX_STENCIL_NONE
        },
        };
// clang-format on

auto convert(light_type t) -> LightType::Enum
{
    static_assert(std::uint8_t(light_type::count) == std::uint8_t(LightType::Count), "Missing impl");

    switch(t)
    {
        case light_type::spot:
            return LightType::SpotLight;
        case light_type::point:
            return LightType::PointLight;
        default:
            return LightType::DirectionalLight;
    }
}

auto convert(sm_impl t) -> SmImpl::Enum
{
    static_assert(std::uint8_t(sm_impl::count) == std::uint8_t(SmImpl::Count), "Missing impl");

    switch(t)
    {
        case sm_impl::hard:
            return SmImpl::Hard;

        case sm_impl::pcf:
            return SmImpl::PCF;

        case sm_impl::pcss:
            return SmImpl::PCSS;

        case sm_impl::esm:
            return SmImpl::ESM;

        case sm_impl::vsm:
            return SmImpl::VSM;
        default:
            return SmImpl::Count;
    }
}

auto convert(sm_depth t) -> DepthImpl::Enum
{
    static_assert(std::uint8_t(sm_depth::count) == std::uint8_t(DepthImpl::Count), "Missing impl");
    switch(t)
    {
        case sm_depth::invz:
            return DepthImpl::InvZ;

        case sm_depth::linear:
            return DepthImpl::Linear;

        default:
            return DepthImpl::Count;
    }
}

auto convert(sm_resolution t) -> float
{
    switch(t)
    {
        case sm_resolution::low:
            return 8;

        case sm_resolution::medium:
            return 9;

        case sm_resolution::high:
            return 10;

        case sm_resolution::very_high:
            return 11;

        default:
            return 10;
    }
}

void mtxYawPitchRoll(float* _result, float _yaw, float _pitch, float _roll)
{
    float sroll = bx::sin(_roll);
    float croll = bx::cos(_roll);
    float spitch = bx::sin(_pitch);
    float cpitch = bx::cos(_pitch);
    float syaw = bx::sin(_yaw);
    float cyaw = bx::cos(_yaw);

    _result[0] = sroll * spitch * syaw + croll * cyaw;
    _result[1] = sroll * cpitch;
    _result[2] = sroll * spitch * cyaw - croll * syaw;
    _result[3] = 0.0f;
    _result[4] = croll * spitch * syaw - sroll * cyaw;
    _result[5] = croll * cpitch;
    _result[6] = croll * spitch * cyaw + sroll * syaw;
    _result[7] = 0.0f;
    _result[8] = cpitch * syaw;
    _result[9] = -spitch;
    _result[10] = cpitch * cyaw;
    _result[11] = 0.0f;
    _result[12] = 0.0f;
    _result[13] = 0.0f;
    _result[14] = 0.0f;
    _result[15] = 1.0f;
}

void screenSpaceQuad(bool _originBottomLeft = true, float _width = 1.0f, float _height = 1.0f)
{
    if(3 == bgfx::getAvailTransientVertexBuffer(3, PosColorTexCoord0Vertex::get_layout()))
    {
        bgfx::TransientVertexBuffer vb;
        bgfx::allocTransientVertexBuffer(&vb, 3, PosColorTexCoord0Vertex::get_layout());
        PosColorTexCoord0Vertex* vertex = (PosColorTexCoord0Vertex*)vb.data;

        const float zz = 0.0f;

        const float minx = -_width;
        const float maxx = _width;
        const float miny = 0.0f;
        const float maxy = _height * 2.0f;

        const float minu = -1.0f;
        const float maxu = 1.0f;

        float minv = 0.0f;
        float maxv = 2.0f;

        if(_originBottomLeft)
        {
            std::swap(minv, maxv);
            minv -= 1.0f;
            maxv -= 1.0f;
        }

        vertex[0].m_x = minx;
        vertex[0].m_y = miny;
        vertex[0].m_z = zz;
        vertex[0].m_rgba = 0xffffffff;
        vertex[0].m_u = minu;
        vertex[0].m_v = minv;

        vertex[1].m_x = maxx;
        vertex[1].m_y = miny;
        vertex[1].m_z = zz;
        vertex[1].m_rgba = 0xffffffff;
        vertex[1].m_u = maxu;
        vertex[1].m_v = minv;

        vertex[2].m_x = maxx;
        vertex[2].m_y = maxy;
        vertex[2].m_z = zz;
        vertex[2].m_rgba = 0xffffffff;
        vertex[2].m_u = maxu;
        vertex[2].m_v = maxv;

        bgfx::setVertexBuffer(0, &vb);
    }
}

void worldSpaceFrustumCorners(
    float* _corners24f
  , float _near
  , float _far
  , float _projWidth
  , float _projHeight
  , const float* _invViewMtx
  )
{
  // Define frustum corners in view space.
  const float nw = _near * _projWidth;
  const float nh = _near * _projHeight;
  const float fw = _far  * _projWidth;
  const float fh = _far  * _projHeight;

  const uint8_t numCorners = 8;
  const bx::Vec3 corners[numCorners] =
  {
      { -nw,  nh, _near },
      {  nw,  nh, _near },
      {  nw, -nh, _near },
      { -nw, -nh, _near },
      { -fw,  fh, _far  },
      {  fw,  fh, _far  },
      {  fw, -fh, _far  },
      { -fw, -fh, _far  },
  };

  // Convert them to world space.
  float (*out)[3] = (float(*)[3])_corners24f;
  for (uint8_t ii = 0; ii < numCorners; ++ii)
  {
      bx::store(&out[ii], bx::mul(corners[ii], _invViewMtx) );
  }
}

/**
* Calculate cascade split depths based on view camera frustum using GPU Gems 3 method.
* Based on: https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
* 
* @param cascadeSplits Output array of normalized split depths [0..1] for each cascade
* @param numSplits Number of cascade splits (typically 4)
* @param nearClip Near clip plane distance
* @param farClip Far clip plane distance  
* @param splitLambda Blend factor between logarithmic (1.0) and uniform (0.0) distribution
*/
void calculateCascadeSplits(float* cascadeSplits, uint8_t numSplits, float nearClip, float farClip, float splitLambda)
{
    const float clipRange = farClip - nearClip;
    const float minZ = nearClip;
    const float maxZ = nearClip + clipRange;
    const float range = maxZ - minZ;
    const float ratio = maxZ / minZ;

    // Calculate split depths based on view camera frustum
    // Use a modified indexing similar to the original implementation for better distribution feel
    // The original used odd indices (1,3,5,7) out of (num_splits*2), giving more weight to near cascades
    const float numSlicesF = float(numSplits) * 2.5f;
    
    for(uint32_t i = 0; i < numSplits; i++)
    {
        // Use odd indices like the original: 1, 3, 5, 7 for 4 splits
        const float si = float(i * 2 + 1) / numSlicesF;
        const float logSplit = minZ * bx::pow(ratio, si);
        const float uniformSplit = minZ + range * si;
        const float d = splitLambda * (logSplit - uniformSplit) + uniformSplit;
        cascadeSplits[i] = (d - nearClip) / clipRange;
    }
}

} // namespace

shadowmap_generator::shadowmap_generator()
{
    init(engine::context());
}

shadowmap_generator::~shadowmap_generator()
{
    deinit();
}

void shadowmap_generator::deinit()
{
    deinit_uniforms();
    deinit_textures();

    uniforms_.destroy();
    programs_.destroy();
}

void shadowmap_generator::deinit_textures()
{
    if(!valid_)
    {
        return;
    }

    valid_ = false;

    for(int i = 0; i < ShadowMapRenderTargets::Count; ++i)
    {
        if(bgfx::isValid(rt_shadow_map_[i]))
        {
            bgfx::destroy(rt_shadow_map_[i]);
            rt_shadow_map_[i] = {bgfx::kInvalidHandle};
        }
    }

    if(bgfx::isValid(rt_blur_))
    {
        bgfx::destroy(rt_blur_);
        rt_blur_ = {bgfx::kInvalidHandle};
    }
}

void shadowmap_generator::deinit_uniforms()
{
    if(bgfx::isValid(tex_color_))
    {
        bgfx::destroy(tex_color_);
    }

    for(int i = 0; i < ShadowMapRenderTargets::Count; ++i)
    {
        if(bgfx::isValid(shadow_map_[i]))
        {
            bgfx::destroy(shadow_map_[i]);
        }
    }
}

void shadowmap_generator::init(rtti::context& ctx)
{
    if(bgfx::isValid(tex_color_))
    {
        return;
    }
    // Uniforms.
    uniforms_.init();
    tex_color_ = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
    shadow_map_[0] = bgfx::createUniform("s_shadowMap0", bgfx::UniformType::Sampler);
    shadow_map_[1] = bgfx::createUniform("s_shadowMap1", bgfx::UniformType::Sampler);
    shadow_map_[2] = bgfx::createUniform("s_shadowMap2", bgfx::UniformType::Sampler);
    shadow_map_[3] = bgfx::createUniform("s_shadowMap3", bgfx::UniformType::Sampler);

    for(int i = 0; i < ShadowMapRenderTargets::Count; ++i)
    {
        rt_shadow_map_[i] = {bgfx::kInvalidHandle};
    }

    // Programs.
    programs_.init(ctx);

    // Lights.
    // clang-format off
    point_light_ =
        {
            { { 0.0f, 0.0f, 0.0f, 1.0f   } }, //position
            { { 0.0f,-0.4f,-0.6f, 0.0f   } }, //spotdirection, spotexponent
        };

    directional_light_ =
        {
            { { 0.5f,-1.0f, 0.1f, 0.0f  } }, //position
            { { 0.0f, 0.0f, 0.0f, 1.0f  } }, //spotdirection, spotexponent
        };

    // clang-format on

    // Setup uniforms.
    color_[0] = color_[1] = color_[2] = color_[3] = 1.0f;
    uniforms_.setPtrs(&point_light_,
                      color_,
                      light_mtx_,
                      &shadow_map_mtx_[ShadowMapRenderTargets::First][0],
                      &shadow_map_mtx_[ShadowMapRenderTargets::Second][0],
                      &shadow_map_mtx_[ShadowMapRenderTargets::Third][0],
                      &shadow_map_mtx_[ShadowMapRenderTargets::Fourth][0]);
    // uniforms_.submitConstUniforms();

    // clang-format off
    // Settings.
    ShadowMapSettings smSettings[LightType::Count][DepthImpl::Count][SmImpl::Count] =
    {
        { //LightType::Spot

            { //DepthImpl::InvZ

                { //SmImpl::Hard
                    10.0f, 7.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 10.0f, 1.0f, 20.0f, 1.0f         // m_depthValuePow
                    , 1.0f, 1.0f, 10.0f, 1.0f          // m_near
                    , 250.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.0035f, 0.0f, 0.01f, 0.00001f   // m_bias
                    , 0.0012f, 0.0f, 0.05f, 0.00001f   // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 500.0f, 1.0f, 1000.0f, 1.0f      // m_customParam1
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_yNum
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_xOffset
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPackSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPackInstanced
                },
                { //SmImpl::PCF
                    10.0f, 7.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 10.0f, 1.0f, 20.0f, 1.0f         // m_depthValuePow
                    , 1.0f, 1.0f, 99.0f, 1.0f          // m_near
                    , 250.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.007f, 0.0f, 0.01f, 0.00001f    // m_bias
                    , 0.001f, 0.0f, 0.05f, 0.00001f    // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 500.0f, 1.0f, 1000.0f, 1.0f      // m_customParam1
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_yNum
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_xOffset
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPackSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPackInstanced
                },
                { //SmImpl::PCSS
                    10.0f, 7.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 10.0f, 1.0f, 20.0f, 1.0f         // m_depthValuePow
                    , 1.0f, 1.0f, 99.0f, 1.0f          // m_near
                    , 250.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.007f, 0.0f, 0.01f, 0.00001f    // m_bias
                    , 0.001f, 0.0f, 0.05f, 0.00001f    // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 500.0f, 1.0f, 1000.0f, 1.0f      // m_customParam1
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_yNum
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_xOffset
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPackSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPackInstanced
                },
                { //SmImpl::VSM
                    10.0f, 7.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 10.0f, 1.0f, 20.0f, 1.0f         // m_depthValuePow
                    , 8.0f, 1.0f, 10.0f, 1.0f          // m_near
                    , 250.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.045f, 0.0f, 0.1f, 0.00001f     // m_bias
                    , 0.001f, 0.0f, 0.05f, 0.00001f    // m_normalOffset
                    , 0.02f, 0.0f, 0.04f, 0.00001f     // m_customParam0
                    , 450.0f, 1.0f, 1000.0f, 1.0f      // m_customParam1
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_yNum
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_xOffset
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::InvZ][PackDepth::VSM].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::InvZ][PackDepth::VSM].get() //m_progPackSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::InvZ][PackDepth::VSM].get() //m_progPackInstanced
                },
                { //SmImpl::ESM
                    10.0f, 7.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 10.0f, 1.0f, 20.0f, 1.0f         // m_depthValuePow
                    , 3.0f, 1.0f, 10.0f, 0.01f         // m_near
                    , 250.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.02f, 0.0f, 0.3f, 0.00001f      // m_bias
                    , 0.001f, 0.0f, 0.05f, 0.00001f    // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 9000.0f, 1.0f, 15000.0f, 1.0f    // m_customParam1
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_yNum
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_xOffset
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::InvZ][PackDepth::RGBA].get() //m_packDepthSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPackInstanced
                }

            },
            { //DepthImpl::Linear

                { //SmImpl::Hard
                    10.0f, 7.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 1.0f, 1.0f, 20.0f, 1.0f          // m_depthValuePow
                    , 1.0f, 1.0f, 10.0f, 1.0f          // m_near
                    , 250.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.0025f, 0.0f, 0.01f, 0.00001f   // m_bias
                    , 0.0012f, 0.0f, 0.05f, 0.00001f   // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 500.0f, 1.0f, 1000.0f, 1.0f      // m_customParam1
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_yNum
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_xOffset
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::Linear][PackDepth::RGBA].get() //m_packDepthSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPackInstanced
                },
                { //SmImpl::PCF
                    10.0f, 7.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 1.0f, 1.0f, 20.0f, 1.0f          // m_depthValuePow
                    , 1.0f, 1.0f, 99.0f, 1.0f          // m_near
                    , 250.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.0025f, 0.0f, 0.01f, 0.00001f   // m_bias
                    , 0.001f, 0.0f, 0.05f,  0.00001f   // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 2000.0f, 1.0f, 2000.0f, 1.0f     // m_customParam1
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_yNum
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_xOffset
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPackSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPackInstanced
                },
                { //SmImpl::PCSS
                    10.0f, 7.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 1.0f, 1.0f, 20.0f, 1.0f          // m_depthValuePow
                    , 1.0f, 1.0f, 99.0f, 1.0f          // m_near
                    , 250.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.0025f, 0.0f, 0.01f, 0.00001f   // m_bias
                    , 0.001f, 0.0f, 0.05f,  0.00001f   // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 2000.0f, 1.0f, 2000.0f, 1.0f     // m_customParam1
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_yNum
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_xOffset
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPackSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPackInstanced
                },
                { //SmImpl::VSM
                    10.0f, 7.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 1.0f, 1.0f, 20.0f, 1.0f          // m_depthValuePow
                    , 1.0f, 1.0f, 10.0f, 1.0f          // m_near
                    , 250.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.006f, 0.0f, 0.01f, 0.00001f    // m_bias
                    , 0.001f, 0.0f, 0.05f, 0.00001f    // m_normalOffset
                    , 0.02f, 0.0f, 0.1f, 0.00001f      // m_customParam0
                    , 300.0f, 1.0f, 1500.0f, 1.0f      // m_customParam1
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_yNum
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_xOffset
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::Linear][PackDepth::VSM].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::Linear][PackDepth::VSM].get() //m_progPackSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::Linear][PackDepth::VSM].get() //m_progPackInstanced
                },
                { //SmImpl::ESM
                    10.0f, 7.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 1.0f, 1.0f, 20.0f, 1.0f          // m_depthValuePow
                    , 1.0f, 1.0f, 10.0f, 0.01f         // m_near
                    , 250.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.0055f, 0.0f, 0.01f, 0.00001f   // m_bias
                    , 0.001f, 0.0f, 0.05f, 0.00001f    // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 2500.0f, 1.0f, 5000.0f, 1.0f     // m_customParam1
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_yNum
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_xOffset
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::Linear][PackDepth::RGBA].get() //m_packDepthSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPackInstanced
                }

            }

        },
        { //LightType::Point

            { //DepthImpl::InvZ

                { //SmImpl::Hard
                    12.0f, 9.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 10.0f, 1.0f, 20.0f, 1.0f         // m_depthValuePow
                    , 1.0f, 1.0f, 10.0f, 1.0f          // m_near
                    , 250.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.006f, 0.0f, 0.01f, 0.00001f    // m_bias
                    , 0.001f, 0.0f, 0.05f, 0.00001f    // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 50.0f, 1.0f, 300.0f, 1.0f        // m_customParam1
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_yNum
                    , 0.25f, 0.0f, 2.0f, 0.001f        // m_xOffset
                    , 0.25f, 0.0f, 2.0f, 0.001f        // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::InvZ][PackDepth::RGBA].get() //m_packDepthSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPackInstanced
                },
                { //SmImpl::PCF
                    12.0f, 9.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 10.0f, 1.0f, 20.0f, 1.0f         // m_depthValuePow
                    , 1.0f, 1.0f, 99.0f, 1.0f          // m_near
                    , 250.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.004f, 0.0f, 0.01f, 0.00001f    // m_bias
                    , 0.001f, 0.0f, 0.05f, 0.00001f    // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 50.0f, 1.0f, 300.0f, 1.0f        // m_customParam1
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_yNum
                    , 1.0f, 0.0f, 3.0f, 0.001f         // m_xOffset
                    , 1.0f, 0.0f, 3.0f, 0.001f         // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::InvZ][PackDepth::RGBA].get() //m_packDepthSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPackInstanced
                },
                { //SmImpl::PCSS
                    12.0f, 9.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 10.0f, 1.0f, 20.0f, 1.0f         // m_depthValuePow
                    , 1.0f, 1.0f, 99.0f, 1.0f          // m_near
                    , 250.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.004f, 0.0f, 0.01f, 0.00001f    // m_bias
                    , 0.001f, 0.0f, 0.05f, 0.00001f    // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 50.0f, 1.0f, 300.0f, 1.0f        // m_customParam1
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_yNum
                    , 1.0f, 0.0f, 3.0f, 0.001f         // m_xOffset
                    , 1.0f, 0.0f, 3.0f, 0.001f         // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::InvZ][PackDepth::RGBA].get() //m_packDepthSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPackInstanced
                },
                { //SmImpl::VSM
                    12.0f, 9.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 10.0f, 1.0f, 20.0f, 1.0f         // m_depthValuePow
                    , 8.0f, 1.0f, 10.0f, 1.0f          // m_near
                    , 250.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.055f, 0.0f, 0.1f, 0.00001f     // m_bias
                    , 0.001f, 0.0f, 0.05f, 0.00001f    // m_normalOffset
                    , 0.02f, 0.0f, 0.04f, 0.00001f     // m_customParam0
                    , 450.0f, 1.0f, 900.0f, 1.0f       // m_customParam1
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_yNum
                    , 0.25f, 0.0f, 2.0f, 0.001f        // m_xOffset
                    , 0.25f, 0.0f, 2.0f, 0.001f        // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::InvZ][PackDepth::VSM].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::InvZ][PackDepth::VSM].get() //m_packDepthSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::InvZ][PackDepth::VSM].get() //m_progPackInstanced
                },
                { //SmImpl::ESM
                    12.0f, 9.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 10.0f, 1.0f, 20.0f, 1.0f         // m_depthValuePow
                    , 3.0f, 1.0f, 10.0f, 0.01f         // m_near
                    , 250.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.035f, 0.0f, 0.1f, 0.00001f     // m_bias
                    , 0.001f, 0.0f, 0.05f, 0.00001f    // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 9000.0f, 1.0f, 15000.0f, 1.0f    // m_customParam1
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_yNum
                    , 0.25f, 0.0f, 2.0f, 0.001f        // m_xOffset
                    , 0.25f, 0.0f, 2.0f, 0.001f        // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::InvZ][PackDepth::RGBA].get() //m_packDepthSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPackInstanced
                }

            },
            { //DepthImpl::Linear

                { //SmImpl::Hard
                    12.0f, 9.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 1.0f, 1.0f, 20.0f, 1.0f          // m_depthValuePow
                    , 1.0f, 1.0f, 10.0f, 1.0f          // m_near
                    , 250.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.003f, 0.0f, 0.01f, 0.00001f    // m_bias
                    , 0.001f, 0.0f, 0.05f, 0.00001f    // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 120.0f, 1.0f, 300.0f, 1.0f       // m_customParam1
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_yNum
                    , 0.25f, 0.0f, 2.0f, 0.001f        // m_xOffset
                    , 0.25f, 0.0f, 2.0f, 0.001f        // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::Linear][PackDepth::RGBA].get() //m_packDepthSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPackInstanced
                },
                { //SmImpl::PCF
                    12.0f, 9.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 1.0f, 1.0f, 20.0f, 1.0f          // m_depthValuePow
                    , 1.0f, 1.0f, 99.0f, 1.0f          // m_near
                    , 250.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.0035f, 0.0f, 0.01f, 0.00001f   // m_bias
                    , 0.001f, 0.0f, 0.05f, 0.00001f    // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 120.0f, 1.0f, 300.0f, 1.0f       // m_customParam1
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_yNum
                    , 1.0f, 0.0f, 3.0f, 0.001f         // m_xOffset
                    , 1.0f, 0.0f, 3.0f, 0.001f         // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPackSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPackInstanced
                },
                { //SmImpl::PCSS
                    12.0f, 9.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 1.0f, 1.0f, 20.0f, 1.0f          // m_depthValuePow
                    , 1.0f, 1.0f, 99.0f, 1.0f          // m_near
                    , 250.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.0035f, 0.0f, 0.01f, 0.00001f   // m_bias
                    , 0.001f, 0.0f, 0.05f, 0.00001f    // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 120.0f, 1.0f, 300.0f, 1.0f       // m_customParam1
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_yNum
                    , 1.0f, 0.0f, 3.0f, 0.001f         // m_xOffset
                    , 1.0f, 0.0f, 3.0f, 0.001f         // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPackSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPackInstanced
                },
                { //SmImpl::VSM
                    12.0f, 9.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 1.0f, 1.0f, 20.0f, 1.0f          // m_depthValuePow
                    , 1.0f, 1.0f, 10.0f, 1.0f          // m_near
                    , 250.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.006f, 0.0f, 0.1f, 0.00001f     // m_bias
                    , 0.001f, 0.0f, 0.05f, 0.00001f    // m_normalOffset
                    , 0.02f, 0.0f, 0.1f, 0.00001f      // m_customParam0
                    , 400.0f, 1.0f, 900.0f, 1.0f       // m_customParam1
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_yNum
                    , 0.25f, 0.0f, 2.0f, 0.001f        // m_xOffset
                    , 0.25f, 0.0f, 2.0f, 0.001f        // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::Linear][PackDepth::VSM].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::Linear][PackDepth::VSM].get() //m_progPackSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::Linear][PackDepth::VSM].get() //m_progPackInstanced
                },
                { //SmImpl::ESM
                    12.0f, 9.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 1.0f, 1.0f, 20.0f, 1.0f          // m_depthValuePow
                    , 1.0f, 1.0f, 10.0f, 0.01f         // m_near
                    , 250.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.007f, 0.0f, 0.01f, 0.00001f    // m_bias
                    , 0.001f, 0.0f, 0.05f, 0.00001f    // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 8000.0f, 1.0f, 15000.0f, 1.0f    // m_customParam1
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_yNum
                    , 0.25f, 0.0f, 2.0f, 0.001f        // m_xOffset
                    , 0.25f, 0.0f, 2.0f, 0.001f        // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::Linear][PackDepth::RGBA].get() //m_packDepthSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPackInstanced
                }

            }

        },
        { //LightType::Directional

            { //DepthImpl::InvZ

                { //SmImpl::Hard
                    11.0f, 7.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 1.0f, 1.0f, 20.0f, 1.0f          // m_depthValuePow
                    , 1.0f, 1.0f, 10.0f, 1.0f          // m_near
                    , 550.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.0012f, 0.0f, 0.01f, 0.00001f   // m_bias
                    , 0.001f, 0.0f, 0.04f, 0.00001f    // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 200.0f, 1.0f, 400.0f, 1.0f       // m_customParam1
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_yNum
                    , 0.2f, 0.0f, 1.0f, 0.01f          // m_xOffset
                    , 0.2f, 0.0f, 1.0f, 0.01f          // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::InvZ][PackDepth::RGBA].get() //m_packDepthSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPackInstanced
                },
                { //SmImpl::PCF
                    11.0f, 7.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 1.0f, 1.0f, 20.0f, 1.0f          // m_depthValuePow
                    , 1.0f, 1.0f, 99.0f, 1.0f          // m_near
                    , 550.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.0012f, 0.0f, 0.01f, 0.00001f   // m_bias
                    , 0.001f, 0.0f, 0.04f, 0.00001f    // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 200.0f, 1.0f, 400.0f, 1.0f       // m_customParam1
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_yNum
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_xOffset
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::InvZ][PackDepth::RGBA].get() //m_packDepthSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPackInstanced
                },
                { //SmImpl::PCSS
                    11.0f, 7.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 1.0f, 1.0f, 20.0f, 1.0f          // m_depthValuePow
                    , 1.0f, 1.0f, 99.0f, 1.0f          // m_near
                    , 550.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.0012f, 0.0f, 0.01f, 0.00001f   // m_bias
                    , 0.001f, 0.0f, 0.04f, 0.00001f    // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 200.0f, 1.0f, 400.0f, 1.0f       // m_customParam1
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_yNum
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_xOffset
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::InvZ][PackDepth::RGBA].get() //m_packDepthSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPackInstanced
                },
                { //SmImpl::VSM
                    11.0f, 7.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 1.0f, 1.0f, 20.0f, 1.0f          // m_depthValuePow
                    , 1.0f, 1.0f, 10.0f, 1.0f          // m_near
                    , 550.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.004f, 0.0f, 0.01f, 0.00001f    // m_bias
                    , 0.001f, 0.0f, 0.04f, 0.00001f    // m_normalOffset
                    , 0.02f, 0.0f, 0.04f, 0.00001f     // m_customParam0
                    , 2500.0f, 1.0f, 5000.0f, 1.0f     // m_customParam1
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_yNum
                    , 0.2f, 0.0f, 1.0f, 0.01f          // m_xOffset
                    , 0.2f, 0.0f, 1.0f, 0.01f          // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::InvZ][PackDepth::VSM].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::InvZ][PackDepth::VSM].get() //m_packDepthSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::InvZ][PackDepth::VSM].get() //m_progPackInstanced
                },
                { //SmImpl::ESM
                    11.0f, 7.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 1.0f, 1.0f, 20.0f, 1.0f          // m_depthValuePow
                    , 1.0f, 1.0f, 10.0f, 0.01f         // m_near
                    , 550.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.004f, 0.0f, 0.01f, 0.00001f    // m_bias
                    , 0.001f, 0.0f, 0.04f, 0.00001f    // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 9500.0f, 1.0f, 15000.0f, 1.0f    // m_customParam1
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_yNum
                    , 0.2f, 0.0f, 1.0f, 0.01f          // m_xOffset
                    , 0.2f, 0.0f, 1.0f, 0.01f          // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::InvZ][PackDepth::RGBA].get() //m_packDepthSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::InvZ][PackDepth::RGBA].get() //m_progPackInstanced
                }

            },
            { //DepthImpl::Linear

                { //SmImpl::Hard
                    11.0f, 7.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 1.0f, 1.0f, 20.0f, 1.0f          // m_depthValuePow
                    , 1.0f, 1.0f, 10.0f, 1.0f          // m_near
                    , 550.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.0012f, 0.0f, 0.01f, 0.00001f   // m_bias
                    , 0.001f, 0.0f, 0.04f, 0.00001f    // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 500.0f, 1.0f, 1000.0f, 1.0f      // m_customParam1
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_yNum
                    , 0.2f, 0.0f, 1.0f, 0.01f          // m_xOffset
                    , 0.2f, 0.0f, 1.0f, 0.01f          // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::Linear][PackDepth::RGBA].get() //m_packDepthSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPackInstanced
                },
                { //SmImpl::PCF
                    11.0f, 7.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 1.0f, 1.0f, 20.0f, 1.0f          // m_depthValuePow
                    , 1.0f, 1.0f, 99.0f, 1.0f          // m_near
                    , 550.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.0012f, 0.0f, 0.01f, 0.00001f   // m_bias
                    , 0.001f, 0.0f, 0.04f, 0.00001f    // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 200.0f, 1.0f, 400.0f, 1.0f       // m_customParam1
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_yNum
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_xOffset
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPackSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPackInstanced
                },
                { //SmImpl::PCSS
                    11.0f, 7.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 1.0f, 1.0f, 20.0f, 1.0f          // m_depthValuePow
                    , 1.0f, 1.0f, 99.0f, 1.0f          // m_near
                    , 550.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.0012f, 0.0f, 0.01f, 0.00001f   // m_bias
                    , 0.001f, 0.0f, 0.04f, 0.00001f    // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 200.0f, 1.0f, 400.0f, 1.0f       // m_customParam1
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 8.0f, 1.0f           // m_yNum
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_xOffset
                    , 1.0f, 0.0f, 3.0f, 0.01f          // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPackSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPackInstanced
                },
                { //SmImpl::VSM
                    11.0f, 7.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 1.0f, 1.0f, 20.0f, 1.0f          // m_depthValuePow
                    , 1.0f, 1.0f, 10.0f, 1.0f          // m_near
                    , 550.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.004f, 0.0f, 0.01f, 0.00001f    // m_bias
                    , 0.001f, 0.0f, 0.04f, 0.00001f    // m_normalOffset
                    , 0.02f, 0.0f, 0.04f, 0.00001f     // m_customParam0
                    , 2500.0f, 1.0f, 5000.0f, 1.0f     // m_customParam1
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_yNum
                    , 0.2f, 0.0f, 1.0f, 0.01f          // m_xOffset
                    , 0.2f, 0.0f, 1.0f, 0.01f          // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::Linear][PackDepth::VSM].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::Linear][PackDepth::VSM].get() //m_progPackSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::Linear][PackDepth::VSM].get() //m_progPackInstanced
                },
                { //SmImpl::ESM
                    11.0f, 7.0f, 12.0f, 1.0f         // m_sizePwrTwo
                    , 1.0f, 1.0f, 20.0f, 1.0f          // m_depthValuePow
                    , 1.0f, 1.0f, 10.0f, 0.01f         // m_near
                    , 550.0f, 100.0f, 2000.0f, 50.0f   // m_far
                    , 0.004f, 0.0f, 0.01f, 0.00001f    // m_bias
                    , 0.001f, 0.0f, 0.04f, 0.00001f    // m_normalOffset
                    , 0.7f, 0.0f, 1.0f, 0.01f          // m_customParam0
                    , 9500.0f, 1.0f, 15000.0f, 1.0f    // m_customParam1
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_xNum
                    , 2.0f, 0.0f, 4.0f, 1.0f           // m_yNum
                    , 0.2f, 0.0f, 1.0f, 0.01f          // m_xOffset
                    , 0.2f, 0.0f, 1.0f, 0.01f          // m_yOffset
                    , true                             // m_doBlur
                    , programs_.m_packDepth[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPack
                    , programs_.m_packDepthSkinned[DepthImpl::Linear][PackDepth::RGBA].get() //m_packDepthSkinned
                    , programs_.m_packDepthInstanced[DepthImpl::Linear][PackDepth::RGBA].get() //m_progPackInstanced
                }

            }
        }
    };
    // clang-format on
    bx::memCopy(sm_settings_, smSettings, sizeof(smSettings));

    settings_.m_lightType = LightType::SpotLight;
    settings_.m_depthImpl = DepthImpl::InvZ;
    settings_.m_smImpl = SmImpl::Hard;
    settings_.m_spotOuterAngle = 45.0f;
    settings_.m_spotInnerAngle = 30.0f;
    settings_.m_fovXAdjust = 0.0f;
    settings_.m_fovYAdjust = 0.0f;
    settings_.m_coverageSpotL = 90.0f;
    settings_.m_splitDistribution = 0.6f;
    settings_.m_numSplits = 4;
    settings_.m_updateLights = true;
    settings_.m_updateScene = true;
    settings_.m_drawDepthBuffer = false;
    settings_.m_showSmCoverage = false;
    settings_.m_stencilPack = true;
    settings_.m_stabilize = true;
}

auto shadowmap_generator::get_depth_type() const -> PackDepth::Enum
{
    PackDepth::Enum depthType = (SmImpl::VSM == settings_.m_smImpl) ? PackDepth::VSM : PackDepth::RGBA;
    return depthType;
}

auto shadowmap_generator::get_rt_texture(uint8_t split) const -> bgfx::TextureHandle
{
    if(!bgfx::isValid(rt_shadow_map_[split]))
    {
        return {bgfx::kInvalidHandle};
    }

    return bgfx::getTexture(rt_shadow_map_[split]);
}

auto shadowmap_generator::get_depth_render_program(PackDepth::Enum depth) const -> gpu_program::ptr
{
    return programs_.m_drawDepth[depth];
}

void shadowmap_generator::submit_uniforms(uint8_t stage) const
{
    if(!bgfx::isValid(tex_color_))
    {
        return;
    }
    // uniforms_.submitConstUniforms();
    // uniforms_.submitPerFrameUniforms();
    uniforms_.submitPerDrawUniforms();

    for(uint8_t ii = 0; ii < ShadowMapRenderTargets::Count; ++ii)
    {
        if(!bgfx::isValid(rt_shadow_map_[ii]))
        {
            continue;
        }

        bgfx::setTexture(stage + ii, shadow_map_[ii], bgfx::getTexture(rt_shadow_map_[ii]));
    }
}

auto shadowmap_generator::already_updated() const -> bool
{
    return last_update_ == gfx::get_render_frame();
}

void shadowmap_generator::update(const camera& cam, const light& l, const math::transform& ltrans, bool is_active)
{
    last_update_ = gfx::get_render_frame();

    if(!l.casts_shadows)
    {
        deinit_textures();
        return;
    }

    bool recreateTextures = false;
    recreateTextures |= !valid_;

    valid_ = true;

    const auto& pos = ltrans.get_position();
    const auto& dir = ltrans.z_unit_axis();
    point_light_.m_position.m_x = pos.x;
    point_light_.m_position.m_y = pos.y;
    point_light_.m_position.m_z = pos.z;

    point_light_.m_spotDirectionInner.m_x = dir.x;
    point_light_.m_spotDirectionInner.m_y = dir.y;
    point_light_.m_spotDirectionInner.m_z = dir.z;

    directional_light_.m_position.m_x = dir.x;
    directional_light_.m_position.m_y = dir.y;
    directional_light_.m_position.m_z = dir.z;

    auto last_settings = settings_;

    settings_.m_lightType = convert(l.type);
    settings_.m_smImpl = convert(l.shadow_params.type);
    settings_.m_depthImpl = convert(l.shadow_params.depth);

    settings_.m_showSmCoverage = l.shadow_params.show_coverage;

    switch(l.type)
    {
        case light_type::spot:
            settings_.m_spotOuterAngle = l.spot_data.get_outer_angle();
            settings_.m_spotInnerAngle = l.spot_data.get_inner_angle();
            settings_.m_coverageSpotL = settings_.m_spotOuterAngle;
            break;
        case light_type::point:
            settings_.m_stencilPack = l.point_shadow_params.stencil_pack;
            settings_.m_fovXAdjust = l.point_shadow_params.fov_x_adjust;
            settings_.m_fovYAdjust = l.point_shadow_params.fov_y_adjust;

            break;
        default:
            settings_.m_splitDistribution = l.directional_shadow_params.split_distribution;
            settings_.m_numSplits = l.directional_shadow_params.num_splits;
            settings_.m_stabilize = l.directional_shadow_params.stabilize;

            break;
    }

#define SET_CLAMPED_VAL(x, val) x = val // math::clamp(val, x##Min, x##Max)

    ShadowMapSettings* currentSmSettings =
        &sm_settings_[settings_.m_lightType][settings_.m_depthImpl][settings_.m_smImpl];

    SET_CLAMPED_VAL(currentSmSettings->m_sizePwrTwo, convert(l.shadow_params.resolution));
    SET_CLAMPED_VAL(currentSmSettings->m_near, l.shadow_params.near_plane);
    SET_CLAMPED_VAL(currentSmSettings->m_bias, l.shadow_params.bias);
    SET_CLAMPED_VAL(currentSmSettings->m_normalOffset, l.shadow_params.normal_bias);

    switch(l.shadow_params.type)
    {
        case sm_impl::hard:
            currentSmSettings->m_doBlur = false;

            SET_CLAMPED_VAL(currentSmSettings->m_xOffset, 1);
            SET_CLAMPED_VAL(currentSmSettings->m_yOffset, 1);
            break;
        case sm_impl::pcf:
            currentSmSettings->m_doBlur = false;
            SET_CLAMPED_VAL(currentSmSettings->m_xOffset, l.shadow_params.pcf.x_offset);
            SET_CLAMPED_VAL(currentSmSettings->m_yOffset, l.shadow_params.pcf.y_offset);
            break;
        case sm_impl::pcss:
            currentSmSettings->m_doBlur = false;
            SET_CLAMPED_VAL(currentSmSettings->m_xOffset, l.shadow_params.pcss.penumbra_x_offset);
            SET_CLAMPED_VAL(currentSmSettings->m_yOffset, l.shadow_params.pcss.penumbra_y_offset);
            break;
        case sm_impl::vsm:
            currentSmSettings->m_doBlur = l.shadow_params.vsm.do_blur;
            SET_CLAMPED_VAL(currentSmSettings->m_customParam0, l.shadow_params.vsm.min_variance);
            SET_CLAMPED_VAL(currentSmSettings->m_customParam1, l.shadow_params.vsm.depth_multiplier);
            SET_CLAMPED_VAL(currentSmSettings->m_xOffset, l.shadow_params.vsm.blur_x_offset);
            SET_CLAMPED_VAL(currentSmSettings->m_yOffset, l.shadow_params.vsm.blur_y_offset);
            break;
        case sm_impl::esm:
            currentSmSettings->m_doBlur = l.shadow_params.esm.do_blur;
            SET_CLAMPED_VAL(currentSmSettings->m_customParam0, l.shadow_params.esm.hardness);
            SET_CLAMPED_VAL(currentSmSettings->m_customParam1, l.shadow_params.esm.depth_multiplier);
            SET_CLAMPED_VAL(currentSmSettings->m_xOffset, l.shadow_params.esm.blur_x_offset);
            SET_CLAMPED_VAL(currentSmSettings->m_yOffset, l.shadow_params.esm.blur_y_offset);
            break;
        default:
            break;
    }

    switch(l.type)
    {
        case light_type::spot:
            SET_CLAMPED_VAL(currentSmSettings->m_far, l.spot_data.range);
            break;
        case light_type::point:
            SET_CLAMPED_VAL(currentSmSettings->m_far, l.point_data.range);
            break;
        default:
            SET_CLAMPED_VAL(currentSmSettings->m_far, l.shadow_params.far_plane);
            break;
    }

    if(LightType::SpotLight == settings_.m_lightType)
    {
        point_light_.m_spotDirectionInner.m_inner = settings_.m_spotInnerAngle;
    }

    // Update render target size.
    uint16_t shadowMapSize = 1 << uint32_t(currentSmSettings->m_sizePwrTwo);
    recreateTextures |= current_shadow_map_size_ != shadowMapSize;
    recreateTextures |= last_settings.m_smImpl != settings_.m_smImpl;
    recreateTextures |= last_settings.m_numSplits != settings_.m_numSplits;
    recreateTextures |= last_settings.m_lightType != settings_.m_lightType;

    if(recreateTextures)
    {
        current_shadow_map_size_ = shadowMapSize;

        if(bgfx::isValid(rt_shadow_map_[0]))
        {
            bgfx::destroy(rt_shadow_map_[0]);
            rt_shadow_map_[0] = {bgfx::kInvalidHandle};
        }

        {
            bgfx::TextureHandle fbtextures[] = {
                bgfx::createTexture2D(current_shadow_map_size_,
                                      current_shadow_map_size_,
                                      false,
                                      1,
                                      bgfx::TextureFormat::BGRA8,
                                      BGFX_TEXTURE_RT),
                bgfx::createTexture2D(current_shadow_map_size_,
                                      current_shadow_map_size_,
                                      false,
                                      1,
                                      bgfx::TextureFormat::D24S8,
                                      BGFX_TEXTURE_RT),
            };
            rt_shadow_map_[0] = bgfx::createFrameBuffer(BX_COUNTOF(fbtextures), fbtextures, true);
        }

        // if(LightType::DirectionalLight == settings_.m_lightType)
        {
            for(uint8_t ii = 1; ii < ShadowMapRenderTargets::Count; ++ii)
            {
                if(bgfx::isValid(rt_shadow_map_[ii]))
                {
                    bgfx::destroy(rt_shadow_map_[ii]);
                    rt_shadow_map_[ii] = {bgfx::kInvalidHandle};
                }

                if(ii < settings_.m_numSplits)
                {
                    bgfx::TextureHandle fbtextures[] = {
                        bgfx::createTexture2D(current_shadow_map_size_,
                                              current_shadow_map_size_,
                                              false,
                                              1,
                                              bgfx::TextureFormat::BGRA8,
                                              BGFX_TEXTURE_RT),
                        bgfx::createTexture2D(current_shadow_map_size_,
                                              current_shadow_map_size_,
                                              false,
                                              1,
                                              bgfx::TextureFormat::D24S8,
                                              BGFX_TEXTURE_RT),
                    };
                    rt_shadow_map_[ii] = bgfx::createFrameBuffer(BX_COUNTOF(fbtextures), fbtextures, true);
                }
            }
        }

        if(bgfx::isValid(rt_blur_))
        {
            bgfx::destroy(rt_blur_);
            rt_blur_ = {bgfx::kInvalidHandle};
        }

        bool bVsmOrEsm = (SmImpl::VSM == settings_.m_smImpl) || (SmImpl::ESM == settings_.m_smImpl);

        // Blur shadow map.
        if(bVsmOrEsm && currentSmSettings->m_doBlur)
        {
            rt_blur_ = bgfx::createFrameBuffer(current_shadow_map_size_, current_shadow_map_size_, bgfx::TextureFormat::BGRA8);
        }
    }

    float currentShadowMapSizef = float(int16_t(current_shadow_map_size_));

    // Update uniforms.

    uniforms_.m_shadowMapTexelSize = 1.0f / currentShadowMapSizef;
    uniforms_.m_shadowMapBias = currentSmSettings->m_bias;
    uniforms_.m_shadowMapOffset = currentSmSettings->m_normalOffset;
    uniforms_.m_shadowMapParam0 = currentSmSettings->m_customParam0;
    uniforms_.m_shadowMapParam1 = currentSmSettings->m_customParam1;
    uniforms_.m_depthValuePow = currentSmSettings->m_depthValuePow;
    uniforms_.m_XNum = currentSmSettings->m_xNum;
    uniforms_.m_YNum = currentSmSettings->m_yNum;
    uniforms_.m_XOffset = currentSmSettings->m_xOffset;
    uniforms_.m_YOffset = currentSmSettings->m_yOffset;
    uniforms_.m_showSmCoverage = float(settings_.m_showSmCoverage);
    uniforms_.m_numSplits = float(settings_.m_numSplits);
    uniforms_.m_lightPtr = (LightType::DirectionalLight == settings_.m_lightType) ? &directional_light_ : &point_light_;

    ///
    bool homogeneousDepth = gfx::is_homogeneous_depth();
    bool originBottomLeft = gfx::is_origin_bottom_left();

    // Compute transform matrices.
    auto& lightView = light_view_;
    auto& lightProj = light_proj_;
    auto& lightFrustums = light_frustums_;

    float mtxYpr[TetrahedronFaces::Count][16];

    if(LightType::SpotLight == settings_.m_lightType)
    {
        const float fovy = settings_.m_coverageSpotL;
        const float aspect = 1.0f;
        bx::mtxProj(lightProj[ProjType::Horizontal],
                    fovy,
                    aspect,
                    currentSmSettings->m_near,
                    currentSmSettings->m_far,
                    false);

        // For linear depth, prevent depth division by variable w-component in shaders and divide here by far plane
        if(DepthImpl::Linear == settings_.m_depthImpl)
        {
            lightProj[ProjType::Horizontal][10] /= currentSmSettings->m_far;
            lightProj[ProjType::Horizontal][14] /= currentSmSettings->m_far;
        }

        const bx::Vec3 at = bx::add(bx::load<bx::Vec3>(point_light_.m_position.m_v),
                                    bx::load<bx::Vec3>(point_light_.m_spotDirectionInner.m_v));
        bx::mtxLookAt(lightView[TetrahedronFaces::Green], bx::load<bx::Vec3>(point_light_.m_position.m_v), at);
    }
    else if(LightType::PointLight == settings_.m_lightType)
    {
        float ypr[TetrahedronFaces::Count][3] = {
            {bx::toRad(0.0f), bx::toRad(27.36780516f), bx::toRad(0.0f)},
            {bx::toRad(180.0f), bx::toRad(27.36780516f), bx::toRad(0.0f)},
            {bx::toRad(-90.0f), bx::toRad(-27.36780516f), bx::toRad(0.0f)},
            {bx::toRad(90.0f), bx::toRad(-27.36780516f), bx::toRad(0.0f)},
        };

        if(settings_.m_stencilPack)
        {
            const float fovx = 143.98570868f + 3.51f + settings_.m_fovXAdjust;
            const float fovy = 125.26438968f + 9.85f + settings_.m_fovYAdjust;
            const float aspect = bx::tan(bx::toRad(fovx * 0.5f)) / bx::tan(bx::toRad(fovy * 0.5f));

            bx::mtxProj(lightProj[ProjType::Vertical],
                        fovx,
                        aspect,
                        currentSmSettings->m_near,
                        currentSmSettings->m_far,
                        false);

            // For linear depth, prevent depth division by variable w-component in shaders and divide here by far plane
            if(DepthImpl::Linear == settings_.m_depthImpl)
            {
                lightProj[ProjType::Vertical][10] /= currentSmSettings->m_far;
                lightProj[ProjType::Vertical][14] /= currentSmSettings->m_far;
            }

            ypr[TetrahedronFaces::Green][2] = bx::toRad(180.0f);
            ypr[TetrahedronFaces::Yellow][2] = bx::toRad(0.0f);
            ypr[TetrahedronFaces::Blue][2] = bx::toRad(90.0f);
            ypr[TetrahedronFaces::Red][2] = bx::toRad(-90.0f);
        }

        const float fovx = 143.98570868f + 7.8f + settings_.m_fovXAdjust;
        const float fovy = 125.26438968f + 3.0f + settings_.m_fovYAdjust;
        const float aspect = bx::tan(bx::toRad(fovx * 0.5f)) / bx::tan(bx::toRad(fovy * 0.5f));

        bx::mtxProj(lightProj[ProjType::Horizontal],
                    fovy,
                    aspect,
                    currentSmSettings->m_near,
                    currentSmSettings->m_far,
                    homogeneousDepth);

        // For linear depth, prevent depth division by variable w component in shaders and divide here by far plane
        if(DepthImpl::Linear == settings_.m_depthImpl)
        {
            lightProj[ProjType::Horizontal][10] /= currentSmSettings->m_far;
            lightProj[ProjType::Horizontal][14] /= currentSmSettings->m_far;
        }

        for(uint8_t ii = 0; ii < TetrahedronFaces::Count; ++ii)
        {
            float mtxTmp[16];
            mtxYawPitchRoll(mtxTmp, ypr[ii][0], ypr[ii][1], ypr[ii][2]);

            float tmp[3] = {
                -bx::dot(bx::load<bx::Vec3>(point_light_.m_position.m_v), bx::load<bx::Vec3>(&mtxTmp[0])),
                -bx::dot(bx::load<bx::Vec3>(point_light_.m_position.m_v), bx::load<bx::Vec3>(&mtxTmp[4])),
                -bx::dot(bx::load<bx::Vec3>(point_light_.m_position.m_v), bx::load<bx::Vec3>(&mtxTmp[8])),
            };

            bx::mtxTranspose(mtxYpr[ii], mtxTmp);

            bx::memCopy(lightView[ii], mtxYpr[ii], 12 * sizeof(float));
            lightView[ii][12] = tmp[0];
            lightView[ii][13] = tmp[1];
            lightView[ii][14] = tmp[2];
            lightView[ii][15] = 1.0f;
        }
    }
    else // LightType::DirectionalLight == m_settings.m_lightType
    {
        // ============================================================================
        // Cascaded Shadow Map calculation based on GPU Gems 3, Chapter 10
        // ============================================================================

        const uint8_t maxNumSplits = 4;
        BX_ASSERT(maxNumSplits >= settings_.m_numSplits, "Error! Max num splits.");

        // Get camera parameters
        const float nearClip = currentSmSettings->m_near;
        const float farClip = currentSmSettings->m_far;
        const float clipRange = farClip - nearClip;

        // Calculate cascade split depths using GPU Gems 3 logarithmic/uniform blend
        float cascadeSplits[maxNumSplits];
        calculateCascadeSplits(cascadeSplits, 
                                 uint8_t(settings_.m_numSplits), 
                                 nearClip, 
                                 farClip, 
                                 settings_.m_splitDistribution);

        // Light direction (normalized) - this is the direction the light travels
        const bx::Vec3 lightDir = bx::normalize(bx::Vec3{
            directional_light_.m_position.m_x,
            directional_light_.m_position.m_y,
            directional_light_.m_position.m_z
        });

        const bx::Vec3 lightEye = {0.0f, 0.0f, 0.0f};
        const bx::Vec3 lightAt = lightDir;  // Look along light direction
        
        // Use +Y as default up vector, falling back to +Z if light is nearly vertical
        bx::Vec3 upVec = {0.0f, 1.0f, 0.0f};
        if(bx::abs(bx::dot(lightDir, upVec)) > 0.99f)
        {
            upVec = {0.0f, 0.0f, 1.0f};
        }
        
        // All cascades share the same light view matrix (camera position independent)
        bx::mtxLookAt(lightView[0], lightEye, lightAt, upVec);

        // Create base orthographic projection (will be adjusted by crop matrices)
        float mtxProj[16];
        bx::mtxOrtho(mtxProj,
                     -1.0f, 1.0f,    // left, right
                     -1.0f, 1.0f,    // bottom, top
                     -currentSmSettings->m_far,
                     currentSmSettings->m_far,
                     0.0f,
                     homogeneousDepth);

        // Get camera inverse view matrix for frustum corner calculation
        const auto& mtxViewInv = cam.get_view_inverse();

        // Process each cascade
        const uint8_t numCorners = 8;
        float frustumCorners[maxNumSplits][numCorners][3];
        float lastSplitDist = 0.0f;

        for(uint8_t ii = 0; ii < settings_.m_numSplits; ++ii)
        {
            const float splitDist = cascadeSplits[ii];

            const float cascadeNear = nearClip + lastSplitDist * clipRange;
            const float cascadeFar = nearClip + splitDist * clipRange;

            // Update cascade far distance uniform (use original split distance for shader blend)
            uniforms_.m_csmFarDistances[ii] = cascadeFar;

            // Compute frustum corners in view space then transform to world space
            const float camFovy = cam.get_fov();
            const float camAspect = cam.get_aspect_ratio();
            const float projHeight = bx::tan(bx::toRad(camFovy) * 0.5f);
            const float projWidth = projHeight * camAspect;

            // Compute frustum corners for one split in world space.
            worldSpaceFrustumCorners((float*)frustumCorners[ii], cascadeNear, cascadeFar, projWidth, projHeight, mtxViewInv);

            bx::Vec3 min = {9000.0f, 9000.0f, 9000.0f};
            bx::Vec3 max = {-9000.0f, -9000.0f, -9000.0f};
            float frustum_radius = 0.0f;
            
            // Calculate frustum center in world space
            bx::Vec3 frustumCenter = {0.0f, 0.0f, 0.0f};
            for(uint8_t jj = 0; jj < numCorners; ++jj)
            {
                // Transform from view space to world space
                const bx::Vec3 worldCorner = bx::load<bx::Vec3>(&frustumCorners[ii][jj]);
                frustumCenter = bx::add(frustumCenter, worldCorner);
            }
            frustumCenter = bx::mul(frustumCenter, 1.0f / float(numCorners));
					
            
            // Transform center to light space for radius calculation
            const bx::Vec3 lightSpaceCenter = bx::mul(frustumCenter, lightView[0]);
            
            // Transform corners to light space and compute bounds
            for(uint8_t jj = 0; jj < numCorners; ++jj)
            {
                const bx::Vec3 xyz = bx::mul(bx::load<bx::Vec3>(frustumCorners[ii][jj]), lightView[0]);
                
                // Calculate distance from center for radius
                const float dx = xyz.x - lightSpaceCenter.x;
                const float dy = xyz.y - lightSpaceCenter.y;
                const float dz = xyz.z - lightSpaceCenter.z;
                const float distance = bx::sqrt(dx*dx + dy*dy + dz*dz);
                frustum_radius = bx::max(frustum_radius, distance);
                
                // Update bounding box in light space
                min = bx::min(min, xyz);
                max = bx::max(max, xyz);
            }
            
            // Round radius to reduce flickering
            frustum_radius = bx::ceil(frustum_radius * 16.0f) / 16.0f;

            // Project bounds to the base ortho projection space
            const bx::Vec3 minproj = bx::mulH(min, mtxProj);
            const bx::Vec3 maxproj = bx::mulH(max, mtxProj);

            // Calculate scale using radius-based approach for stability
            float scalex = 1.0f / frustum_radius;
            float scaley = 1.0f / frustum_radius;

            if(settings_.m_stabilize)
            {
                // Quantize scale for stability
                const float quantizer = 64.0f;
                scalex = quantizer / bx::ceil(quantizer / scalex);
                scaley = quantizer / bx::ceil(quantizer / scaley);
            }

            // Calculate offset to center the cascade in the projection
            float offsetx = -scalex * (minproj.x + maxproj.x) * 0.5f;
            float offsety = -scaley * (minproj.y + maxproj.y) * 0.5f;

            // Apply texel snapping for stability
            if(settings_.m_stabilize)
            {
                float currentShadowMapSizef = float(int16_t(current_shadow_map_size_));
                const float halfSize = currentShadowMapSizef * 0.5f;
                
                // Snap to texel grid
                offsetx = bx::round(offsetx * halfSize) / halfSize;
                offsety = bx::round(offsety * halfSize) / halfSize;
            }

            // Build crop matrix to adjust the base projection for this cascade
            float mtxCrop[16];
            bx::mtxIdentity(mtxCrop);
            mtxCrop[0] = scalex;   // x-scale
            mtxCrop[5] = scaley;   // y-scale
            mtxCrop[12] = offsetx; // x-offset
            mtxCrop[13] = offsety; // y-offset
            mtxCrop[14] = -lightSpaceCenter.z; // z-offset: center depth range on cascade frustum

            // Final projection = crop * base projection
            bx::mtxMul(lightProj[ii], mtxCrop, mtxProj);

            lastSplitDist = splitDist;
        }
    }

    if(LightType::SpotLight == settings_.m_lightType)
    {
        lightFrustums[0].update(math::make_mat4(lightView[0]),
                                math::make_mat4(lightProj[ProjType::Horizontal]),
                                homogeneousDepth);
    }
    else if(LightType::PointLight == settings_.m_lightType)
    {
        lightFrustums[TetrahedronFaces::Green].update(math::make_mat4(lightView[TetrahedronFaces::Green]),
                                                      math::make_mat4(lightProj[ProjType::Horizontal]),
                                                      homogeneousDepth);

        lightFrustums[TetrahedronFaces::Yellow].update(math::make_mat4(lightView[TetrahedronFaces::Yellow]),
                                                       math::make_mat4(lightProj[ProjType::Horizontal]),
                                                       homogeneousDepth);

        if(settings_.m_stencilPack)
        {
            lightFrustums[TetrahedronFaces::Blue].update(math::make_mat4(lightView[TetrahedronFaces::Blue]),
                                                         math::make_mat4(lightProj[ProjType::Vertical]),
                                                         homogeneousDepth);

            lightFrustums[TetrahedronFaces::Red].update(math::make_mat4(lightView[TetrahedronFaces::Red]),
                                                        math::make_mat4(lightProj[ProjType::Vertical]),
                                                        homogeneousDepth);
        }
        else
        {
            lightFrustums[TetrahedronFaces::Blue].update(math::make_mat4(lightView[TetrahedronFaces::Blue]),
                                                         math::make_mat4(lightProj[ProjType::Horizontal]),
                                                         homogeneousDepth);

            lightFrustums[TetrahedronFaces::Red].update(math::make_mat4(lightView[TetrahedronFaces::Red]),
                                                        math::make_mat4(lightProj[ProjType::Horizontal]),
                                                        homogeneousDepth);
        }
    }
    else // LightType::DirectionalLight == settings.m_lightType
    {
        lightFrustums[0].update(math::make_mat4(lightView[0]), math::make_mat4(lightProj[0]), homogeneousDepth);
        lightFrustums[1].update(math::make_mat4(lightView[0]), math::make_mat4(lightProj[1]), homogeneousDepth);
        lightFrustums[2].update(math::make_mat4(lightView[0]), math::make_mat4(lightProj[2]), homogeneousDepth);
        lightFrustums[3].update(math::make_mat4(lightView[0]), math::make_mat4(lightProj[3]), homogeneousDepth);
    }

    // Prepare for scene.
    {
        // Setup shadow mtx.
        float mtxShadow[16];

        const float ymul = (originBottomLeft) ? 0.5f : -0.5f;
        float zadd = (DepthImpl::Linear == settings_.m_depthImpl) ? 0.0f : 0.5f;

        // clang-format off
        const float mtxBias[16] =
            {
                0.5f, 0.0f, 0.0f, 0.0f,
                0.0f, ymul, 0.0f, 0.0f,
                0.0f, 0.0f, 0.5f, 0.0f,
                0.5f, 0.5f, zadd, 1.0f,
            };
        // clang-format on

        if(LightType::SpotLight == settings_.m_lightType)
        {
            float mtxTmp[16];
            bx::mtxMul(mtxTmp, lightProj[ProjType::Horizontal], mtxBias);
            bx::mtxMul(mtxShadow, lightView[0], mtxTmp); // lightViewProjBias
        }
        else if(LightType::PointLight == settings_.m_lightType)
        {
            const float s = (originBottomLeft) ? 1.0f : -1.0f; // sign
            zadd = (DepthImpl::Linear == settings_.m_depthImpl) ? 0.0f : 0.5f;

            // clang-format off
            const float mtxCropBias[2][TetrahedronFaces::Count][16] =
                {
                    { // settings.m_stencilPack == false

                     { // D3D: Green, OGL: Blue
                      0.25f,    0.0f, 0.0f, 0.0f,
                      0.0f, s*0.25f, 0.0f, 0.0f,
                      0.0f,    0.0f, 0.5f, 0.0f,
                      0.25f,   0.25f, zadd, 1.0f,
                      },
                     { // D3D: Yellow, OGL: Red
                      0.25f,    0.0f, 0.0f, 0.0f,
                      0.0f, s*0.25f, 0.0f, 0.0f,
                      0.0f,    0.0f, 0.5f, 0.0f,
                      0.75f,   0.25f, zadd, 1.0f,
                      },
                     { // D3D: Blue, OGL: Green
                      0.25f,    0.0f, 0.0f, 0.0f,
                      0.0f, s*0.25f, 0.0f, 0.0f,
                      0.0f,    0.0f, 0.5f, 0.0f,
                      0.25f,   0.75f, zadd, 1.0f,
                      },
                     { // D3D: Red, OGL: Yellow
                         0.25f,    0.0f, 0.0f, 0.0f,
                         0.0f, s*0.25f, 0.0f, 0.0f,
                         0.0f,    0.0f, 0.5f, 0.0f,
                         0.75f,   0.75f, zadd, 1.0f,
                         },
                     },
                    { // settings.m_stencilPack == true

                     { // D3D: Red, OGL: Blue
                      0.25f,   0.0f, 0.0f, 0.0f,
                      0.0f, s*0.5f, 0.0f, 0.0f,
                      0.0f,   0.0f, 0.5f, 0.0f,
                      0.25f,   0.5f, zadd, 1.0f,
                      },
                     { // D3D: Blue, OGL: Red
                      0.25f,   0.0f, 0.0f, 0.0f,
                      0.0f, s*0.5f, 0.0f, 0.0f,
                      0.0f,   0.0f, 0.5f, 0.0f,
                      0.75f,   0.5f, zadd, 1.0f,
                      },
                     { // D3D: Green, OGL: Green
                      0.5f,    0.0f, 0.0f, 0.0f,
                      0.0f, s*0.25f, 0.0f, 0.0f,
                      0.0f,    0.0f, 0.5f, 0.0f,
                      0.5f,   0.75f, zadd, 1.0f,
                      },
                     { // D3D: Yellow, OGL: Yellow
                         0.5f,    0.0f, 0.0f, 0.0f,
                         0.0f, s*0.25f, 0.0f, 0.0f,
                         0.0f,    0.0f, 0.5f, 0.0f,
                         0.5f,   0.25f, zadd, 1.0f,
                         },
                     }
                };
            // clang-format on

            // clang-format off
                   // Use as: [stencilPack][flipV][tetrahedronFace]
            static const uint8_t cropBiasIndices[2][2][4] =
                {
                 { // settings.m_stencilPack == false
                     { 0, 1, 2, 3 }, //flipV == false
                     { 2, 3, 0, 1 }, //flipV == true
                 },
                 { // settings.m_stencilPack == true
                     { 3, 2, 0, 1 }, //flipV == false
                     { 2, 3, 0, 1 }, //flipV == true
                 },
                 };
            // clang-format on

            for(uint8_t ii = 0; ii < TetrahedronFaces::Count; ++ii)
            {
                ProjType::Enum projType = (settings_.m_stencilPack) ? ProjType::Enum(ii > 1) : ProjType::Horizontal;
                uint8_t biasIndex = cropBiasIndices[settings_.m_stencilPack][uint8_t(originBottomLeft)][ii];

                float mtxTmp[16];
                bx::mtxMul(mtxTmp, mtxYpr[ii], lightProj[projType]);
                bx::mtxMul(shadow_map_mtx_[ii],
                           mtxTmp,
                           mtxCropBias[settings_.m_stencilPack][biasIndex]); // mtxYprProjBias
            }

            bx::mtxTranslate(mtxShadow // lightInvTranslate
                             ,
                             -point_light_.m_position.m_v[0],
                             -point_light_.m_position.m_v[1],
                             -point_light_.m_position.m_v[2]);
        }
        else // LightType::DirectionalLight == settings.m_lightType
        {
            // Use per-cascade light view matrices for improved cascade stability
            for(uint8_t ii = 0; ii < settings_.m_numSplits; ++ii)
            {
                float mtxTmp[16];

                bx::mtxMul(mtxTmp, lightProj[ii], mtxBias);
                bx::mtxMul(shadow_map_mtx_[ii], lightView[0], mtxTmp); // lViewProjCropBias
            }
        }

        if(LightType::DirectionalLight != settings_.m_lightType)
        {
            float tmp[16];
            bx::mtxIdentity(tmp);

            bx::mtxMul(light_mtx_, tmp, mtxShadow);
        }
    }
}

void shadowmap_generator::generate_shadowmaps(const shadow_map_models_t& models, const camera& cam, ::unravel::rendering::pipeline_stats* stats)
{
    auto& lightView = light_view_;
    auto& lightProj = light_proj_;
    auto& lightFrustums = light_frustums_;

    bool homogeneousDepth = gfx::is_homogeneous_depth();
    bool originBottomLeft = gfx::is_origin_bottom_left();

    float screenProj[16];
    float screenView[16];
    bx::mtxIdentity(screenView);

    bx::mtxOrtho(screenProj, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 100.0f, 0.0f, gfx::get_caps()->homogeneousDepth);

    
    /// begin generating
    gfx::render_pass shadowmap_pass_0("ShadowmapPass 0");
    gfx::render_pass shadowmap_pass_1("Shadowmap Pass 1");
    gfx::render_pass shadowmap_pass_2("Shadowmap Pass 2");
    gfx::render_pass shadowmap_pass_3("Shadowmap Pass 3");
    gfx::render_pass shadowmap_pass_4("Shadowmap Pass 4");
    gfx::render_pass shadowmap_vblur_pass_0("Shadowmap VBlur Pass 0");
    gfx::render_pass shadowmap_hblur_pass_0("Shadowmap HBlur Pass 0");
    gfx::render_pass shadowmap_vblur_pass_1("Shadowmap VBlur Pass 1");
    gfx::render_pass shadowmap_hblur_pass_1("Shadowmap HBlur Pass 1");
    gfx::render_pass shadowmap_vblur_pass_2("Shadowmap VBlur Pass 2");
    gfx::render_pass shadowmap_hblur_pass_2("Shadowmap HBlur Pass 2");
    gfx::render_pass shadowmap_vblur_pass_3("Shadowmap VBlur Pass 3");
    gfx::render_pass shadowmap_hblur_pass_3("Shadowmap HBlur Pass 3");

    auto RENDERVIEW_SHADOWMAP_0_ID = shadowmap_pass_0.id;
    auto RENDERVIEW_SHADOWMAP_1_ID = shadowmap_pass_1.id;
    auto RENDERVIEW_SHADOWMAP_2_ID = shadowmap_pass_2.id;
    auto RENDERVIEW_SHADOWMAP_3_ID = shadowmap_pass_3.id;
    auto RENDERVIEW_SHADOWMAP_4_ID = shadowmap_pass_4.id;
    auto RENDERVIEW_VBLUR_0_ID = shadowmap_vblur_pass_0.id;
    auto RENDERVIEW_HBLUR_0_ID = shadowmap_hblur_pass_0.id;
    auto RENDERVIEW_VBLUR_1_ID = shadowmap_vblur_pass_1.id;
    auto RENDERVIEW_HBLUR_1_ID = shadowmap_hblur_pass_1.id;
    auto RENDERVIEW_VBLUR_2_ID = shadowmap_vblur_pass_2.id;
    auto RENDERVIEW_HBLUR_2_ID = shadowmap_hblur_pass_2.id;
    auto RENDERVIEW_VBLUR_3_ID = shadowmap_vblur_pass_3.id;
    auto RENDERVIEW_HBLUR_3_ID = shadowmap_hblur_pass_3.id;

    if(LightType::SpotLight == settings_.m_lightType)
    {
        /**
         * RENDERVIEW_SHADOWMAP_0_ID - Clear shadow map. (used as convenience, otherwise render_pass_1 could be cleared)
         * RENDERVIEW_SHADOWMAP_1_ID - Craft shadow map.
         * RENDERVIEW_VBLUR_0_ID - Vertical blur.
         * RENDERVIEW_HBLUR_0_ID - Horizontal blur.
         */

        bgfx::setViewRect(RENDERVIEW_SHADOWMAP_0_ID, 0, 0, current_shadow_map_size_, current_shadow_map_size_);
        bgfx::setViewRect(RENDERVIEW_SHADOWMAP_1_ID, 0, 0, current_shadow_map_size_, current_shadow_map_size_);
        bgfx::setViewRect(RENDERVIEW_VBLUR_0_ID, 0, 0, current_shadow_map_size_, current_shadow_map_size_);
        bgfx::setViewRect(RENDERVIEW_HBLUR_0_ID, 0, 0, current_shadow_map_size_, current_shadow_map_size_);

        bgfx::setViewTransform(RENDERVIEW_SHADOWMAP_0_ID, screenView, screenProj);
        bgfx::setViewTransform(RENDERVIEW_SHADOWMAP_1_ID, lightView[0], lightProj[ProjType::Horizontal]);
        bgfx::setViewTransform(RENDERVIEW_VBLUR_0_ID, screenView, screenProj);
        bgfx::setViewTransform(RENDERVIEW_HBLUR_0_ID, screenView, screenProj);

        bgfx::setViewFrameBuffer(RENDERVIEW_SHADOWMAP_0_ID, rt_shadow_map_[0]);
        bgfx::setViewFrameBuffer(RENDERVIEW_SHADOWMAP_1_ID, rt_shadow_map_[0]);
        bgfx::setViewFrameBuffer(RENDERVIEW_VBLUR_0_ID, rt_blur_);
        bgfx::setViewFrameBuffer(RENDERVIEW_HBLUR_0_ID, rt_shadow_map_[0]);
    }
    else if(LightType::PointLight == settings_.m_lightType)
    {
        /**
         * RENDERVIEW_SHADOWMAP_0_ID - Clear entire shadow map.
         * RENDERVIEW_SHADOWMAP_1_ID - Craft green tetrahedron shadow face.
         * RENDERVIEW_SHADOWMAP_2_ID - Craft yellow tetrahedron shadow face.
         * RENDERVIEW_SHADOWMAP_3_ID - Craft blue tetrahedron shadow face.
         * RENDERVIEW_SHADOWMAP_4_ID - Craft red tetrahedron shadow face.
         * RENDERVIEW_VBLUR_0_ID - Vertical blur.
         * RENDERVIEW_HBLUR_0_ID - Horizontal blur.
         */

        bgfx::setViewRect(RENDERVIEW_SHADOWMAP_0_ID, 0, 0, current_shadow_map_size_, current_shadow_map_size_);
        if(settings_.m_stencilPack)
        {
            const uint16_t f = current_shadow_map_size_;     // full size
            const uint16_t h = current_shadow_map_size_ / 2; // half size
            bgfx::setViewRect(RENDERVIEW_SHADOWMAP_1_ID, 0, 0, f, h);
            bgfx::setViewRect(RENDERVIEW_SHADOWMAP_2_ID, 0, h, f, h);
            bgfx::setViewRect(RENDERVIEW_SHADOWMAP_3_ID, 0, 0, h, f);
            bgfx::setViewRect(RENDERVIEW_SHADOWMAP_4_ID, h, 0, h, f);
        }
        else
        {
            const uint16_t h = current_shadow_map_size_ / 2; // half size
            bgfx::setViewRect(RENDERVIEW_SHADOWMAP_1_ID, 0, 0, h, h);
            bgfx::setViewRect(RENDERVIEW_SHADOWMAP_2_ID, h, 0, h, h);
            bgfx::setViewRect(RENDERVIEW_SHADOWMAP_3_ID, 0, h, h, h);
            bgfx::setViewRect(RENDERVIEW_SHADOWMAP_4_ID, h, h, h, h);
        }
        bgfx::setViewRect(RENDERVIEW_VBLUR_0_ID, 0, 0, current_shadow_map_size_, current_shadow_map_size_);
        bgfx::setViewRect(RENDERVIEW_HBLUR_0_ID, 0, 0, current_shadow_map_size_, current_shadow_map_size_);

        bgfx::setViewTransform(RENDERVIEW_SHADOWMAP_0_ID, screenView, screenProj);
        bgfx::setViewTransform(RENDERVIEW_SHADOWMAP_1_ID,
                               lightView[TetrahedronFaces::Green],
                               lightProj[ProjType::Horizontal]);

        bgfx::setViewTransform(RENDERVIEW_SHADOWMAP_2_ID,
                               lightView[TetrahedronFaces::Yellow],
                               lightProj[ProjType::Horizontal]);

        if(settings_.m_stencilPack)
        {
            bgfx::setViewTransform(RENDERVIEW_SHADOWMAP_3_ID,
                                   lightView[TetrahedronFaces::Blue],
                                   lightProj[ProjType::Vertical]);

            bgfx::setViewTransform(RENDERVIEW_SHADOWMAP_4_ID,
                                   lightView[TetrahedronFaces::Red],
                                   lightProj[ProjType::Vertical]);
        }
        else
        {
            bgfx::setViewTransform(RENDERVIEW_SHADOWMAP_3_ID,
                                   lightView[TetrahedronFaces::Blue],
                                   lightProj[ProjType::Horizontal]);

            bgfx::setViewTransform(RENDERVIEW_SHADOWMAP_4_ID,
                                   lightView[TetrahedronFaces::Red],
                                   lightProj[ProjType::Horizontal]);
        }
        bgfx::setViewTransform(RENDERVIEW_VBLUR_0_ID, screenView, screenProj);
        bgfx::setViewTransform(RENDERVIEW_HBLUR_0_ID, screenView, screenProj);

        bgfx::setViewFrameBuffer(RENDERVIEW_SHADOWMAP_0_ID, rt_shadow_map_[0]);
        bgfx::setViewFrameBuffer(RENDERVIEW_SHADOWMAP_1_ID, rt_shadow_map_[0]);
        bgfx::setViewFrameBuffer(RENDERVIEW_SHADOWMAP_2_ID, rt_shadow_map_[0]);
        bgfx::setViewFrameBuffer(RENDERVIEW_SHADOWMAP_3_ID, rt_shadow_map_[0]);
        bgfx::setViewFrameBuffer(RENDERVIEW_SHADOWMAP_4_ID, rt_shadow_map_[0]);
        bgfx::setViewFrameBuffer(RENDERVIEW_VBLUR_0_ID, rt_blur_);
        bgfx::setViewFrameBuffer(RENDERVIEW_HBLUR_0_ID, rt_shadow_map_[0]);
    }
    else // LightType::DirectionalLight == settings.m_lightType
    {
        /**
         * RENDERVIEW_SHADOWMAP_1_ID - Craft shadow map for first  split.
         * RENDERVIEW_SHADOWMAP_2_ID - Craft shadow map for second split.
         * RENDERVIEW_SHADOWMAP_3_ID - Craft shadow map for third  split.
         * RENDERVIEW_SHADOWMAP_4_ID - Craft shadow map for fourth split.
         * RENDERVIEW_VBLUR_0_ID - Vertical   blur for first  split.
         * RENDERVIEW_HBLUR_0_ID - Horizontal blur for first  split.
         * RENDERVIEW_VBLUR_1_ID - Vertical   blur for second split.
         * RENDERVIEW_HBLUR_1_ID - Horizontal blur for second split.
         * RENDERVIEW_VBLUR_2_ID - Vertical   blur for third  split.
         * RENDERVIEW_HBLUR_2_ID - Horizontal blur for third  split.
         * RENDERVIEW_VBLUR_3_ID - Vertical   blur for fourth split.
         * RENDERVIEW_HBLUR_3_ID - Horizontal blur for fourth split.
         */

        bgfx::setViewRect(RENDERVIEW_SHADOWMAP_1_ID, 0, 0, current_shadow_map_size_, current_shadow_map_size_);
        bgfx::setViewRect(RENDERVIEW_SHADOWMAP_2_ID, 0, 0, current_shadow_map_size_, current_shadow_map_size_);
        bgfx::setViewRect(RENDERVIEW_SHADOWMAP_3_ID, 0, 0, current_shadow_map_size_, current_shadow_map_size_);
        bgfx::setViewRect(RENDERVIEW_SHADOWMAP_4_ID, 0, 0, current_shadow_map_size_, current_shadow_map_size_);
        bgfx::setViewRect(RENDERVIEW_VBLUR_0_ID, 0, 0, current_shadow_map_size_, current_shadow_map_size_);
        bgfx::setViewRect(RENDERVIEW_HBLUR_0_ID, 0, 0, current_shadow_map_size_, current_shadow_map_size_);
        bgfx::setViewRect(RENDERVIEW_VBLUR_1_ID, 0, 0, current_shadow_map_size_, current_shadow_map_size_);
        bgfx::setViewRect(RENDERVIEW_HBLUR_1_ID, 0, 0, current_shadow_map_size_, current_shadow_map_size_);
        bgfx::setViewRect(RENDERVIEW_VBLUR_2_ID, 0, 0, current_shadow_map_size_, current_shadow_map_size_);
        bgfx::setViewRect(RENDERVIEW_HBLUR_2_ID, 0, 0, current_shadow_map_size_, current_shadow_map_size_);
        bgfx::setViewRect(RENDERVIEW_VBLUR_3_ID, 0, 0, current_shadow_map_size_, current_shadow_map_size_);
        bgfx::setViewRect(RENDERVIEW_HBLUR_3_ID, 0, 0, current_shadow_map_size_, current_shadow_map_size_);

        // Each cascade now has its own light view matrix for improved stability
        bgfx::setViewTransform(RENDERVIEW_SHADOWMAP_1_ID, lightView[0], lightProj[0]);
        bgfx::setViewTransform(RENDERVIEW_SHADOWMAP_2_ID, lightView[0], lightProj[1]);
        bgfx::setViewTransform(RENDERVIEW_SHADOWMAP_3_ID, lightView[0], lightProj[2]);
        bgfx::setViewTransform(RENDERVIEW_SHADOWMAP_4_ID, lightView[0], lightProj[3]);

        bgfx::setViewTransform(RENDERVIEW_VBLUR_0_ID, screenView, screenProj);
        bgfx::setViewTransform(RENDERVIEW_HBLUR_0_ID, screenView, screenProj);
        bgfx::setViewTransform(RENDERVIEW_VBLUR_1_ID, screenView, screenProj);
        bgfx::setViewTransform(RENDERVIEW_HBLUR_1_ID, screenView, screenProj);
        bgfx::setViewTransform(RENDERVIEW_VBLUR_2_ID, screenView, screenProj);
        bgfx::setViewTransform(RENDERVIEW_HBLUR_2_ID, screenView, screenProj);
        bgfx::setViewTransform(RENDERVIEW_VBLUR_3_ID, screenView, screenProj);
        bgfx::setViewTransform(RENDERVIEW_HBLUR_3_ID, screenView, screenProj);

        bgfx::setViewFrameBuffer(RENDERVIEW_SHADOWMAP_1_ID, rt_shadow_map_[0]);
        bgfx::setViewFrameBuffer(RENDERVIEW_SHADOWMAP_2_ID, rt_shadow_map_[1]);
        bgfx::setViewFrameBuffer(RENDERVIEW_SHADOWMAP_3_ID, rt_shadow_map_[2]);
        bgfx::setViewFrameBuffer(RENDERVIEW_SHADOWMAP_4_ID, rt_shadow_map_[3]);
        bgfx::setViewFrameBuffer(RENDERVIEW_VBLUR_0_ID, rt_blur_);          // vblur
        bgfx::setViewFrameBuffer(RENDERVIEW_HBLUR_0_ID, rt_shadow_map_[0]); // hblur
        bgfx::setViewFrameBuffer(RENDERVIEW_VBLUR_1_ID, rt_blur_);          // vblur
        bgfx::setViewFrameBuffer(RENDERVIEW_HBLUR_1_ID, rt_shadow_map_[1]); // hblur
        bgfx::setViewFrameBuffer(RENDERVIEW_VBLUR_2_ID, rt_blur_);          // vblur
        bgfx::setViewFrameBuffer(RENDERVIEW_HBLUR_2_ID, rt_shadow_map_[2]); // hblur
        bgfx::setViewFrameBuffer(RENDERVIEW_VBLUR_3_ID, rt_blur_);          // vblur
        bgfx::setViewFrameBuffer(RENDERVIEW_HBLUR_3_ID, rt_shadow_map_[3]); // hblur
    }

    // Clear shadowmap rendertarget at beginning.
    const uint8_t flags0 = (LightType::DirectionalLight == settings_.m_lightType)
                               ? 0
                               : BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL;

    bgfx::setViewClear(RENDERVIEW_SHADOWMAP_0_ID,
                       flags0,
                       0xfefefefe // blur fails on completely white regions
                       ,
                       clear_values_.clear_depth,
                       clear_values_.clear_stencil);
    bgfx::touch(RENDERVIEW_SHADOWMAP_0_ID);

    const uint8_t flags1 =
        (LightType::DirectionalLight == settings_.m_lightType) ? BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH : 0;

    for(uint8_t ii = 0; ii < 4; ++ii)
    {
        bgfx::setViewClear(RENDERVIEW_SHADOWMAP_1_ID + ii,
                           flags1,
                           0xfefefefe // blur fails on completely white regions
                           ,
                           clear_values_.clear_depth,
                           clear_values_.clear_stencil);
        bgfx::touch(RENDERVIEW_SHADOWMAP_1_ID + ii);
    }

    // Render.

    ShadowMapSettings* currentSmSettings =
        &sm_settings_[settings_.m_lightType][settings_.m_depthImpl][settings_.m_smImpl];

    // uniforms_.submitConstUniforms();
    // uniforms_.submitPerFrameUniforms();

    bool anythingDrawn = false;
    // Craft shadow map.
    {
        // Craft stencil mask for point light shadow map packing.
        if(LightType::PointLight == settings_.m_lightType && settings_.m_stencilPack)
        {
            if(6 == bgfx::getAvailTransientVertexBuffer(6, PosVertex::get_layout()))
            {
                bgfx::TransientVertexBuffer vb;
                bgfx::allocTransientVertexBuffer(&vb, 6, PosVertex::get_layout());
                PosVertex* vertex = (PosVertex*)vb.data;

                const float min = 0.0f;
                const float max = 1.0f;
                const float center = 0.5f;
                const float zz = 0.0f;

                vertex[0].m_x = min;
                vertex[0].m_y = min;
                vertex[0].m_z = zz;

                vertex[1].m_x = max;
                vertex[1].m_y = min;
                vertex[1].m_z = zz;

                vertex[2].m_x = center;
                vertex[2].m_y = center;
                vertex[2].m_z = zz;

                vertex[3].m_x = center;
                vertex[3].m_y = center;
                vertex[3].m_z = zz;

                vertex[4].m_x = max;
                vertex[4].m_y = max;
                vertex[4].m_z = zz;

                vertex[5].m_x = min;
                vertex[5].m_y = max;
                vertex[5].m_z = zz;

                bgfx::setState(0);
                bgfx::setStencil(BGFX_STENCIL_TEST_ALWAYS | BGFX_STENCIL_FUNC_REF(1) | BGFX_STENCIL_FUNC_RMASK(0xff) |
                                 BGFX_STENCIL_OP_FAIL_S_REPLACE | BGFX_STENCIL_OP_FAIL_Z_REPLACE |
                                 BGFX_STENCIL_OP_PASS_Z_REPLACE);
                bgfx::setVertexBuffer(0, &vb);

                programs_.m_black->begin();
                bgfx::submit(RENDERVIEW_SHADOWMAP_0_ID, programs_.m_black->native_handle());
                programs_.m_black->end();
            }
        }

        anythingDrawn =
            render_scene_into_shadowmap(RENDERVIEW_SHADOWMAP_1_ID, models, lightFrustums, currentSmSettings, &cam, stats);
    }

    if(anythingDrawn)
    {
        PackDepth::Enum depthType = (SmImpl::VSM == settings_.m_smImpl) ? PackDepth::VSM : PackDepth::RGBA;
        bool bVsmOrEsm = (SmImpl::VSM == settings_.m_smImpl) || (SmImpl::ESM == settings_.m_smImpl);

        // Blur shadow map.
        if(bVsmOrEsm && currentSmSettings->m_doBlur)
        {
            bgfx::setTexture(4, shadow_map_[0], bgfx::getTexture(rt_shadow_map_[0]));
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
            screenSpaceQuad(originBottomLeft);
            programs_.m_vBlur[depthType]->begin();
            bgfx::submit(RENDERVIEW_VBLUR_0_ID, programs_.m_vBlur[depthType]->native_handle());
            programs_.m_vBlur[depthType]->end();

            bgfx::setTexture(4, shadow_map_[0], bgfx::getTexture(rt_blur_));
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
            screenSpaceQuad(originBottomLeft);
            programs_.m_hBlur[depthType]->begin();
            bgfx::submit(RENDERVIEW_HBLUR_0_ID, programs_.m_hBlur[depthType]->native_handle());
            programs_.m_hBlur[depthType]->end();

            if(LightType::DirectionalLight == settings_.m_lightType)
            {
                for(uint8_t ii = 1, jj = 2; ii < settings_.m_numSplits; ++ii, jj += 2)
                {
                    const uint8_t viewId = RENDERVIEW_VBLUR_0_ID + jj;

                    bgfx::setTexture(4, shadow_map_[0], bgfx::getTexture(rt_shadow_map_[ii]));
                    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
                    screenSpaceQuad(originBottomLeft);
                    bgfx::submit(viewId, programs_.m_vBlur[depthType]->native_handle());

                    bgfx::setTexture(4, shadow_map_[0], bgfx::getTexture(rt_blur_));
                    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
                    screenSpaceQuad(originBottomLeft);
                    bgfx::submit(viewId + 1, programs_.m_hBlur[depthType]->native_handle());
                }
            }
        }
    }
}

auto shadowmap_generator::render_scene_into_shadowmap(uint8_t shadowmap_1_id,
                                                      const shadow_map_models_t& models,
                                                      const math::frustum lightFrustums[ShadowMapRenderTargets::Count],
                                                      ShadowMapSettings* currentSmSettings,
                                                      const camera* cam,
                                                      ::unravel::rendering::pipeline_stats* stats) -> bool
{
    bool any_rendered = false;
    // Draw scene into shadowmap.
    uint8_t drawNum;
    if(LightType::SpotLight == settings_.m_lightType)
    {
        drawNum = 1;
    }
    else if(LightType::PointLight == settings_.m_lightType)
    {
        drawNum = 4;
    }
    else // LightType::DirectionalLight == settings.m_lightType)
    {
        drawNum = uint8_t(settings_.m_numSplits);
    }

    // Resize and clear batch collectors for each cascade/view
    cascade_batch_collectors_.resize(drawNum);
    for (auto& collector : cascade_batch_collectors_)
    {
        collector.clear();
    }

    bool static_batching_enabled = batch_collector::is_static_mesh_batching_enabled();


    for(const auto& element : models)
    {
        const auto& entity = element.entity;
        const auto& lod_data = element.lod_data;
        const auto& transform_comp = entity.get<transform_component>();
        auto& model_comp = entity.get<model_component>();


        const auto& world_transform = transform_comp.get_transform_global();
        const auto& world_bounds = model_comp.get_world_bounds();

        // For directional lights, perform additional swept AABB test using camera frustum
        bool should_render = true;
        if(LightType::DirectionalLight == settings_.m_lightType && cam != nullptr)
        {
            const auto& camera_frustum = cam->get_frustum();

            const auto& bounds_extents = world_bounds.get_extents();
            const float bounds_radius = math::length(bounds_extents);

            const math::vec3 light_direction(
                directional_light_.m_position.m_x,
                directional_light_.m_position.m_y,
                directional_light_.m_position.m_z
            );
            // Conservative sweep distance: allow the caster to project into the camera frustum
            // across the full shadow range, including its bounding radius.
            const float max_distance = currentSmSettings->m_far + bounds_radius;

            should_render = camera_frustum.test_swept_aabb(world_bounds, light_direction, max_distance);
        }
        
        if(!should_render)
        {
            continue;
        }

        const auto& world_bounds_transform = model_comp.get_world_bounds_transform();
        const auto& local_bounds = model_comp.get_local_bounds(lod_data.current_lod_index);

        const auto& submesh_transforms = model_comp.get_submesh_transforms();
        const auto& bone_transforms = model_comp.get_bone_transforms();
        const auto& skinning_matrices = model_comp.get_skinning_transforms();

        const bool is_skinned = !skinning_matrices.empty();
        
        
        const auto& model = model_comp.get_model();
        if(!model.is_valid())
            continue;


        for(uint8_t ii = 0; ii < drawNum; ++ii)
        {
            // Standard frustum culling
            auto query = lightFrustums[ii].classify_obb(local_bounds, world_bounds_transform);
            if(query == math::volume_query::outside)
            {
                continue;
            }

            const uint8_t viewId = shadowmap_1_id + ii;

            uint8_t renderStateIndex = RenderState::ShadowMap_PackDepth;
            if(LightType::PointLight == settings_.m_lightType && settings_.m_stencilPack)
            {
                renderStateIndex =
                    uint8_t((ii < 2) ? RenderState::ShadowMap_PackDepthHoriz : RenderState::ShadowMap_PackDepthVert);
            }

            const auto& _renderState = render_states[renderStateIndex];

            // Always set the last render frame for tracking
            model_comp.set_last_render_frame(gfx::get_render_frame());
            
            // Check if this model can be batched (static mesh, no skinning)
            const bool can_batch = static_batching_enabled && !is_skinned;
            
            if (can_batch)
            {
                // Collect this model for batching in this specific cascade
                model.submit_for_batching(cascade_batch_collectors_[ii], world_transform, submesh_transforms, lod_data.current_lod_index, 0.0f, &lightFrustums[ii]);
            }
            else
            {
                // Render skinned models individually (original behavior)
                model::submit_callbacks callbacks;
                callbacks.setup_begin = [&](const model::submit_callbacks::params& submit_params)
                {
                    if(stats != nullptr)
                    {
                        stats->drawn_models_for_shadows++;
                        stats->drawn_skinned_models_for_shadows += uint32_t(submit_params.skinned);
                    }
                    
                    auto& prog =
                        submit_params.skinned ? currentSmSettings->m_progPackSkinned : currentSmSettings->m_progPack;
                    prog->begin();
                };
                callbacks.setup_params_per_instance = [&](const model::submit_callbacks::params& submit_params)
                {
                    // Set uniforms.
                    uniforms_.submitPerDrawUniforms();

                    // Apply render state.
                    gfx::set_stencil(_renderState.m_fstencil, _renderState.m_bstencil);
                    gfx::set_state(_renderState.m_state, _renderState.m_blendFactorRgba);
                };
                callbacks.setup_params_per_submesh =
                    [&](const model::submit_callbacks::params& submit_params, const material& mat)
                {
                    auto& prog =
                        submit_params.skinned ? currentSmSettings->m_progPackSkinned : currentSmSettings->m_progPack;

                    gfx::submit(viewId, prog->native_handle(), 0, submit_params.preserve_state);
                };
                callbacks.setup_end = [&](const model::submit_callbacks::params& submit_params)
                {
                    auto& prog =
                        submit_params.skinned ? currentSmSettings->m_progPackSkinned : currentSmSettings->m_progPack;

                    prog->end();
                };

                model.submit(world_transform,
                             submesh_transforms,
                             bone_transforms,
                             skinning_matrices,
                             lod_data.current_lod_index,
                             callbacks,
                             &lightFrustums[ii]);
            }

            any_rendered = true;

            // For CSM (directional lights), if bounds are fully inside this cascade
            // we don't need to render it to farther cascades (they are nested by distance).
            // For point/spot lights, faces cover different directions, so an object
            // must be rendered to ALL faces where it is visible.
            if(LightType::DirectionalLight == settings_.m_lightType
               && query == math::volume_query::inside)
            {
                break;
            }
        }
    }
    
    // Submit batched geometry for each cascade
    if (static_batching_enabled)
    {
        for(uint8_t ii = 0; ii < drawNum; ++ii)
        {
            const uint8_t viewId = shadowmap_1_id + ii;
            
            uint8_t renderStateIndex = RenderState::ShadowMap_PackDepth;
            if(LightType::PointLight == settings_.m_lightType && settings_.m_stencilPack)
            {
                renderStateIndex =
                    uint8_t((ii < 2) ? RenderState::ShadowMap_PackDepthHoriz : RenderState::ShadowMap_PackDepthVert);
            }
            
            const auto& renderState = render_states[renderStateIndex];
            submit_batched_shadow_geometry_cascade(cascade_batch_collectors_[ii], viewId, currentSmSettings, renderState, stats);
        }
    }

    return any_rendered;
}

void shadowmap_generator::submit_batched_shadow_geometry_cascade(batch_collector& collector,
                                                                uint8_t viewId, 
                                                                ShadowMapSettings* currentSmSettings,
                                                                const RenderState& renderState,
                                                                ::unravel::rendering::pipeline_stats* stats)
{
    // Prepare batches for rendering
    submit_context context;
    context.view_id = viewId;
    context.enable_distance_sorting = false; // Shadow maps don't need distance sorting
    context.max_instances_per_batch = 1024;
    context.enable_profiling = false;
    
    collector.prepare_batches(context);
    const auto& prepared_batches = collector.get_prepared_batches();
    
    if (prepared_batches.empty()) 
    {
        return;
    }
    
    // Begin instanced program
    currentSmSettings->m_progPackInstanced->begin();
    
    // Submit each batch
    for (const auto* batch : prepared_batches)
    {
        if (!batch->is_valid() || batch->instances.empty())
        {
            continue;
        }

        if(stats != nullptr)
        {
            stats->drawn_models_for_shadows++;
        }

        // Get mesh from batch key
        const auto mesh_ptr = batch->key.mesh_ptr;
        const auto lod_index = batch->key.lod_index;
        const auto submesh_index = batch->key.submesh_index;

        if (!mesh_ptr)
        {
            continue;
        }

        const auto submesh = mesh_ptr->get_submesh(submesh_index, lod_index);
        if(!submesh)
        {
            continue;
        }

        // Create instance buffer from batch instances
        const auto instance_count = static_cast<uint32_t>(batch->instances.size());
        const auto instance_data_size = static_cast<uint16_t>(instance_vertex_data::packed_size());
        
        // Allocate instance buffer
        bgfx::InstanceDataBuffer instance_buffer;
        bgfx::allocInstanceDataBuffer(&instance_buffer, instance_count, instance_data_size);
        if (!instance_buffer.data)
        {
            continue; // Skip this batch if allocation failed
        }

        // Pack instance data into buffer
        auto* buffer_data = reinterpret_cast<instance_vertex_data*>(instance_buffer.data);
        for (size_t i = 0; i < batch->instances.size(); ++i)
        {
            buffer_data[i] = instance_vertex_data(batch->instances[i]);
        }

        // Set instance data buffer
        bgfx::setInstanceDataBuffer(&instance_buffer);
        
        // Bind vertex and index buffers for the specific submesh
        mesh_ptr->bind_render_buffers_for_submesh(submesh, lod_index);
        
        // Set uniforms and render state
        uniforms_.submitPerDrawUniforms();
        gfx::set_stencil(renderState.m_fstencil, renderState.m_bstencil);
        gfx::set_state(renderState.m_state, renderState.m_blendFactorRgba);

        // Submit the instanced draw call
        gfx::submit(viewId, currentSmSettings->m_progPackInstanced->native_handle(), 0, false);
    }

    currentSmSettings->m_progPackInstanced->end();

    // Update statistics
    if(stats != nullptr)
    {
        const auto& batch_stats = collector.get_stats();
        stats->add_batch_stats(batch_stats);
    }
    
    // Clear batches to invalidate all transform pointers and free memory
    collector.clear();
}

void Programs::init(rtti::context& ctx)
{
    // clang-format off
    auto& am = ctx.get_cached<asset_manager>();
    // clang-format on

    auto loadProgram = [&](const std::string& vs, const std::string& fs)
    {
        auto vs_shader = am.get_asset<gfx::shader>("engine:/data/shaders/shadowmaps/" + vs + ".sc");
        auto fs_shadfer = am.get_asset<gfx::shader>("engine:/data/shaders/shadowmaps/" + fs + ".sc");

        return std::make_shared<gpu_program>(vs_shader, fs_shadfer);
    };

    // clang-format off
     // Misc.
    m_black        = loadProgram("vs_shadowmaps_color",         "fs_shadowmaps_color_black");

    // Blur.
    m_vBlur[PackDepth::RGBA] = loadProgram("vs_shadowmaps_vblur", "fs_shadowmaps_vblur");
    m_hBlur[PackDepth::RGBA] = loadProgram("vs_shadowmaps_hblur", "fs_shadowmaps_hblur");
    m_vBlur[PackDepth::VSM]  = loadProgram("vs_shadowmaps_vblur", "fs_shadowmaps_vblur_vsm");
    m_hBlur[PackDepth::VSM]  = loadProgram("vs_shadowmaps_hblur", "fs_shadowmaps_hblur_vsm");

    // Draw depth.
    m_drawDepth[PackDepth::RGBA] = loadProgram("vs_shadowmaps_unpackdepth", "fs_shadowmaps_unpackdepth");
    m_drawDepth[PackDepth::VSM]  = loadProgram("vs_shadowmaps_unpackdepth", "fs_shadowmaps_unpackdepth_vsm");

    // Pack depth.
    m_packDepth[DepthImpl::InvZ][PackDepth::RGBA] = loadProgram("packdepth/vs_shadowmaps_packdepth", "packdepth/fs_shadowmaps_packdepth");
    m_packDepth[DepthImpl::InvZ][PackDepth::VSM]  = loadProgram("packdepth/vs_shadowmaps_packdepth", "packdepth/fs_shadowmaps_packdepth_vsm");

    m_packDepth[DepthImpl::Linear][PackDepth::RGBA] = loadProgram("packdepth/vs_shadowmaps_packdepth_linear", "packdepth/fs_shadowmaps_packdepth_linear");
    m_packDepth[DepthImpl::Linear][PackDepth::VSM]  = loadProgram("packdepth/vs_shadowmaps_packdepth_linear", "packdepth/fs_shadowmaps_packdepth_vsm_linear");

    m_packDepthSkinned[DepthImpl::InvZ][PackDepth::RGBA] = loadProgram("packdepth/vs_shadowmaps_packdepth_skinned", "packdepth/fs_shadowmaps_packdepth");
    m_packDepthSkinned[DepthImpl::InvZ][PackDepth::VSM]  = loadProgram("packdepth/vs_shadowmaps_packdepth_skinned", "packdepth/fs_shadowmaps_packdepth_vsm");

    m_packDepthSkinned[DepthImpl::Linear][PackDepth::RGBA] = loadProgram("packdepth/vs_shadowmaps_packdepth_linear_skinned", "packdepth/fs_shadowmaps_packdepth_linear");
    m_packDepthSkinned[DepthImpl::Linear][PackDepth::VSM]  = loadProgram("packdepth/vs_shadowmaps_packdepth_linear_skinned", "packdepth/fs_shadowmaps_packdepth_vsm_linear");

    // Pack depth instanced (for static mesh batching).
    m_packDepthInstanced[DepthImpl::InvZ][PackDepth::RGBA] = loadProgram("packdepth/vs_shadowmaps_packdepth_instanced", "packdepth/fs_shadowmaps_packdepth");
    m_packDepthInstanced[DepthImpl::InvZ][PackDepth::VSM]  = loadProgram("packdepth/vs_shadowmaps_packdepth_instanced", "packdepth/fs_shadowmaps_packdepth_vsm");

    m_packDepthInstanced[DepthImpl::Linear][PackDepth::RGBA] = loadProgram("packdepth/vs_shadowmaps_packdepth_linear_instanced", "packdepth/fs_shadowmaps_packdepth_linear");
    m_packDepthInstanced[DepthImpl::Linear][PackDepth::VSM]  = loadProgram("packdepth/vs_shadowmaps_packdepth_linear_instanced", "packdepth/fs_shadowmaps_packdepth_vsm_linear");

}

}
} // namespace unravel
