#include "shadow.h"
#include "bgfx/bgfx.h"

#include <engine/assets/asset_manager.h>
#include <engine/rendering/pipeline/deferred/pipeline.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/model.h>
#include <engine/rendering/renderer.h>
#include <engine/rendering/skinning_palette_buffer.h>

#include <graphics/index_buffer.h>
#include <graphics/render_pass.h>
#include <graphics/render_view.h>
#include <graphics/texture.h>
#include <graphics/vertex_buffer.h>

#include <atomic>
#include <cstring>

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

auto apply_shadow_cull(uint64_t base_state, cull_type cull) -> uint64_t
{
    uint64_t state = base_state & ~(BGFX_STATE_CULL_CCW | BGFX_STATE_CULL_CW);
    if(cull == cull_type::counter_clockwise)
    {
        state |= BGFX_STATE_CULL_CCW;
    }
    else if(cull == cull_type::clockwise)
    {
        state |= BGFX_STATE_CULL_CW;
    }
    return state;
}

auto resolve_cutout_pack_program(const Programs& programs, gpu_program* opaque_prog) -> gpu_program*
{
    if(!opaque_prog)
    {
        return opaque_prog;
    }

    for(uint32_t depth = 0; depth < DepthImpl::Count; ++depth)
    {
        for(uint32_t pack = 0; pack < PackDepth::Count; ++pack)
        {
            if(programs.m_packDepth[depth][pack].get() == opaque_prog)
            {
                return programs.m_packDepthCutout[depth][pack].get();
            }
            if(programs.m_packDepthInstanced[depth][pack].get() == opaque_prog)
            {
                return programs.m_packDepthInstancedCutout[depth][pack].get();
            }
            if(programs.m_packDepthSkinned[depth][pack].get() == opaque_prog)
            {
                return programs.m_packDepthSkinnedCutout[depth][pack].get();
            }
            if(programs.m_packDepthSkinnedInstanced[depth][pack].get() == opaque_prog)
            {
                return programs.m_packDepthSkinnedInstancedCutout[depth][pack].get();
            }
        }
    }

    return opaque_prog;
}

/// The instanced skinned program with the depth and packing of @p skinned_prog (a settings table m_progPackSkinned).
auto resolve_skinned_instanced_pack_program(const Programs& programs, gpu_program* skinned_prog) -> gpu_program*
{
    for(uint32_t depth = 0; depth < DepthImpl::Count; ++depth)
    {
        for(uint32_t pack = 0; pack < PackDepth::Count; ++pack)
        {
            if(skinned_prog != nullptr && programs.m_packDepthSkinned[depth][pack].get() == skinned_prog)
            {
                return programs.m_packDepthSkinnedInstanced[depth][pack].get();
            }
        }
    }
    return nullptr;
}

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
* Calculate cascade split depths over the view camera's shadow range (parallel-split shadow maps).
* Each split blends a logarithmic and a uniform split of the range by splitLambda.
* 
* @param cascadeSplits Output array of normalized split depths [0..1] for each cascade
* @param numSplits Number of cascade splits (typically 4)
* @param nearClip Near clip plane distance
* @param farClip Far clip plane distance  
* @param splitLambda Blend factor between logarithmic (1.0) and uniform (0.0) distribution
*/
void calculateCascadeSplits(float* cascadeSplits, uint8_t numSplits, float nearClip, float farClip, float splitLambda)
{
    // Practical split scheme: split i sits at the lambda blend of the logarithmic and the
    // uniform distribution of the shadow range, and the last split is the far plane. (The
    // previous odd-index variant topped out at 0.7 of the range, so the far plane meant
    // nothing and the outer part of the shadow distance had no cascade at all.)
    const float clipRange = farClip - nearClip;
    const float ratio = farClip / nearClip;
    for(uint32_t i = 0; i < numSplits; i++)
    {
        const float si = float(i + 1) / float(numSplits);
        const float logSplit = nearClip * bx::pow(ratio, si);
        const float uniformSplit = nearClip + clipRange * si;
        const float d = splitLambda * (logSplit - uniformSplit) + uniformSplit;
        cascadeSplits[i] = (d - nearClip) / clipRange;
    }
}

/// A stabilized cascade's depth centre snaps to this fraction of the shadow distance (1/16 of its 2 * far range).
constexpr float CASCADE_DEPTH_SNAP_FRACTION = 1.0f / 8.0f;
/// Render frames a caster's shadow must hold still before it joins a light's static copy: one that changed recently
/// is expected to change again, and every caster joining or leaving renders the copy again.
constexpr uint64_t MOVER_SETTLE_FRAMES = 30;
/// Casters that hold still a light needs in reach before it draws over a static copy: below that, drawing them again
/// costs less than restoring the whole tile.
constexpr uint32_t STATIC_COPY_MIN_CASTERS = 4;

/// Mixes @p value into a cached-map key (shadow map caching).
auto mix_key(uint64_t key, uint64_t value) -> uint64_t
{
    return uint64_t(batch_collector_detail::mix_hash(size_t(key ^ (value + 0x9e3779b97f4a7c15ULL + (key << 6) + (key >> 2)))));
}

/// Mixes the exact bits of @p count floats into a cached-map key.
auto mix_floats(uint64_t key, const float* values, size_t count) -> uint64_t
{
    for(size_t index = 0; index < count; ++index)
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &values[index], sizeof(bits));
        key = mix_key(key, bits);
    }
    return key;
}

std::atomic<bool> map_caching_enabled{true};
std::atomic<uint64_t> cached_maps_epoch{1};

} // namespace

void set_map_caching_enabled(bool enabled)
{
    if(map_caching_enabled.exchange(enabled, std::memory_order_relaxed) != enabled)
    {
        invalidate_cached_maps();
    }
}

auto is_map_caching_enabled() -> bool
{
    return map_caching_enabled.load(std::memory_order_relaxed);
}

void invalidate_cached_maps()
{
    cached_maps_epoch.fetch_add(1, std::memory_order_relaxed);
}

auto get_cached_maps_epoch() -> uint64_t
{
    return cached_maps_epoch.load(std::memory_order_relaxed);
}

auto make_light_projection(float fov_y_degrees,
                           float aspect,
                           float near_plane,
                           float far_plane,
                           bool homogeneous_depth,
                           bool is_linear_depth) -> light_projection
{
    /// Row-major elements that map view z to clip z: the scale and the offset.
    constexpr size_t PROJ_Z_SCALE_INDEX = 10;
    constexpr size_t PROJ_Z_OFFSET_INDEX = 14;
    light_projection result;
    result.homogeneous_depth = homogeneous_depth;
    bx::mtxProj(result.cull_proj, fov_y_degrees, aspect, near_plane, far_plane, homogeneous_depth);
    bx::memCopy(result.render_proj, result.cull_proj, sizeof(result.render_proj));
    // For linear depth, prevent depth division by variable w-component in shaders and divide
    // here by far plane.
    if(is_linear_depth)
    {
        result.render_proj[PROJ_Z_SCALE_INDEX] /= far_plane;
        result.render_proj[PROJ_Z_OFFSET_INDEX] /= far_plane;
    }
    return result;
}

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

    shadow_atlas::get().release(get_atlas_owner());
    atlas_tile_.reset();
    atlas_generation_ = UINT64_MAX;
    atlas_placement_ = 0;
    destroy_own_maps();
    invalidate_view_keys();
}

void shadowmap_generator::destroy_own_maps()
{
    bool destroyed = false;
    for(int i = 0; i < ShadowMapRenderTargets::Count; ++i)
    {
        if(bgfx::isValid(rt_shadow_map_[i]))
        {
            bgfx::destroy(rt_shadow_map_[i]);
            rt_shadow_map_[i] = {bgfx::kInvalidHandle};
            destroyed = true;
        }
    }

    if(bgfx::isValid(rt_blur_))
    {
        bgfx::destroy(rt_blur_);
        rt_blur_ = {bgfx::kInvalidHandle};
    }
    if(destroyed)
    {
        invalidate_view_keys();
    }
}

void shadowmap_generator::invalidate_view_keys()
{
    for(auto& is_valid : view_key_valid_)
    {
        is_valid = false;
    }
    casters_key_valid_ = false;
    static_copy_valid_ = false;
}

void shadowmap_generator::deinit_uniforms()
{
    if(bgfx::isValid(tex_color_))
    {
        bgfx::destroy(tex_color_);
    }
    if(bgfx::isValid(shadow_atlas_rect_))
    {
        bgfx::destroy(shadow_atlas_rect_);
        shadow_atlas_rect_ = {bgfx::kInvalidHandle};
    }
    if(bgfx::isValid(static_depth_sampler_))
    {
        bgfx::destroy(static_depth_sampler_);
        static_depth_sampler_ = {bgfx::kInvalidHandle};
    }
    if(bgfx::isValid(restore_params_))
    {
        bgfx::destroy(restore_params_);
        restore_params_ = {bgfx::kInvalidHandle};
    }

    for(int i = 0; i < ShadowMapRenderTargets::Count; ++i)
    {
        if(bgfx::isValid(shadow_map_[i]))
        {
            bgfx::destroy(shadow_map_[i]);
        }
        if(bgfx::isValid(shadow_depth_[i]))
        {
            bgfx::destroy(shadow_depth_[i]);
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
    shadow_atlas_rect_ = bgfx::createUniform("u_shadowAtlasRect", bgfx::UniformType::Vec4);
    static_depth_sampler_ = bgfx::createUniform("s_staticDepth", bgfx::UniformType::Sampler);
    restore_params_ = bgfx::createUniform("u_restoreParams", bgfx::UniformType::Vec4);
    shadow_map_[0] = bgfx::createUniform("s_shadowMap0", bgfx::UniformType::Sampler);
    shadow_map_[1] = bgfx::createUniform("s_shadowMap1", bgfx::UniformType::Sampler);
    shadow_map_[2] = bgfx::createUniform("s_shadowMap2", bgfx::UniformType::Sampler);
    shadow_map_[3] = bgfx::createUniform("s_shadowMap3", bgfx::UniformType::Sampler);
    shadow_depth_[0] = bgfx::createUniform("s_shadowDepth0", bgfx::UniformType::Sampler);
    shadow_depth_[1] = bgfx::createUniform("s_shadowDepth1", bgfx::UniformType::Sampler);
    shadow_depth_[2] = bgfx::createUniform("s_shadowDepth2", bgfx::UniformType::Sampler);
    shadow_depth_[3] = bgfx::createUniform("s_shadowDepth3", bgfx::UniformType::Sampler);

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
    if(uses_atlas_)
    {
        return split == 0 && atlas_tile_ ? shadow_atlas::get().get_color_texture() : bgfx::TextureHandle{bgfx::kInvalidHandle};
    }
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

void shadowmap_generator::submit_uniforms(uint8_t stage, uint8_t compare_stage) const
{
    if(!bgfx::isValid(tex_color_))
    {
        return;
    }
    // uniforms_.submitConstUniforms();
    // uniforms_.submitPerFrameUniforms();
    uniforms_.submitPerDrawUniforms();

    // Depth maps hold raw float depth: point sampling (interpolated depth is neither of the
    // two surfaces it lies between) and clamped edges. Blurred VSM / ESM maps interpolate.
    const bool filtered_map = (SmImpl::VSM == settings_.m_smImpl) || (SmImpl::ESM == settings_.m_smImpl);
    const uint32_t sampler_flags = (filtered_map ? 0u : uint32_t(BGFX_SAMPLER_POINT)) | BGFX_SAMPLER_UVW_CLAMP;
    // Bilinear comparison: each fetch filters the results of the 2x2 texels around it.
    constexpr uint32_t compare_flags = BGFX_SAMPLER_COMPARE_LEQUAL | BGFX_SAMPLER_MIP_POINT | BGFX_SAMPLER_UVW_CLAMP;
    if(uses_atlas_)
    {
        if(!atlas_tile_)
        {
            return;
        }
        const auto& atlas = shadow_atlas::get();
        bgfx::setTexture(stage, shadow_map_[0], atlas.get_color_texture(), sampler_flags);
        if(compare_stage != no_compare_stage)
        {
            bgfx::setTexture(compare_stage, shadow_depth_[0], atlas.get_depth_texture(), compare_flags);
        }
        bgfx::setUniform(shadow_atlas_rect_, atlas_rect_);
        return;
    }
    for(uint8_t ii = 0; ii < ShadowMapRenderTargets::Count; ++ii)
    {
        if(!bgfx::isValid(rt_shadow_map_[ii]))
        {
            continue;
        }

        bgfx::setTexture(stage + ii, shadow_map_[ii], bgfx::getTexture(rt_shadow_map_[ii]), sampler_flags);
    }
    if(filtered_map || compare_stage == no_compare_stage)
    {
        return;
    }
    constexpr uint8_t depth_attachment = 1;
    for(uint8_t ii = 0; ii < ShadowMapRenderTargets::Count; ++ii)
    {
        if(!bgfx::isValid(rt_shadow_map_[ii]))
        {
            continue;
        }
        bgfx::setTexture(compare_stage + ii,
                         shadow_depth_[ii],
                         bgfx::getTexture(rt_shadow_map_[ii], depth_attachment),
                         compare_flags);
    }
}

auto shadowmap_generator::already_resolved() const -> bool
{
    // A tile rendered before the atlas last moved its tiles holds another light's texels, or none.
    const bool is_tile_current = !uses_atlas_ || atlas_generation_ == shadow_atlas::get().get_generation();
    return last_resolve_frame_ == gfx::get_render_frame() && is_tile_current;
}

void shadowmap_generator::mark_resolved()
{
    last_resolve_frame_ = gfx::get_render_frame();
    atlas_generation_ = shadow_atlas::get().get_generation();
}

auto shadowmap_generator::needs_clear() const -> bool
{
    // A new or moved tile holds whatever the atlas held there.
    return needs_clear_ || (uses_atlas_ && atlas_generation_ != shadow_atlas::get().get_generation());
}

auto shadowmap_generator::uses_shadow_atlas(const light& l) -> bool
{
    const bool is_local = l.type == light_type::spot || l.type == light_type::point;
    const bool compares_depth = l.shadow_params.type == sm_impl::hard || l.shadow_params.type == sm_impl::pcf ||
                                l.shadow_params.type == sm_impl::pcss;
    return l.casts_shadows && is_local && compares_depth;
}

auto shadowmap_generator::make_atlas_request(const light& l, float importance) const -> shadow_atlas_request
{
    return {get_atlas_owner(), std::uint16_t(1u << uint32_t(convert(l.shadow_params.resolution))), importance};
}

auto shadowmap_generator::has_shadow_map() const -> bool
{
    return uses_atlas_ ? atlas_tile_.has_value() : bgfx::isValid(rt_shadow_map_[0]);
}

auto shadowmap_generator::get_map_uv_rect() const -> math::vec4
{
    return uses_atlas_ ? math::vec4(atlas_rect_[0], atlas_rect_[1], atlas_rect_[2], atlas_rect_[3]) : math::vec4(0.0f, 0.0f, 1.0f, 1.0f);
}

auto shadowmap_generator::get_atlas_tile() const -> hpp::optional<shadow_atlas_tile>
{
    return uses_atlas_ ? atlas_tile_ : hpp::optional<shadow_atlas_tile>{};
}

auto shadowmap_generator::has_linear_depth() const -> bool
{
    return settings_.m_depthImpl == DepthImpl::Linear;
}

auto shadowmap_generator::get_world_to_map(uint8_t face) const -> math::mat4
{
    // The lighting pass: u_lightMtx for a spot light; u_shadowMapMtx<face> after u_lightMtx for a point light.
    const math::mat4 light_matrix = math::make_mat4(light_mtx_);
    if(settings_.m_lightType != LightType::PointLight)
    {
        return light_matrix;
    }
    const uint8_t last_face = uint8_t(TetrahedronFaces::Count - 1);
    return math::make_mat4(shadow_map_mtx_[face < last_face ? face : last_face]) * light_matrix;
}

void shadowmap_generator::update_atlas_tile()
{
    const auto& atlas = shadow_atlas::get();
    const auto tile = atlas.find(get_atlas_owner());
    const uint64_t placement = atlas.find_placement(get_atlas_owner());
    // Placed again, even on the same square: another light may have rendered there in between.
    if(tile != atlas_tile_ || placement != atlas_placement_)
    {
        needs_clear_ = true;
    }
    atlas_tile_ = tile;
    atlas_placement_ = placement;
    if(!tile)
    {
        return;
    }
    const float atlas_size = float(atlas.get_size());
    atlas_rect_[0] = float(tile->x) / atlas_size;
    // View rects count rows from the top; with the texture origin at the bottom (GL) the tile's first row sits at the
    // far end of v. Inside the tile the light's own matrices already flip v for that origin (mtxBias).
    const bool is_origin_bottom_left = bgfx::getCaps()->originBottomLeft;
    atlas_rect_[1] = is_origin_bottom_left ? 1.0f - float(tile->y + tile->size) / atlas_size : float(tile->y) / atlas_size;
    atlas_rect_[2] = float(tile->size) / atlas_size;
    atlas_rect_[3] = float(tile->size) / atlas_size;
}

void shadowmap_generator::update(const camera& cam, const light& l, const math::transform& ltrans, bool is_active)
{
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

    const bool was_in_atlas = uses_atlas_;
    uses_atlas_ = uses_shadow_atlas(l);
    if(uses_atlas_)
    {
        // The map lives in the light's atlas tile, at the tile's edge (the request, or less while the atlas is full).
        destroy_own_maps();
        update_atlas_tile();
        current_shadow_map_size_ = atlas_tile_ ? atlas_tile_->size : shadowMapSize;
        recreateTextures = false;
    }
    else if(was_in_atlas)
    {
        shadow_atlas::get().release(get_atlas_owner());
        atlas_tile_.reset();
        recreateTextures = true;
    }

    if(recreateTextures)
    {
        current_shadow_map_size_ = shadowMapSize;
        needs_clear_ = true;
        invalidate_view_keys();

        // Float depth (moments for VSM). The RGBA8 packing this replaced decoded with a
        // systematic error of up to 2e-3 of the depth range (the 1/255 steps of UNORM8 against
        // the 1/256 the packing assumes), which on a 2 * far deep directional map was metres
        // of world depth, so every bias value was first a calibration of that error.
        const bgfx::TextureFormat::Enum color_format =
            (SmImpl::VSM == settings_.m_smImpl) ? bgfx::TextureFormat::RG32F : bgfx::TextureFormat::R32F;

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
                                      color_format,
                                      BGFX_TEXTURE_RT),
                bgfx::createTexture2D(current_shadow_map_size_,
                                      current_shadow_map_size_,
                                      false,
                                      1,
                                      bgfx::TextureFormat::D24S8,
                                      BGFX_TEXTURE_RT),
            };
            bgfx::Attachment attachments[2];
            attachments[0].init(fbtextures[0], bgfx::Access::Write, 0, 1, 0, BGFX_ATTACHMENT_AUTO_GEN_MIPS);
            attachments[1].init(fbtextures[1], bgfx::Access::Write, 0, 1, 0, BGFX_ATTACHMENT_NONE);

            rt_shadow_map_[0] = bgfx::createFrameBuffer(BX_COUNTOF(attachments), attachments, true);
        }

        // Only directional lights have cascades. A spot light renders into map 0, and a point light
        // packs its four tetrahedron faces into viewports of map 0.
        const int map_count = (LightType::DirectionalLight == settings_.m_lightType) ? settings_.m_numSplits : 1;
        {
            for(uint8_t ii = 1; ii < ShadowMapRenderTargets::Count; ++ii)
            {
                if(bgfx::isValid(rt_shadow_map_[ii]))
                {
                    bgfx::destroy(rt_shadow_map_[ii]);
                    rt_shadow_map_[ii] = {bgfx::kInvalidHandle};
                }

                if(ii < map_count)
                {
                    bgfx::TextureHandle fbtextures[] = {
                        bgfx::createTexture2D(current_shadow_map_size_,
                                              current_shadow_map_size_,
                                              false,
                                              1,
                                              color_format,
                                              BGFX_TEXTURE_RT),
                        bgfx::createTexture2D(current_shadow_map_size_,
                                              current_shadow_map_size_,
                                              false,
                                              1,
                                              bgfx::TextureFormat::D24S8,
                                              BGFX_TEXTURE_RT),
                    };

                    bgfx::Attachment attachments[2];
                    attachments[0].init(fbtextures[0], bgfx::Access::Write, 0, 1, 0, BGFX_ATTACHMENT_AUTO_GEN_MIPS);
                    attachments[1].init(fbtextures[1], bgfx::Access::Write, 0, 1, 0, BGFX_ATTACHMENT_NONE);
                    
                    rt_shadow_map_[ii] = bgfx::createFrameBuffer(BX_COUNTOF(attachments), attachments, true);
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
            rt_blur_ = bgfx::createFrameBuffer(current_shadow_map_size_, current_shadow_map_size_, color_format);
        }
    }

    float currentShadowMapSizef = float(int16_t(current_shadow_map_size_));

    // Update uniforms.

    uniforms_.m_shadowMapTexelSize = 1.0f / currentShadowMapSizef;
    uniforms_.m_shadowMapBias = currentSmSettings->m_bias;
    uniforms_.m_shadowMapOffset = currentSmSettings->m_normalOffset;
    uniforms_.m_shadowBiasParams[0] = l.shadow_params.slope_bias;
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
    // The backend's own clip depth range, not the engine's projection convention: it decides the
    // window depth the depth attachments hold.
    uniforms_.m_hardwareDepthScale = bgfx::getCaps()->homogeneousDepth ? 1.0f : 2.0f;
    bool originBottomLeft = gfx::is_origin_bottom_left();

    // Compute transform matrices.
    auto& lightView = light_view_;
    auto& lightProj = light_proj_;
    auto& lightFrustums = light_frustums_;

    float mtxYpr[TetrahedronFaces::Count][16];

    // Spot / point lights render with perspective[..].render_proj and cull with its cull_proj
    // (see light_projection). Directional cascades cull with the projections they render with.
    light_projection perspective[ProjType::Count];
    const bool is_linear_depth = DepthImpl::Linear == settings_.m_depthImpl;

    if(LightType::SpotLight == settings_.m_lightType)
    {
        const float fovy = settings_.m_coverageSpotL;
        const float aspect = 1.0f;
        perspective[ProjType::Horizontal] =
            make_light_projection(fovy, aspect, currentSmSettings->m_near, currentSmSettings->m_far, false, is_linear_depth);
        bx::memCopy(lightProj[ProjType::Horizontal], perspective[ProjType::Horizontal].render_proj, sizeof(lightProj[0]));

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

            perspective[ProjType::Vertical] =
                make_light_projection(fovx, aspect, currentSmSettings->m_near, currentSmSettings->m_far, false, is_linear_depth);
            bx::memCopy(lightProj[ProjType::Vertical], perspective[ProjType::Vertical].render_proj, sizeof(lightProj[0]));

            ypr[TetrahedronFaces::Green][2] = bx::toRad(180.0f);
            ypr[TetrahedronFaces::Yellow][2] = bx::toRad(0.0f);
            ypr[TetrahedronFaces::Blue][2] = bx::toRad(90.0f);
            ypr[TetrahedronFaces::Red][2] = bx::toRad(-90.0f);
        }

        const float fovx = 143.98570868f + 7.8f + settings_.m_fovXAdjust;
        const float fovy = 125.26438968f + 3.0f + settings_.m_fovYAdjust;
        const float aspect = bx::tan(bx::toRad(fovx * 0.5f)) / bx::tan(bx::toRad(fovy * 0.5f));

        perspective[ProjType::Horizontal] =
            make_light_projection(fovy, aspect, currentSmSettings->m_near, currentSmSettings->m_far, homogeneousDepth, is_linear_depth);
        bx::memCopy(lightProj[ProjType::Horizontal], perspective[ProjType::Horizontal].render_proj, sizeof(lightProj[0]));

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
        // Cascaded Shadow Map calculation (parallel-split shadow maps)
        // ============================================================================

        const uint8_t maxNumSplits = 4;
        BX_ASSERT(maxNumSplits >= settings_.m_numSplits, "Error! Max num splits.");

        // Get camera parameters
        const float nearClip = currentSmSettings->m_near;
        const float farClip = currentSmSettings->m_far;
        const float clipRange = farClip - nearClip;

        // Calculate cascade split depths as a logarithmic/uniform blend
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

            // The crop maps 2 / scalex world units onto the full map width: one texel of
            // this cascade in world units, the unit every bias is expressed in.
            uniforms_.m_csmTexelWorld[ii] = 2.0f / (scalex * float(current_shadow_map_size_));

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
            // z-offset: center depth range on cascade frustum. Stabilized cascades snap it too, so a cascade holds still
            // (and its cached map stays valid) until the camera moves a step along the light; the 2 * far deep range
            // gives up at most half a step of its margin for it.
            float depthCenter = lightSpaceCenter.z;
            if(settings_.m_stabilize)
            {
                const float depthStep = currentSmSettings->m_far * CASCADE_DEPTH_SNAP_FRACTION;
                depthCenter = bx::round(depthCenter / depthStep) * depthStep;
            }
            mtxCrop[14] = -depthCenter;

            // Final projection = crop * base projection
            bx::mtxMul(lightProj[ii], mtxCrop, mtxProj);

            lastSplitDist = splitDist;
        }
    }

    // Culling frustums of the perspective lights: always from the projection AS BUILT and with
    // the depth convention it was built with - never from lightProj, whose depth rows linear
    // depth has rescaled, and never assuming the backend's convention (the spot and the
    // stencil-packed vertical faces are built for [0, 1] depth on every backend).
    const auto update_perspective_frustum = [&](uint8_t view, ProjType::Enum proj_type) -> void
    {
        const auto& projection = perspective[proj_type];
        lightFrustums[view].update(math::make_mat4(lightView[view]),
                                   math::make_mat4(projection.cull_proj),
                                   projection.homogeneous_depth);
    };
    if(LightType::SpotLight == settings_.m_lightType)
    {
        update_perspective_frustum(0, ProjType::Horizontal);
    }
    else if(LightType::PointLight == settings_.m_lightType)
    {
        const ProjType::Enum side_faces_proj = settings_.m_stencilPack ? ProjType::Vertical : ProjType::Horizontal;
        update_perspective_frustum(TetrahedronFaces::Green, ProjType::Horizontal);
        update_perspective_frustum(TetrahedronFaces::Yellow, ProjType::Horizontal);
        update_perspective_frustum(TetrahedronFaces::Blue, side_faces_proj);
        update_perspective_frustum(TetrahedronFaces::Red, side_faces_proj);
    }
    else // LightType::DirectionalLight == settings.m_lightType
    {
        lightFrustums[0].update(math::make_mat4(lightView[0]), math::make_mat4(lightProj[0]), homogeneousDepth);
        lightFrustums[1].update(math::make_mat4(lightView[0]), math::make_mat4(lightProj[1]), homogeneousDepth);
        lightFrustums[2].update(math::make_mat4(lightView[0]), math::make_mat4(lightProj[2]), homogeneousDepth);
        lightFrustums[3].update(math::make_mat4(lightView[0]), math::make_mat4(lightProj[3]), homogeneousDepth);
    }

    update_bias_uniforms(originBottomLeft);

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

void shadowmap_generator::update_bias_uniforms(bool origin_bottom_left)
{
    // Stored depth = 0.5 * z_ndc + zadd (mtxBias), z_ndc = proj[10] * z + proj[14] for ortho and
    // linear maps and proj[10] + proj[14] / z for perspective 1/z maps; the crop matrices of
    // the cascades do not touch z. The shader turns a world-space bias into stored depth with
    // these derivatives. +v of the map runs along +up of the light view when the texture
    // origin is bottom-left (mtxBias ymul), along -up otherwise.
    float* params = uniforms_.m_shadowBiasParams;
    float* persp = uniforms_.m_perspTexelParams;
    float* axis_u = uniforms_.m_shadowAxisU;
    float* axis_v = uniforms_.m_shadowAxisV;
    params[1] = params[2] = params[3] = 0.0f;
    for(int ii = 0; ii < 4; ++ii)
    {
        persp[ii] = 0.0f;
        axis_u[ii] = 0.0f;
        axis_v[ii] = 0.0f;
    }
    const float v_sign = origin_bottom_left ? 1.0f : -1.0f;
    const auto set_axes = [&](const float* view)
    {
        axis_u[0] = view[0];
        axis_u[1] = view[4];
        axis_u[2] = view[8];
        axis_v[0] = v_sign * view[1];
        axis_v[1] = v_sign * view[5];
        axis_v[2] = v_sign * view[9];
    };
    const float map_size = float(current_shadow_map_size_);
    if(LightType::DirectionalLight == settings_.m_lightType)
    {
        params[1] = 0.5f * light_proj_[0][10];
        set_axes(light_view_[0]);
        return;
    }
    for(int ii = 0; ii < 4; ++ii)
    {
        uniforms_.m_csmTexelWorld[ii] = 0.0f;
    }
    const float* proj = light_proj_[ProjType::Horizontal];
    params[1] = 0.5f * proj[10];
    params[2] = -0.5f * proj[14];
    params[3] = params[2];
    // Map extent per unit distance along the light axis: 2 z / proj[0] wide, 2 z / proj[5] tall.
    persp[1] = 2.0f / proj[0];
    persp[2] = 2.0f / proj[5];
    if(LightType::SpotLight == settings_.m_lightType)
    {
        persp[0] = persp[2] / map_size;
        set_axes(light_view_[0]);
        return;
    }
    // Point light: every tetrahedron face covers half the map height; the stencil-packed
    // vertical faces use their own projection for the depth derivative. The axes stay zero:
    // the shader takes each face's receiver plane from its face matrix (omniPlaneGradient).
    persp[0] = persp[2] / (map_size * 0.5f);
    if(settings_.m_stencilPack)
    {
        params[3] = -0.5f * light_proj_[ProjType::Vertical][14];
    }
}

void shadowmap_generator::generate_shadowmaps(const shadow_map_models_t& models, const camera& cam, ::unravel::rendering::pipeline_stats* stats)
{
    mark_resolved();
    if(uses_atlas_ && !atlas_tile_)
    {
        // Not even the smallest tile was free: the light shades unshadowed this frame (has_shadow_map).
        return;
    }
    // Where the spot and point maps go: the light's atlas tile, or its own map.
    const bgfx::FrameBufferHandle map_target = uses_atlas_ ? shadow_atlas::get().get_frame_buffer() : rt_shadow_map_[0];
    const uint16_t map_x = uses_atlas_ ? atlas_tile_->x : uint16_t(0);
    const uint16_t map_y = uses_atlas_ ? atlas_tile_->y : uint16_t(0);

    ShadowMapSettings* currentSmSettings =
        &sm_settings_[settings_.m_lightType][settings_.m_depthImpl][settings_.m_smImpl];

    const bool is_directional = LightType::DirectionalLight == settings_.m_lightType;

    // Work out what every view would draw, then render only the views whose content changed (shadow map caching): the
    // others keep last frame's content, untouched (no clear, no draws). Decided before any view is set up: a light
    // that renders nothing takes no views.
    shadow_collection collection = prepare_shadow_casters(models, light_frustums_, currentSmSettings, &cam);
    if(!collection.reuse_all && is_directional)
    {
        collect_shadow_casters(models, light_frustums_, currentSmSettings, &cam, caster_filter::all, collection, stats);
    }
    uint64_t view_keys[ShadowMapRenderTargets::Count] = {};
    const uint8_t render_views = collection.reuse_all ? uint8_t(0) : select_views_to_render(collection, view_keys);
    if(stats != nullptr)
    {
        // A local light's map counts once, a cascade once each.
        const uint32_t view_count = get_view_count();
        const uint32_t rendered = is_directional ? uint32_t(bx::countBits(uint32_t(render_views))) : (render_views != 0u ? 1u : 0u);
        const uint32_t regions = is_directional ? view_count : 1u;
        stats->shadow_maps_rendered += rendered;
        stats->shadow_maps_reused += regions - rendered;
    }
    // Every view holds this collection's content once the views below render: the next time the casters key matches,
    // nothing needs collecting.
    if(!collection.reuse_all)
    {
        casters_key_ = collection.casters_key;
        casters_key_valid_ = true;
    }
    if(render_views == 0u)
    {
        return;
    }

    // A local light with casters that move in reach draws only them, over its static copy. The copy renders first, in
    // views of its own, when the casters that hold still changed.
    const bool uses_static_copy = can_use_static_copy(collection);
    if(uses_static_copy)
    {
        refresh_static_copy(models, currentSmSettings, cam, collection, stats);
        collect_shadow_casters(models, light_frustums_, currentSmSettings, &cam, caster_filter::movers_only, collection, stats);
        if(stats != nullptr)
        {
            stats->shadow_maps_restored++;
        }
    }
    else if(!is_directional)
    {
        collect_shadow_casters(models, light_frustums_, currentSmSettings, &cam, caster_filter::all, collection, stats);
    }

    auto& lightView = light_view_;
    auto& lightProj = light_proj_;
    auto& lightFrustums = light_frustums_;

    bool homogeneousDepth = gfx::is_homogeneous_depth();
    bool originBottomLeft = gfx::is_origin_bottom_left();

    float screenProj[16];
    float screenView[16];
    bx::mtxIdentity(screenView);

    bx::mtxOrtho(screenProj, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 100.0f, 0.0f, bgfx::getCaps()->homogeneousDepth);

    
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

    if(LightType::DirectionalLight != settings_.m_lightType)
    {
        /**
         * RENDERVIEW_SHADOWMAP_0_ID - Clear (point light), stencil mask, restore from the static copy.
         * RENDERVIEW_SHADOWMAP_1_ID..4 - The spot light's map, or the point light's tetrahedron faces.
         * RENDERVIEW_VBLUR_0_ID - Vertical blur.
         * RENDERVIEW_HBLUR_0_ID - Horizontal blur.
         */
        const bgfx::ViewId local_views[5] = {RENDERVIEW_SHADOWMAP_0_ID,
                                             RENDERVIEW_SHADOWMAP_1_ID,
                                             RENDERVIEW_SHADOWMAP_2_ID,
                                             RENDERVIEW_SHADOWMAP_3_ID,
                                             RENDERVIEW_SHADOWMAP_4_ID};
        set_local_light_views(local_views, map_target, map_x, map_y);

        bgfx::setViewRect(RENDERVIEW_VBLUR_0_ID, 0, 0, current_shadow_map_size_, current_shadow_map_size_);
        bgfx::setViewRect(RENDERVIEW_HBLUR_0_ID, 0, 0, current_shadow_map_size_, current_shadow_map_size_);
        bgfx::setViewTransform(RENDERVIEW_VBLUR_0_ID, screenView, screenProj);
        bgfx::setViewTransform(RENDERVIEW_HBLUR_0_ID, screenView, screenProj);
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
    const bool uses_stencil_mask = LightType::PointLight == settings_.m_lightType && settings_.m_stencilPack;
    uint8_t flags0 = (LightType::DirectionalLight == settings_.m_lightType)
                         ? 0
                         : BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL;
    // The restore writes every texel of the tile: only the stencil the mask starts from is cleared.
    if(uses_static_copy)
    {
        flags0 = uses_stencil_mask ? BGFX_CLEAR_STENCIL : 0;
    }

    // A spot light's map clears in its one draw view, which has the same target and rect: a view of its own for the
    // clear opens one more render pass per light. A point light clears in view 0, ahead of its stencil mask and faces,
    // and so does a restored map, ahead of the casters that move.
    const bgfx::ViewId clear_view = (LightType::SpotLight == settings_.m_lightType && !uses_static_copy)
                                        ? RENDERVIEW_SHADOWMAP_1_ID
                                        : RENDERVIEW_SHADOWMAP_0_ID;
    bgfx::setViewClear(clear_view,
                       flags0,
                       0xfefefefe // blur fails on completely white regions
                       ,
                       clear_values_.clear_depth,
                       clear_values_.clear_stencil);
    bgfx::touch(clear_view);

    const uint8_t flags1 =
        (LightType::DirectionalLight == settings_.m_lightType) ? BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH : 0;

    // Only the cascades clear here (the local lights cleared above): a view without a clear needs no touch, and an empty
    // view left untouched costs nothing, where a touched one still opens a render pass. A reused cascade keeps its
    // content: no clear.
    for(uint8_t ii = 0; ii < 4 && flags1 != 0; ++ii)
    {
        if((render_views & (1u << ii)) == 0u)
        {
            continue;
        }
        bgfx::setViewClear(RENDERVIEW_SHADOWMAP_1_ID + ii,
                           flags1,
                           0xfefefefe // blur fails on completely white regions
                           ,
                           clear_values_.clear_depth,
                           clear_values_.clear_stencil);
        bgfx::touch(RENDERVIEW_SHADOWMAP_1_ID + ii);
    }

    // Render.

    // uniforms_.submitConstUniforms();
    // uniforms_.submitPerFrameUniforms();

    const bool anythingDrawn = collection.anything_collected;
    // Craft shadow map.
    {
        if(uses_static_copy)
        {
            submit_static_copy_restore(RENDERVIEW_SHADOWMAP_0_ID, originBottomLeft);
        }
        // Craft stencil mask for point light shadow map packing.
        if(uses_stencil_mask)
        {
            submit_stencil_mask(RENDERVIEW_SHADOWMAP_0_ID);
        }

        submit_shadow_casters(render_views, RENDERVIEW_SHADOWMAP_1_ID, lightFrustums, currentSmSettings, collection, true, stats);
        // A restored map holds its static copy's casters.
        needs_clear_ = anythingDrawn || uses_static_copy;
        for(uint8_t ii = 0; ii < get_view_count(); ++ii)
        {
            if((render_views & (1u << ii)) != 0u)
            {
                view_keys_[ii] = view_keys[ii];
                view_key_valid_[ii] = true;
            }
        }
    }

    if(anythingDrawn)
    {
        PackDepth::Enum depthType = (SmImpl::VSM == settings_.m_smImpl) ? PackDepth::VSM : PackDepth::RGBA;
        bool bVsmOrEsm = (SmImpl::VSM == settings_.m_smImpl) || (SmImpl::ESM == settings_.m_smImpl);

        // Blur shadow map: the views rendered this time (a reused one is blurred already).
        if(bVsmOrEsm && currentSmSettings->m_doBlur && (render_views & 1u) != 0u)
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
        }
        if(bVsmOrEsm && currentSmSettings->m_doBlur)
        {
            if(LightType::DirectionalLight == settings_.m_lightType)
            {
                for(uint8_t ii = 1, jj = 2; ii < settings_.m_numSplits; ++ii, jj += 2)
                {
                    if((render_views & (1u << ii)) == 0u)
                    {
                        continue;
                    }
                    const bgfx::ViewId viewId = RENDERVIEW_VBLUR_0_ID + jj;

                    // Each blur begins its own program: the first cascade's blur may have been skipped.
                    bgfx::setTexture(4, shadow_map_[0], bgfx::getTexture(rt_shadow_map_[ii]));
                    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
                    screenSpaceQuad(originBottomLeft);
                    programs_.m_vBlur[depthType]->begin();
                    bgfx::submit(viewId, programs_.m_vBlur[depthType]->native_handle());
                    programs_.m_vBlur[depthType]->end();

                    bgfx::setTexture(4, shadow_map_[0], bgfx::getTexture(rt_blur_));
                    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
                    screenSpaceQuad(originBottomLeft);
                    programs_.m_hBlur[depthType]->begin();
                    bgfx::submit(viewId + 1, programs_.m_hBlur[depthType]->native_handle());
                    programs_.m_hBlur[depthType]->end();
                }
            }
        }
    }
}

auto shadowmap_generator::get_view_count() const -> uint8_t
{
    if(LightType::SpotLight == settings_.m_lightType)
    {
        return 1;
    }
    if(LightType::PointLight == settings_.m_lightType)
    {
        return 4;
    }
    return uint8_t(settings_.m_numSplits);
}

auto shadowmap_generator::make_shadow_reach(const ShadowMapSettings* currentSmSettings, const camera* cam) const
    -> shadow_reach
{
    // Directional lights only (see shadow_reach): a caster whose shadow cannot reach the view is dropped from every
    // cascade. Resolved once per pass over the casters - get_frustum() resolves lazily and the caster loops must not be
    // the one to trigger it.
    shadow_reach reach;
    if(LightType::DirectionalLight == settings_.m_lightType && cam != nullptr)
    {
        reach.view_frustum = &cam->get_frustum();
        reach.light_direction = math::vec3(directional_light_.m_position.m_x,
                                           directional_light_.m_position.m_y,
                                           directional_light_.m_position.m_z);
        reach.max_distance = currentSmSettings->m_far;
    }
    return reach;
}

auto shadowmap_generator::is_skinned_batching_enabled(const ShadowMapSettings* currentSmSettings) const -> bool
{
    const gpu_program* skinned_instanced_prog =
        resolve_skinned_instanced_pack_program(programs_, currentSmSettings->m_progPackSkinned);
    return batch_collector::is_skinned_mesh_batching_enabled() && skinned_instanced_prog != nullptr &&
           skinned_instanced_prog->is_valid();
}

auto shadowmap_generator::prepare_shadow_casters(const shadow_map_models_t& models,
                                                 const math::frustum lightFrustums[ShadowMapRenderTargets::Count],
                                                 const ShadowMapSettings* currentSmSettings,
                                                 const camera* cam) -> shadow_collection
{
    shadow_collection collection;
    const uint64_t batching_bits = uint64_t(batch_collector::is_static_mesh_batching_enabled()) |
                                   uint64_t(is_skinned_batching_enabled(currentSmSettings)) << 1u;
    compute_caster_facts(models, lightFrustums, make_shadow_reach(currentSmSettings, cam), cam, batching_bits, collection);
    if(can_reuse_all_views(collection.casters_key))
    {
        collection.reuse_all = true;
        mark_reused_casters(models, gfx::get_render_frame());
    }
    return collection;
}

void shadowmap_generator::compute_caster_facts(const shadow_map_models_t& models,
                                               const math::frustum lightFrustums[ShadowMapRenderTargets::Count],
                                               const shadow_reach& reach,
                                               const camera* cam,
                                               uint64_t batching_bits,
                                               shadow_collection& collection)
{
    const uint8_t drawNum = get_view_count();
    const bool nested_cascades = (LightType::DirectionalLight == settings_.m_lightType);
    const uint64_t frame = gfx::get_render_frame();
    uint64_t key = mix_key(batching_bits, models.size());
    uint64_t static_key = 0;
    for(uint8_t ii = 0; ii < drawNum; ++ii)
    {
        const uint64_t settings_key = compute_view_settings_key(ii);
        key = mix_key(key, settings_key);
        static_key = mix_key(static_key, settings_key);
    }
    // The reach test follows the camera. Unjittered: the jitter moves the frustum every frame by less than a pixel.
    if(reach.view_frustum != nullptr && cam != nullptr)
    {
        const math::mat4 view_projection = cam->get_view_projection_unjittered();
        key = mix_floats(key, math::value_ptr(view_projection), 16);
    }
    // Mixes @p value into the content hash of every view of @p view_mask.
    const auto mix_into_views = [&](uint8_t view_mask, uint64_t value)
    {
        for(uint8_t ii = 0; ii < drawNum; ++ii)
        {
            if((view_mask & (1u << ii)) != 0u)
            {
                collection.view_hashes[ii] = mix_key(collection.view_hashes[ii], value);
            }
        }
    };
    caster_facts_.resize(models.size());
    for(size_t caster_index = 0; caster_index < models.size(); ++caster_index)
    {
        const auto& element = models[caster_index];
        auto& facts = caster_facts_[caster_index];
        const entt::entity entity = element.entity.entity();
        // The list changed under this place: the mark of whatever held it does not apply. Marked rendered, as every
        // caster in reach was before it was last collected.
        if(facts.entity != entity)
        {
            facts.entity = entity;
            facts.is_marked = true;
        }
        // Frustum culling against the pose-aware world AABB cached in the list, before any component is looked up.
        // Bind-pose local bounds are never used - they don't track node/bone animation. This is the model-level reject
        // in front of the per-submesh tests, which fall back to "visible" while a model's proxy bounds are stale
        // (model_component::get_submit_extras); the world bounds are always valid. It also carries the nested-cascade
        // rule: the mask ENDS at the first cascade that fully contains the model, so the loops that walk its bits
        // need no break of their own.
        facts.view_mask = compute_shadow_view_mask(lightFrustums, drawNum, element.world_bounds, nested_cascades, reach);
        facts.lod_index = 0;
        facts.content_hash = 0;
        facts.is_mover = true;
        key = mix_key(key, facts.view_mask);
        if(facts.view_mask == 0u)
        {
            facts.is_marked = false;
            continue;
        }
        if(element.particles != nullptr)
        {
            // Particles move without their instances changing: a light that reaches them renders every frame.
            key = mix_key(key, frame);
            mix_into_views(facts.view_mask, frame);
            collection.has_movers = true;
            continue;
        }
        const auto& model_comp = element.entity.get<model_component>();
        const auto& model = model_comp.get_model();
        const uint64_t change_frame = model_comp.get_shadow_change_frame();
        facts.content_hash = mix_key(uint64_t(entt::to_integral(entity)), change_frame);
        if(model.is_valid())
        {
            // LOD is resolved HERE rather than carried in the caster list: the list is cached across frames
            // (deferred::refresh_shadow_casters) while the LOD follows the camera, and a caster the light rejected
            // never needs one.
            facts.lod_index =
                cam != nullptr ? model.compute_lod_index(element.world_bounds, *cam, ::unravel::rendering::pipeline::get_shadow_lod_bias()) : 0;
            // Never waits for a mesh still loading: the mesh arriving changes the hash.
            const auto lod_mesh = model.get_lod(facts.lod_index).try_get();
            facts.content_hash = mix_key(facts.content_hash, facts.lod_index);
            facts.content_hash = mix_key(facts.content_hash, reinterpret_cast<std::uintptr_t>(lod_mesh.get()));
        }
        key = mix_key(key, facts.content_hash);
        mix_into_views(facts.view_mask, facts.content_hash);
        facts.is_mover = frame < change_frame + MOVER_SETTLE_FRAMES;
        if(facts.is_mover)
        {
            collection.has_movers = true;
            continue;
        }
        static_key = mix_key(static_key, facts.view_mask);
        static_key = mix_key(static_key, facts.content_hash);
        ++collection.static_caster_count;
    }
    collection.casters_key = key;
    collection.static_key = static_key;
}

void shadowmap_generator::collect_shadow_casters(const shadow_map_models_t& models,
                                                 const math::frustum lightFrustums[ShadowMapRenderTargets::Count],
                                                 ShadowMapSettings* currentSmSettings,
                                                 const camera* cam,
                                                 caster_filter filter,
                                                 shadow_collection& collection,
                                                 ::unravel::rendering::pipeline_stats* stats)
{
    const uint8_t drawNum = get_view_count();

    // Resize and clear batch collectors for each cascade/view
    cascade_batch_collectors_.resize(drawNum);
    for (auto& collector : cascade_batch_collectors_)
    {
        collector.clear();
    }
    cascade_skinned_batch_collectors_.resize(drawNum);
    for(auto& collector : cascade_skinned_batch_collectors_)
    {
        collector.clear();
    }
    individual_casters_.clear();
    collection.anything_collected = false;
    collection.has_static_batches = false;
    collection.has_skinned_batches = false;

    const bool static_batching_enabled = batch_collector::is_static_mesh_batching_enabled();
    const bool skinned_batching_enabled = is_skinned_batching_enabled(currentSmSettings);
    auto& skinning_palettes = engine::context().get_cached<renderer>().get_skinning_palettes();
    // Mesh particles are instanced whatever the project's batching setting: they have no other path.
    bool has_particle_casters = false;

    // For CSM (directional lights) a caster fully inside a nearer cascade's light frustum
    // is not drawn into the farther (larger) cascades. This is exact ONLY because the
    // lighting shader samples the smallest cascade whose crop contains the receiver
    // (fs_pbr_lighting.sh, CalculateSurfaceShadow): a caster that shadows a receiver
    // shares its light-space xy, so whatever shadows a receiver of crop j lies in column
    // j and is drawn into map j. Selecting cascades by view distance instead would need
    // every caster in every cascade it touches. For point/spot lights, faces cover
    // different directions, so an object must be rendered to ALL faces where it is visible.
    const bool nested_cascades = (LightType::DirectionalLight == settings_.m_lightType);
    const shadow_reach reach = make_shadow_reach(currentSmSettings, cam);
    const uint64_t frame = gfx::get_render_frame();

    for(size_t caster_index = 0; caster_index < models.size(); ++caster_index)
    {
        const auto& element = models[caster_index];
        auto& facts = caster_facts_[caster_index];
        // Culled by compute_caster_facts (the view mask) and given its LOD there.
        const uint8_t view_mask = facts.view_mask;
        if(view_mask == 0u)
        {
            continue;
        }
        const bool is_taken = filter == caster_filter::all || facts.is_mover == (filter == caster_filter::movers_only);
        if(!is_taken)
        {
            // Drawn from the static copy: marked rendered as when it was collected into it.
            if(filter == caster_filter::movers_only && facts.is_marked)
            {
                element.entity.get<model_component>().set_last_render_frame(frame);
            }
            continue;
        }
        facts.is_marked = false;

        if(element.particles != nullptr)
        {
            const bool collected = element.particles->collect_mesh_shadow_instances(cascade_batch_collectors_, view_mask);
            if(collected)
            {
                // A shadow in view keeps renderer-based culling running the emitter, as it does for models.
                element.entity.get<particle_emitter_component>().set_last_render_frame(frame);
            }
            has_particle_casters = has_particle_casters || collected;
            collection.anything_collected = collection.anything_collected || collected;
            continue;
        }

        const auto& entity = element.entity;
        const auto& transform_comp = entity.get<transform_component>();
        auto& model_comp = entity.get<model_component>();
        const auto& world_transform = transform_comp.get_transform_global();
        const auto& submesh_transforms = model_comp.get_submesh_transforms();
        const auto& skinning_matrices = model_comp.get_skinning_transforms();

        const bool is_skinned = !skinning_matrices.empty();

        const auto& model = model_comp.get_model();
        if(!model.is_valid())
            continue;

        const uint32_t lod_index = facts.lod_index;

        // Skinned batching takes a skinned model's skinned submeshes; its other submeshes then batch like a static
        // model's (or stay individual when static batching is off).
        const bool batch_skinned = skinned_batching_enabled && is_skinned;
        const bool can_batch = static_batching_enabled && (!is_skinned || batch_skinned);

        const auto extras = model_comp.get_submit_extras(true);

        if(batch_skinned)
        {
            // The palettes the G-buffer and every other view of this frame share.
            const auto rows = skinning_palettes.acquire(&model_comp, skinning_matrices, nullptr);
            const bool collected = model.submit_for_skinned_shadow_batching_cascaded(cascade_skinned_batch_collectors_,
                                                                                     drawNum,
                                                                                     view_mask,
                                                                                     reach,
                                                                                     world_transform,
                                                                                     submesh_transforms,
                                                                                     rows.current,
                                                                                     lod_index,
                                                                                     lightFrustums,
                                                                                     nested_cascades,
                                                                                     extras);
            if(collected)
            {
                model_comp.set_last_render_frame(frame);
                facts.is_marked = true;
                collection.anything_collected = true;
                collection.has_skinned_batches = true;
                if(stats != nullptr)
                {
                    stats->drawn_skinned_models_for_shadows++;
                }
            }
        }

        if(can_batch)
        {
            // Collect into all cascades in one pass so the nested-cascade trick can be
            // applied at submesh granularity (a submesh fully inside a nearer cascade is
            // not collected into farther cascades).
            // The reach test repeats per submesh inside: a scene authored as ONE model with
            // many submeshes passes the model-level test trivially (its bounds contain the
            // camera), so that is the only granularity at which it can reject anything.
            const bool collected = model.submit_for_shadow_batching_cascaded(cascade_batch_collectors_,
                                                                      drawNum,
                                                                      view_mask,
                                                                      reach,
                                                                      world_transform,
                                                                      submesh_transforms,
                                                                      lod_index,
                                                                      0.0f,
                                                                      lightFrustums,
                                                                      nested_cascades,
                                                                      extras);
            if(collected)
            {
                model_comp.set_last_render_frame(frame);
                facts.is_marked = true;
                collection.anything_collected = true;
                if(stats != nullptr)
                {
                    stats->drawn_models_for_shadows++;
                }
            }

            continue;
        }

        // The individual path is drawn by submit_shadow_casters, into the views that render.
        model_comp.set_last_render_frame(frame);
        facts.is_marked = true;
        individual_casters_.push_back({&element, lod_index, view_mask, batch_skinned});
        collection.anything_collected = true;
    }

    collection.has_static_batches = static_batching_enabled || has_particle_casters;
    // The reach test keeps submeshes by the camera, which the casters' facts do not see: a cascade's content is also
    // what was collected into it.
    if(nested_cascades)
    {
        for(uint8_t ii = 0; ii < drawNum; ++ii)
        {
            collection.view_hashes[ii] =
                mix_key(collection.view_hashes[ii], cascade_batch_collectors_[ii].get_content_hash());
            collection.view_hashes[ii] =
                mix_key(collection.view_hashes[ii], cascade_skinned_batch_collectors_[ii].get_content_hash());
        }
    }
}

auto shadowmap_generator::can_reuse_all_views(uint64_t casters_key) const -> bool
{
    if(!is_map_caching_enabled() || !casters_key_valid_ || casters_key_ != casters_key)
    {
        return false;
    }
    const uint8_t view_count = get_view_count();
    return std::all_of(view_key_valid_, view_key_valid_ + view_count, [](bool is_valid) { return is_valid; });
}

void shadowmap_generator::mark_reused_casters(const shadow_map_models_t& models, uint64_t frame) const
{
    // The casters are the ones the last collection saw (the key holds every one of them, in order).
    for(size_t caster_index = 0; caster_index < models.size(); ++caster_index)
    {
        const auto& element = models[caster_index];
        if(caster_facts_[caster_index].is_marked && element.particles == nullptr)
        {
            element.entity.get<model_component>().set_last_render_frame(frame);
        }
    }
}

auto shadowmap_generator::compute_view_key(uint8_t view, const shadow_collection& collection) const -> uint64_t
{
    return mix_key(compute_view_settings_key(view), collection.view_hashes[view]);
}

auto shadowmap_generator::compute_view_settings_key(uint8_t view) const -> uint64_t
{
    uint64_t key = mix_key(get_cached_maps_epoch(), current_shadow_map_size_);
    key = mix_key(key,
                  uint64_t(settings_.m_lightType) | uint64_t(settings_.m_depthImpl) << 8u |
                      uint64_t(settings_.m_smImpl) << 16u | uint64_t(settings_.m_stencilPack) << 24u);
    // The matrices the view renders with (generate_shadowmaps' setViewTransform).
    const bool is_directional = LightType::DirectionalLight == settings_.m_lightType;
    const bool is_vertical_face =
        LightType::PointLight == settings_.m_lightType && settings_.m_stencilPack && view >= TetrahedronFaces::Blue;
    const float* view_matrix = light_view_[is_directional ? 0 : view];
    const float* proj_matrix =
        is_directional ? light_proj_[view] : light_proj_[is_vertical_face ? ProjType::Vertical : ProjType::Horizontal];
    key = mix_floats(key, view_matrix, 16);
    key = mix_floats(key, proj_matrix, 16);
    if(uses_atlas_)
    {
        key = mix_key(key, atlas_placement_);
    }
    return key;
}

auto shadowmap_generator::select_views_to_render(const shadow_collection& collection,
                                                 uint64_t keys[ShadowMapRenderTargets::Count]) const -> uint8_t
{
    const uint8_t view_count = get_view_count();
    const bool is_caching = is_map_caching_enabled();
    uint8_t views = 0;
    for(uint8_t ii = 0; ii < view_count; ++ii)
    {
        keys[ii] = compute_view_key(ii, collection);
        if(!is_caching || !view_key_valid_[ii] || view_keys_[ii] != keys[ii])
        {
            views |= uint8_t(1u << ii);
        }
    }
    // A local light's faces share one clear, and the stencil-packed faces overlap: they render together.
    if(LightType::DirectionalLight != settings_.m_lightType && views != 0u)
    {
        views = uint8_t((1u << view_count) - 1u);
    }
    return views;
}

auto shadowmap_generator::can_use_static_copy(const shadow_collection& collection) const -> bool
{
    // The restore rebuilds the atlas colour from the device depth, which only the projected depth (InvZ) stores as is.
    return is_map_caching_enabled() && uses_atlas_ && settings_.m_depthImpl == DepthImpl::InvZ && collection.has_movers &&
           collection.static_caster_count >= STATIC_COPY_MIN_CASTERS;
}

void shadowmap_generator::refresh_static_copy(const shadow_map_models_t& models,
                                              ShadowMapSettings* currentSmSettings,
                                              const camera& cam,
                                              shadow_collection& collection,
                                              ::unravel::rendering::pipeline_stats* stats)
{
    auto& atlas = shadow_atlas::get();
    const bgfx::FrameBufferHandle target = atlas.get_static_frame_buffer();
    const uint64_t generation = atlas.get_static_generation();
    if(static_copy_valid_ && static_copy_key_ == collection.static_key && static_copy_generation_ == generation)
    {
        return;
    }

    gfx::render_pass static_pass_0("Shadowmap Static Copy 0");
    gfx::render_pass static_pass_1("Shadowmap Static Copy 1");
    gfx::render_pass static_pass_2("Shadowmap Static Copy 2");
    gfx::render_pass static_pass_3("Shadowmap Static Copy 3");
    gfx::render_pass static_pass_4("Shadowmap Static Copy 4");
    const bgfx::ViewId views[5] = {static_pass_0.id, static_pass_1.id, static_pass_2.id, static_pass_3.id, static_pass_4.id};
    set_local_light_views(views, target, atlas_tile_->x, atlas_tile_->y);

    // Depth and stencil only. A spot light clears in its one draw view, a point light ahead of its mask and faces.
    const bgfx::ViewId clear_view = (LightType::SpotLight == settings_.m_lightType) ? views[1] : views[0];
    bgfx::setViewClear(clear_view, BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL, 0, clear_values_.clear_depth, clear_values_.clear_stencil);
    bgfx::touch(clear_view);
    if(LightType::PointLight == settings_.m_lightType && settings_.m_stencilPack)
    {
        submit_stencil_mask(views[0]);
    }

    collect_shadow_casters(models, light_frustums_, currentSmSettings, &cam, caster_filter::static_only, collection, stats);
    const uint8_t all_views = uint8_t((1u << get_view_count()) - 1u);
    submit_shadow_casters(all_views, views[1], light_frustums_, currentSmSettings, collection, false, stats);

    static_copy_key_ = collection.static_key;
    static_copy_generation_ = generation;
    static_copy_valid_ = true;
}

void shadowmap_generator::set_local_light_views(const bgfx::ViewId views[5],
                                                bgfx::FrameBufferHandle target,
                                                uint16_t map_x,
                                                uint16_t map_y) const
{
    float screen_view[16];
    float screen_proj[16];
    bx::mtxIdentity(screen_view);
    bx::mtxOrtho(screen_proj, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 100.0f, 0.0f, bgfx::getCaps()->homogeneousDepth);

    const uint16_t f = current_shadow_map_size_;     // full size
    const uint16_t h = current_shadow_map_size_ / 2; // half size
    bgfx::setViewRect(views[0], map_x, map_y, f, f);
    bgfx::setViewTransform(views[0], screen_view, screen_proj);
    bgfx::setViewFrameBuffer(views[0], target);
    if(LightType::SpotLight == settings_.m_lightType)
    {
        bgfx::setViewRect(views[1], map_x, map_y, f, f);
        bgfx::setViewTransform(views[1], light_view_[0], light_proj_[ProjType::Horizontal]);
        bgfx::setViewFrameBuffer(views[1], target);
        return;
    }
    if(settings_.m_stencilPack)
    {
        bgfx::setViewRect(views[1], map_x, map_y, f, h);
        bgfx::setViewRect(views[2], map_x, uint16_t(map_y + h), f, h);
        bgfx::setViewRect(views[3], map_x, map_y, h, f);
        bgfx::setViewRect(views[4], uint16_t(map_x + h), map_y, h, f);
    }
    else
    {
        bgfx::setViewRect(views[1], map_x, map_y, h, h);
        bgfx::setViewRect(views[2], uint16_t(map_x + h), map_y, h, h);
        bgfx::setViewRect(views[3], map_x, uint16_t(map_y + h), h, h);
        bgfx::setViewRect(views[4], uint16_t(map_x + h), uint16_t(map_y + h), h, h);
    }
    // The stencil-packed vertical faces (blue, red) use their own projection.
    const ProjType::Enum vertical = settings_.m_stencilPack ? ProjType::Vertical : ProjType::Horizontal;
    bgfx::setViewTransform(views[1], light_view_[TetrahedronFaces::Green], light_proj_[ProjType::Horizontal]);
    bgfx::setViewTransform(views[2], light_view_[TetrahedronFaces::Yellow], light_proj_[ProjType::Horizontal]);
    bgfx::setViewTransform(views[3], light_view_[TetrahedronFaces::Blue], light_proj_[vertical]);
    bgfx::setViewTransform(views[4], light_view_[TetrahedronFaces::Red], light_proj_[vertical]);
    for(uint8_t ii = 1; ii < 5; ++ii)
    {
        bgfx::setViewFrameBuffer(views[ii], target);
    }
}

void shadowmap_generator::submit_stencil_mask(bgfx::ViewId view) const
{
    if(6 != bgfx::getAvailTransientVertexBuffer(6, PosVertex::get_layout()))
    {
        return;
    }
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
    bgfx::submit(view, programs_.m_black->native_handle());
    programs_.m_black->end();
}

void shadowmap_generator::submit_static_copy_restore(bgfx::ViewId view, bool origin_bottom_left) const
{
    // The atlas colour stores ndc z * 0.5 + 0.5; the device depth is ndc z where depth runs 0..1, and already
    // ndc z * 0.5 + 0.5 where it runs -1..1.
    const bool is_homogeneous = gfx::is_homogeneous_depth();
    const float params[4] = {is_homogeneous ? 1.0f : 0.5f, is_homogeneous ? 0.0f : 0.5f, 0.0f, 0.0f};
    bgfx::setUniform(restore_params_, params);
    bgfx::setTexture(0,
                     static_depth_sampler_,
                     shadow_atlas::get().get_static_depth_texture(),
                     BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_ALWAYS);
    screenSpaceQuad(origin_bottom_left);
    programs_.m_restore->begin();
    bgfx::submit(view, programs_.m_restore->native_handle());
    programs_.m_restore->end();
}

auto shadowmap_generator::get_face_render_state(uint8_t face, bool writes_color) const -> RenderState
{
    uint8_t index = RenderState::ShadowMap_PackDepth;
    if(LightType::PointLight == settings_.m_lightType && settings_.m_stencilPack)
    {
        index = uint8_t((face < 2) ? RenderState::ShadowMap_PackDepthHoriz : RenderState::ShadowMap_PackDepthVert);
    }
    RenderState state = render_states[index];
    if(!writes_color)
    {
        state.m_state &= ~(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    }
    return state;
}

void shadowmap_generator::submit_shadow_casters(uint8_t view_mask,
                                                bgfx::ViewId shadowmap_1_id,
                                                const math::frustum lightFrustums[ShadowMapRenderTargets::Count],
                                                ShadowMapSettings* currentSmSettings,
                                                const shadow_collection& collection,
                                                bool writes_color,
                                                ::unravel::rendering::pipeline_stats* stats)
{
    for(const auto& caster : individual_casters_)
    {
        const uint8_t caster_views = caster.view_mask & view_mask;
        if(caster_views != 0u)
        {
            submit_individual_caster(caster, caster_views, shadowmap_1_id, lightFrustums, currentSmSettings, writes_color, stats);
        }
    }

    // Submit batched geometry for each cascade
    if(!collection.has_static_batches && !collection.has_skinned_batches)
    {
        return;
    }
    const uint8_t drawNum = get_view_count();
    for(uint8_t ii = 0; ii < drawNum; ++ii)
    {
        if((view_mask & (1u << ii)) == 0u)
        {
            continue;
        }
        const bgfx::ViewId viewId = shadowmap_1_id + ii;
        const RenderState renderState = get_face_render_state(ii, writes_color);
        if(collection.has_static_batches)
        {
            submit_batched_shadow_geometry_cascade(cascade_batch_collectors_[ii], viewId, currentSmSettings, renderState, false, stats);
        }
        if(collection.has_skinned_batches)
        {
            submit_batched_shadow_geometry_cascade(cascade_skinned_batch_collectors_[ii], viewId, currentSmSettings, renderState, true, stats);
        }
    }
}

void shadowmap_generator::submit_individual_caster(const individual_caster& caster,
                                                   uint8_t view_mask,
                                                   bgfx::ViewId shadowmap_1_id,
                                                   const math::frustum lightFrustums[ShadowMapRenderTargets::Count],
                                                   ShadowMapSettings* currentSmSettings,
                                                   bool writes_color,
                                                   ::unravel::rendering::pipeline_stats* stats)
{
    const auto& entity = caster.element->entity;
    const auto& transform_comp = entity.get<transform_component>();
    const auto& model_comp = entity.get<model_component>();
    const auto& world_transform = transform_comp.get_transform_global();
    const auto& submesh_transforms = model_comp.get_submesh_transforms();
    const auto& bone_transforms = model_comp.get_bone_transforms();
    const auto& skinning_matrices = model_comp.get_skinning_transforms();
    const auto& model = model_comp.get_model();
    const auto extras = model_comp.get_submit_extras(true);

    // The individual path (skinned casters) keeps the MODEL-level reach test only: a
    // skinned model is one character's worth of submeshes around one skeleton, so its
    // bounds are already the granularity the sweep can work at.
    bool counted_static_model_for_shadows = false;
    bool counted_skinned_model_for_shadows = false;
    for(uint8_t ii = 0; ii < get_view_count(); ++ii)
    {
        if((view_mask & (1u << ii)) == 0u)
        {
            continue;
        }

        const bgfx::ViewId viewId = shadowmap_1_id + ii;
        const RenderState _renderState = get_face_render_state(ii, writes_color);

        // Render skinned models individually (original behavior)
        model::submit_callbacks callbacks;
        callbacks.setup_begin = [&](const model::submit_callbacks::params& submit_params)
        {
            if(stats != nullptr)
            {
                if(submit_params.skinned)
                {
                    if(!counted_skinned_model_for_shadows)
                    {
                        stats->drawn_skinned_models_for_shadows++;
                        counted_skinned_model_for_shadows = true;
                    }
                }
                else if(!counted_static_model_for_shadows)
                {
                    stats->drawn_models_for_shadows++;
                    counted_static_model_for_shadows = true;
                }
            }
        };
        callbacks.setup_params_per_instance = [&](const model::submit_callbacks::params& submit_params)
        {
            uniforms_.submitPerDrawUniforms();
        };
        callbacks.setup_params_per_submesh =
            [&](const model::submit_callbacks::params& submit_params, const material& mat)
        {
            if(mat.is<pbr_material>() && !static_cast<const pbr_material&>(mat).casts_shadow())
            {
                return;
            }

            if(stats != nullptr)
            {
                if(submit_params.skinned)
                {
                    stats->drawn_skinned_submeshes_for_shadows++;
                }
                else
                {
                    stats->drawn_submeshes_for_shadows++;
                }
            }

            const bool uses_cutout =
                mat.is<pbr_material>() && static_cast<const pbr_material&>(mat).uses_alpha_cutout();
            gpu_program* opaque_prog =
                submit_params.skinned ? currentSmSettings->m_progPackSkinned : currentSmSettings->m_progPack;
            gpu_program* prog =
                uses_cutout ? resolve_cutout_pack_program(programs_, opaque_prog) : opaque_prog;
            if(!prog)
            {
                return;
            }

            prog->begin();

            if(uses_cutout)
            {
                uniforms_.submitShadowCutoutState(static_cast<const pbr_material&>(mat).make_shadow_cutout_state());
            }

            const uint64_t draw_state = apply_shadow_cull(_renderState.m_state, mat.get_cull_type());
            bgfx::setStencil(_renderState.m_fstencil, _renderState.m_bstencil);
            bgfx::setState(draw_state, _renderState.m_blendFactorRgba);

            bgfx::submit(viewId,
                         prog->native_handle(),
                         0,
                         submit_params.preserve_state ? BGFX_DISCARD_NONE : BGFX_DISCARD_ALL);
            prog->end();
        };
        callbacks.setup_end = [&](const model::submit_callbacks::params& submit_params)
        {
        };

        // With skinned batching on, only the model's other submeshes are left: no palettes = no skinned submeshes.
        model.submit(world_transform,
                     submesh_transforms,
                     bone_transforms,
                     caster.skinned_batched ? model::get_empty_skinning() : skinning_matrices,
                     caster.lod_index,
                     callbacks,
                     &lightFrustums[ii],
                     nullptr,
                     extras);
    }
}

void shadowmap_generator::submit_batched_shadow_geometry_cascade(shadow_batch_collector& collector,
                                                                bgfx::ViewId viewId,
                                                                ShadowMapSettings* currentSmSettings,
                                                                const RenderState& renderState,
                                                                bool skinned,
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

    gpu_program* const instanced_prog =
        skinned ? resolve_skinned_instanced_pack_program(programs_, currentSmSettings->m_progPackSkinned)
                : currentSmSettings->m_progPackInstanced;
    const auto instance_data_size = static_cast<uint16_t>(skinned ? skinned_instance_vertex_data::packed_size()
                                                                  : instance_vertex_data::packed_size());
    auto* skinning_palettes = skinned ? &engine::context().get_cached<renderer>().get_skinning_palettes() : nullptr;

    // Submit each batch (opaque and cutout may use different programs)
    for (const auto* batch : prepared_batches)
    {
        if (!batch->is_valid() || batch->instances.empty())
        {
            continue;
        }

        const auto instance_count = static_cast<uint32_t>(batch->instances.size());

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

        const bool uses_cutout = batch->key.uses_alpha_cutout();
        gpu_program* prog = uses_cutout ? resolve_cutout_pack_program(programs_, instanced_prog) : instanced_prog;
        if(!prog)
        {
            continue;
        }

        prog->begin();

        // bgfx lowers num to what the frame's transient buffer still holds; draw only those.
        bgfx::InstanceDataBuffer instance_buffer;
        bgfx::allocInstanceDataBuffer(&instance_buffer, instance_count, instance_data_size);
        if (!instance_buffer.data || instance_buffer.num == 0)
        {
            prog->end();
            continue;
        }

        if(stats != nullptr)
        {
            (skinned ? stats->drawn_skinned_submeshes_for_shadows : stats->drawn_submeshes_for_shadows) +=
                instance_buffer.num;
        }

        if(skinned)
        {
            auto* buffer_data = reinterpret_cast<skinned_instance_vertex_data*>(instance_buffer.data);
            for(uint32_t i = 0; i < instance_buffer.num; ++i)
            {
                buffer_data[i] = skinned_instance_vertex_data(batch->instances[i]);
            }
            skinning_palettes->bind();
        }
        else
        {
            auto* buffer_data = reinterpret_cast<instance_vertex_data*>(instance_buffer.data);
            for(uint32_t i = 0; i < instance_buffer.num; ++i)
            {
                buffer_data[i] = instance_vertex_data(batch->instances[i]);
            }
        }

        bgfx::setInstanceDataBuffer(&instance_buffer);
        mesh_ptr->bind_render_buffers_for_submesh(submesh, lod_index);

        uniforms_.submitPerDrawUniforms();
        if(uses_cutout && batch->key.cutout.has_value())
        {
            uniforms_.submitShadowCutoutState(*batch->key.cutout);
        }

        const uint64_t draw_state = apply_shadow_cull(renderState.m_state, batch->key.cull);
        bgfx::setStencil(renderState.m_fstencil, renderState.m_bstencil);
        bgfx::setState(draw_state, renderState.m_blendFactorRgba);

        bgfx::submit(viewId, prog->native_handle(), 0, BGFX_DISCARD_ALL);
        prog->end();
    }

    // Update statistics
    if(stats != nullptr)
    {
        const auto& batch_stats = collector.get_stats();
        stats->add_batch_stats(batch_stats);
    }
    
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
    m_restore      = loadProgram("vs_shadowmaps_unpackdepth",   "fs_shadowmaps_restore");

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

    // Pack depth cutout (alpha test — matches lit material clip semantics).
    m_packDepthCutout[DepthImpl::InvZ][PackDepth::RGBA] = loadProgram("packdepth/vs_shadowmaps_packdepth_cutout", "packdepth/fs_shadowmaps_packdepth_cutout");
    m_packDepthCutout[DepthImpl::InvZ][PackDepth::VSM]  = loadProgram("packdepth/vs_shadowmaps_packdepth_cutout", "packdepth/fs_shadowmaps_packdepth_vsm_cutout");

    m_packDepthCutout[DepthImpl::Linear][PackDepth::RGBA] = loadProgram("packdepth/vs_shadowmaps_packdepth_linear_cutout", "packdepth/fs_shadowmaps_packdepth_linear_cutout");
    m_packDepthCutout[DepthImpl::Linear][PackDepth::VSM]  = loadProgram("packdepth/vs_shadowmaps_packdepth_linear_cutout", "packdepth/fs_shadowmaps_packdepth_vsm_linear_cutout");

    m_packDepthSkinnedCutout[DepthImpl::InvZ][PackDepth::RGBA] = loadProgram("packdepth/vs_shadowmaps_packdepth_skinned_cutout", "packdepth/fs_shadowmaps_packdepth_cutout");
    m_packDepthSkinnedCutout[DepthImpl::InvZ][PackDepth::VSM]  = loadProgram("packdepth/vs_shadowmaps_packdepth_skinned_cutout", "packdepth/fs_shadowmaps_packdepth_vsm_cutout");

    m_packDepthSkinnedCutout[DepthImpl::Linear][PackDepth::RGBA] = loadProgram("packdepth/vs_shadowmaps_packdepth_linear_skinned_cutout", "packdepth/fs_shadowmaps_packdepth_linear_cutout");
    m_packDepthSkinnedCutout[DepthImpl::Linear][PackDepth::VSM]  = loadProgram("packdepth/vs_shadowmaps_packdepth_linear_skinned_cutout", "packdepth/fs_shadowmaps_packdepth_vsm_linear_cutout");

    m_packDepthInstancedCutout[DepthImpl::InvZ][PackDepth::RGBA] = loadProgram("packdepth/vs_shadowmaps_packdepth_instanced_cutout", "packdepth/fs_shadowmaps_packdepth_cutout");
    m_packDepthInstancedCutout[DepthImpl::InvZ][PackDepth::VSM]  = loadProgram("packdepth/vs_shadowmaps_packdepth_instanced_cutout", "packdepth/fs_shadowmaps_packdepth_vsm_cutout");

    m_packDepthInstancedCutout[DepthImpl::Linear][PackDepth::RGBA] = loadProgram("packdepth/vs_shadowmaps_packdepth_linear_instanced_cutout", "packdepth/fs_shadowmaps_packdepth_linear_cutout");
    m_packDepthInstancedCutout[DepthImpl::Linear][PackDepth::VSM]  = loadProgram("packdepth/vs_shadowmaps_packdepth_linear_instanced_cutout", "packdepth/fs_shadowmaps_packdepth_vsm_linear_cutout");

    // Pack depth instanced skinned (skinned mesh batching; bones from the skinning palette buffer).
    m_packDepthSkinnedInstanced[DepthImpl::InvZ][PackDepth::RGBA] = loadProgram("packdepth/vs_shadowmaps_packdepth_skinned_instanced", "packdepth/fs_shadowmaps_packdepth");
    m_packDepthSkinnedInstanced[DepthImpl::InvZ][PackDepth::VSM]  = loadProgram("packdepth/vs_shadowmaps_packdepth_skinned_instanced", "packdepth/fs_shadowmaps_packdepth_vsm");

    m_packDepthSkinnedInstanced[DepthImpl::Linear][PackDepth::RGBA] = loadProgram("packdepth/vs_shadowmaps_packdepth_linear_skinned_instanced", "packdepth/fs_shadowmaps_packdepth_linear");
    m_packDepthSkinnedInstanced[DepthImpl::Linear][PackDepth::VSM]  = loadProgram("packdepth/vs_shadowmaps_packdepth_linear_skinned_instanced", "packdepth/fs_shadowmaps_packdepth_vsm_linear");

    m_packDepthSkinnedInstancedCutout[DepthImpl::InvZ][PackDepth::RGBA] = loadProgram("packdepth/vs_shadowmaps_packdepth_skinned_instanced_cutout", "packdepth/fs_shadowmaps_packdepth_cutout");
    m_packDepthSkinnedInstancedCutout[DepthImpl::InvZ][PackDepth::VSM]  = loadProgram("packdepth/vs_shadowmaps_packdepth_skinned_instanced_cutout", "packdepth/fs_shadowmaps_packdepth_vsm_cutout");

    m_packDepthSkinnedInstancedCutout[DepthImpl::Linear][PackDepth::RGBA] = loadProgram("packdepth/vs_shadowmaps_packdepth_linear_skinned_instanced_cutout", "packdepth/fs_shadowmaps_packdepth_linear_cutout");
    m_packDepthSkinnedInstancedCutout[DepthImpl::Linear][PackDepth::VSM]  = loadProgram("packdepth/vs_shadowmaps_packdepth_linear_skinned_instanced_cutout", "packdepth/fs_shadowmaps_packdepth_vsm_linear_cutout");
}

}
} // namespace unravel
