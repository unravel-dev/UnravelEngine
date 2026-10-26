#pragma once
#include "glm/gtc/type_ptr.hpp"
#include <engine/engine_export.h>

#include <engine/ecs/ecs.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/light.h>
#include <engine/rendering/batch_collector.h>
#include <engine/rendering/model.h>
#include <engine/rendering/pipeline/pipeline.h>
#include <engine/rendering/shadow_atlas.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <engine/ecs/ecs.h>
#include <engine/rendering/gpu_program.h>
#include <graphics/graphics.h>
#include <hpp/small_vector.hpp>

namespace unravel
{
class particle_emitter_component;

namespace shadow
{
struct LightType
{
    enum Enum
    {
        SpotLight,
        PointLight,
        DirectionalLight,

        Count
    };
};

struct DepthImpl
{
    enum Enum
    {
        InvZ,
        Linear,

        Count
    };
};

struct PackDepth
{
    enum Enum
    {
        RGBA,
        VSM,

        Count
    };
};

struct SmImpl
{
    enum Enum
    {
        Hard,
        PCF,
        PCSS,
        VSM,
        ESM,

        Count
    };
};

struct SmType
{
    enum Enum
    {
        Single,
        Omni,
        Cascade,

        Count
    };
};

struct TetrahedronFaces
{
    enum Enum
    {
        Green,
        Yellow,
        Blue,
        Red,

        Count
    };
};

struct ProjType
{
    enum Enum
    {
        Horizontal,
        Vertical,

        Count
    };
};

/**
 * @brief Perspective projection of a spot light or of one point light face, in the two forms
 * the generator needs.
 *
 * @c render_proj is what the shadow pass renders with. For linear depth its z rows are divided
 * by the far plane (the pack shaders then store view depth / far without a per-pixel divide),
 * which stops it being a projection a frustum can be extracted from: the far plane test
 * degenerates and the light's range no longer culls anything. @c cull_proj is the same
 * projection as built, and @c homogeneous_depth the depth convention it was built with - the
 * pair frustum::update needs, whatever the backend or the depth mode.
 */
struct light_projection
{
    float render_proj[16];
    float cull_proj[16];
    bool homogeneous_depth{};
};

/**
 * @brief Builds a light_projection.
 * @param fov_y_degrees Vertical field of view in degrees.
 * @param homogeneous_depth Depth convention to build with ([-1, 1] when true, [0, 1] otherwise).
 * @param is_linear_depth True for sm_depth::linear (rescales render_proj only).
 */
auto make_light_projection(float fov_y_degrees,
                           float aspect,
                           float near_plane,
                           float far_plane,
                           bool homogeneous_depth,
                           bool is_linear_depth) -> light_projection;

struct ShadowMapRenderTargets
{
    enum Enum
    {
        First,
        Second,
        Third,
        Fourth,

        Count
    };
};

struct PosNormalTexcoordVertex
{
    float m_x;
    float m_y;
    float m_z;
    uint32_t m_normal;
    float m_u;
    float m_v;
};

struct Light
{
    union Position
    {
        struct
        {
            float m_x;
            float m_y;
            float m_z;
            float m_w;
        };

        float m_v[4];
    };

    union SpotDirectionInner
    {
        struct
        {
            float m_x;
            float m_y;
            float m_z;
            float m_inner;
        };

        float m_v[4];
    };

    Position m_position;
    SpotDirectionInner m_spotDirectionInner;
};

struct Uniforms
{
    void init()
    {
        m_ambientPass = 1.0f;
        m_lightingPass = 1.0f;

        m_shadowMapBias = 0.003f;
        m_shadowMapOffset = 0.0f;
        m_shadowMapParam0 = 0.5;
        m_shadowMapParam1 = 1.0;
        m_depthValuePow = 1.0f;
        m_showSmCoverage = 1.0f;
        m_shadowMapTexelSize = 1.0f / 512.0f;

        m_csmFarDistances[0] = 30.0f;
        m_csmFarDistances[1] = 90.0f;
        m_csmFarDistances[2] = 180.0f;
        m_csmFarDistances[3] = 1000.0f;

        m_tetraNormalGreen[0] = 0.0f;
        m_tetraNormalGreen[1] = -0.57735026f;
        m_tetraNormalGreen[2] = 0.81649661f;

        m_tetraNormalYellow[0] = 0.0f;
        m_tetraNormalYellow[1] = -0.57735026f;
        m_tetraNormalYellow[2] = -0.81649661f;

        m_tetraNormalBlue[0] = -0.81649661f;
        m_tetraNormalBlue[1] = 0.57735026f;
        m_tetraNormalBlue[2] = 0.0f;

        m_tetraNormalRed[0] = 0.81649661f;
        m_tetraNormalRed[1] = 0.57735026f;
        m_tetraNormalRed[2] = 0.0f;

        m_XNum = 2.0f;
        m_YNum = 2.0f;
        m_XOffset = 10.0f / 512.0f;
        m_YOffset = 10.0f / 512.0f;

        for(int ii = 0; ii < 4; ++ii)
        {
            m_shadowBiasParams[ii] = 0.0f;
            m_csmTexelWorld[ii] = 0.0f;
            m_perspTexelParams[ii] = 0.0f;
            m_shadowAxisU[ii] = 0.0f;
            m_shadowAxisV[ii] = 0.0f;
        }

        u_params0 = bgfx::createUniform("u_params0", bgfx::UniformType::Vec4);
        u_params1 = bgfx::createUniform("u_params1", bgfx::UniformType::Vec4);
        u_params2 = bgfx::createUniform("u_params2", bgfx::UniformType::Vec4);
        u_color = bgfx::createUniform("u_color", bgfx::UniformType::Vec4);
        u_smSamplingParams = bgfx::createUniform("u_smSamplingParams", bgfx::UniformType::Vec4);
        u_csmFarDistances = bgfx::createUniform("u_csmFarDistances", bgfx::UniformType::Vec4);
        u_shadowBiasParams = bgfx::createUniform("u_shadowBiasParams", bgfx::UniformType::Vec4);
        u_csmTexelWorld = bgfx::createUniform("u_csmTexelWorld", bgfx::UniformType::Vec4);
        u_perspTexelParams = bgfx::createUniform("u_perspTexelParams", bgfx::UniformType::Vec4);
        u_shadowAxisU = bgfx::createUniform("u_shadowAxisU", bgfx::UniformType::Vec4);
        u_shadowAxisV = bgfx::createUniform("u_shadowAxisV", bgfx::UniformType::Vec4);
        u_lightMtx = bgfx::createUniform("u_lightMtx", bgfx::UniformType::Mat4);

        u_tetraNormalGreen = bgfx::createUniform("u_tetraNormalGreen", bgfx::UniformType::Vec4);
        u_tetraNormalYellow = bgfx::createUniform("u_tetraNormalYellow", bgfx::UniformType::Vec4);
        u_tetraNormalBlue = bgfx::createUniform("u_tetraNormalBlue", bgfx::UniformType::Vec4);
        u_tetraNormalRed = bgfx::createUniform("u_tetraNormalRed", bgfx::UniformType::Vec4);

        u_shadowMapMtx0 = bgfx::createUniform("u_shadowMapMtx0", bgfx::UniformType::Mat4);
        u_shadowMapMtx1 = bgfx::createUniform("u_shadowMapMtx1", bgfx::UniformType::Mat4);
        u_shadowMapMtx2 = bgfx::createUniform("u_shadowMapMtx2", bgfx::UniformType::Mat4);
        u_shadowMapMtx3 = bgfx::createUniform("u_shadowMapMtx3", bgfx::UniformType::Mat4);

        u_shadowCutoutBaseColor = bgfx::createUniform("u_base_color", bgfx::UniformType::Vec4);
        u_shadowCutoutSurfaceData = bgfx::createUniform("u_surface_data", bgfx::UniformType::Vec4);
        u_shadowCutoutTiling = bgfx::createUniform("u_tiling", bgfx::UniformType::Vec4);
        s_shadowCutoutColor = bgfx::createUniform("s_tex_color", bgfx::UniformType::Sampler);
    }

    void setPtrs(Light* _lightPtr,
                 float* _colorPtr,
                 float* _lightMtxPtr,
                 float* _shadowMapMtx0,
                 float* _shadowMapMtx1,
                 float* _shadowMapMtx2,
                 float* _shadowMapMtx3)
    {
        m_lightMtxPtr = _lightMtxPtr;
        m_colorPtr = _colorPtr;
        m_lightPtr = _lightPtr;

        m_shadowMapMtx0 = _shadowMapMtx0;
        m_shadowMapMtx1 = _shadowMapMtx1;
        m_shadowMapMtx2 = _shadowMapMtx2;
        m_shadowMapMtx3 = _shadowMapMtx3;
    }

    // Call this once at initialization (frame uniforms persist until changed).
    void submitConstUniforms() const
    {
        bgfx::setUniform(u_tetraNormalGreen, m_tetraNormalGreen);
        bgfx::setUniform(u_tetraNormalYellow, m_tetraNormalYellow);
        bgfx::setUniform(u_tetraNormalBlue, m_tetraNormalBlue);
        bgfx::setUniform(u_tetraNormalRed, m_tetraNormalRed);
    }

    // Call this once per frame.
    void submitPerFrameUniforms() const
    {
        bgfx::setUniform(u_params1, m_params1);
        bgfx::setUniform(u_params2, m_params2);
        bgfx::setUniform(u_smSamplingParams, m_paramsBlur);
        bgfx::setUniform(u_csmFarDistances, m_csmFarDistances);
        bgfx::setUniform(u_shadowBiasParams, m_shadowBiasParams);
        bgfx::setUniform(u_csmTexelWorld, m_csmTexelWorld);
        bgfx::setUniform(u_perspTexelParams, m_perspTexelParams);
        bgfx::setUniform(u_shadowAxisU, m_shadowAxisU);
        bgfx::setUniform(u_shadowAxisV, m_shadowAxisV);
    }

    // Call this before each draw call.
    void submitPerDrawUniforms() const
    {
        submitConstUniforms();
        submitPerFrameUniforms();
        bgfx::setUniform(u_shadowMapMtx0, m_shadowMapMtx0);
        bgfx::setUniform(u_shadowMapMtx1, m_shadowMapMtx1);
        bgfx::setUniform(u_shadowMapMtx2, m_shadowMapMtx2);
        bgfx::setUniform(u_shadowMapMtx3, m_shadowMapMtx3);

        bgfx::setUniform(u_params0, m_params0);
        bgfx::setUniform(u_lightMtx, m_lightMtxPtr);
        bgfx::setUniform(u_color, m_colorPtr);
    }

    void submitShadowCutoutState(const shadow_cutout_state& cutout) const
    {
        if(cutout.color_map)
        {
            bgfx::setTexture(0, s_shadowCutoutColor, cutout.color_map->native_handle());
        }
        bgfx::setUniform(u_shadowCutoutBaseColor, math::value_ptr(cutout.base_color.value));
        const math::vec4 surface_data{0.0f, 0.0f, 0.0f, cutout.alpha_test_value};
        bgfx::setUniform(u_shadowCutoutSurfaceData, math::value_ptr(surface_data));
        const math::vec4 tiling{cutout.tiling.x, cutout.tiling.y, 0.0f, 0.0f};
        bgfx::setUniform(u_shadowCutoutTiling, math::value_ptr(tiling));
    }

    void destroy()
    {
        auto safe_destroy = [](bgfx::UniformHandle& handle)
        {
            if(bgfx::isValid(handle))
            {
                bgfx::destroy(handle);
                handle = {bgfx::kInvalidHandle};
            }
        };
        safe_destroy(u_params0);
        safe_destroy(u_params1);
        safe_destroy(u_params2);
        safe_destroy(u_color);
        safe_destroy(u_smSamplingParams);
        safe_destroy(u_csmFarDistances);
        safe_destroy(u_shadowBiasParams);
        safe_destroy(u_csmTexelWorld);
        safe_destroy(u_perspTexelParams);
        safe_destroy(u_shadowAxisU);
        safe_destroy(u_shadowAxisV);

        safe_destroy(u_tetraNormalGreen);
        safe_destroy(u_tetraNormalYellow);
        safe_destroy(u_tetraNormalBlue);
        safe_destroy(u_tetraNormalRed);

        safe_destroy(u_shadowMapMtx0);
        safe_destroy(u_shadowMapMtx1);
        safe_destroy(u_shadowMapMtx2);
        safe_destroy(u_shadowMapMtx3);

        safe_destroy(u_shadowCutoutBaseColor);
        safe_destroy(u_shadowCutoutSurfaceData);
        safe_destroy(u_shadowCutoutTiling);
        safe_destroy(s_shadowCutoutColor);

        safe_destroy(u_lightMtx);
    }

    union
    {
        struct
        {
            float m_ambientPass;
            float m_lightingPass;
            float m_unused00;
            float m_numSplits;
        };

        float m_params0[4];
    };

    union
    {
        struct
        {
            /// Constant receiver depth bias, in shadow-map texels.
            float m_shadowMapBias;
            /// Receiver normal offset, in shadow-map texels.
            float m_shadowMapOffset;
            float m_shadowMapParam0;
            float m_shadowMapParam1;
        };

        float m_params1[4];
    };

    union
    {
        struct
        {
            float m_depthValuePow;
            float m_showSmCoverage;
            float m_shadowMapTexelSize;
            /// Stored depth to the depth attachment's window depth (x scale + 1 - scale): 2 where
            /// the backend's clip depth spans [0, 1], 1 where it spans [-1, 1] (OpenGL).
            float m_hardwareDepthScale;
        };

        float m_params2[4];
    };

    union
    {
        struct
        {
            float m_XNum;
            float m_YNum;
            float m_XOffset;
            float m_YOffset;
        };

        float m_paramsBlur[4];
    };

    float m_tetraNormalGreen[3];
    float m_tetraNormalYellow[3];
    float m_tetraNormalBlue[3];
    float m_tetraNormalRed[3];
    float m_csmFarDistances[4];

    /// x = slope-scaled bias (texels per unit slope), y = d(stored depth)/d(world depth) for
    /// ortho and linear maps, z = the same numerator for perspective 1/z maps (the shader
    /// divides by w^2), w = z for the vertical tetrahedron faces (stencil-packed point lights).
    float m_shadowBiasParams[4];
    /// World size of one shadow-map texel per cascade (directional lights).
    float m_csmTexelWorld[4];
    /// Perspective maps: x = texel world size per unit distance along the light axis,
    /// y / z = map width / height per unit distance (spot and point lights).
    float m_perspTexelParams[4];
    /// World-space unit vectors along +u and +v of the map, for the receiver-plane bias
    /// (zero for point lights, whose faces would each need their own pair).
    float m_shadowAxisU[4];
    float m_shadowAxisV[4];

    float* m_lightMtxPtr;
    float* m_colorPtr;
    Light* m_lightPtr;
    float* m_shadowMapMtx0;
    float* m_shadowMapMtx1;
    float* m_shadowMapMtx2;
    float* m_shadowMapMtx3;

private:
    bgfx::UniformHandle u_params0;
    bgfx::UniformHandle u_params1;
    bgfx::UniformHandle u_params2;
    bgfx::UniformHandle u_color;
    bgfx::UniformHandle u_smSamplingParams;
    bgfx::UniformHandle u_csmFarDistances;
    bgfx::UniformHandle u_shadowBiasParams;
    bgfx::UniformHandle u_csmTexelWorld;
    bgfx::UniformHandle u_perspTexelParams;
    bgfx::UniformHandle u_shadowAxisU;
    bgfx::UniformHandle u_shadowAxisV;

    bgfx::UniformHandle u_tetraNormalGreen;
    bgfx::UniformHandle u_tetraNormalYellow;
    bgfx::UniformHandle u_tetraNormalBlue;
    bgfx::UniformHandle u_tetraNormalRed;

    bgfx::UniformHandle u_shadowMapMtx0;
    bgfx::UniformHandle u_shadowMapMtx1;
    bgfx::UniformHandle u_shadowMapMtx2;
    bgfx::UniformHandle u_shadowMapMtx3;

    bgfx::UniformHandle u_shadowCutoutBaseColor;
    bgfx::UniformHandle u_shadowCutoutSurfaceData;
    bgfx::UniformHandle u_shadowCutoutTiling;
    bgfx::UniformHandle s_shadowCutoutColor;

    bgfx::UniformHandle u_lightMtx;
};

struct RenderState
{
    enum Enum
    {
        Default = 0,

        ShadowMap_PackDepth,
        ShadowMap_PackDepthHoriz,
        ShadowMap_PackDepthVert,

        Custom_BlendLightTexture,
        Custom_DrawPlaneBottom,

        Count
    };

    uint64_t m_state;
    uint32_t m_blendFactorRgba;
    uint32_t m_fstencil;
    uint32_t m_bstencil;
};

struct PosColorTexCoord0Vertex : gfx::vertex<PosColorTexCoord0Vertex>
{
    float m_x;
    float m_y;
    float m_z;
    uint32_t m_rgba;
    float m_u;
    float m_v;

    static void init(bgfx::VertexLayout& decl)
    {
        decl.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
    }
};

struct PosVertex : gfx::vertex<PosVertex>
{
    float m_x;
    float m_y;
    float m_z;
    static void init(bgfx::VertexLayout& decl)
    {
        decl.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
    }
};

struct Programs
{
    void init(rtti::context& ctx);

    void destroy()
    {
        // Pack depth.
        for(uint8_t ii = 0; ii < DepthImpl::Count; ++ii)
        {
            for(uint8_t jj = 0; jj < PackDepth::Count; ++jj)
            {
                m_packDepth[ii][jj].reset();
                m_packDepthInstanced[ii][jj].reset();
                m_packDepthSkinned[ii][jj].reset();
                m_packDepthCutout[ii][jj].reset();
                m_packDepthInstancedCutout[ii][jj].reset();
                m_packDepthSkinnedCutout[ii][jj].reset();
                m_packDepthSkinnedInstanced[ii][jj].reset();
                m_packDepthSkinnedInstancedCutout[ii][jj].reset();
            }
        }

        // Draw depth.
        for(uint8_t ii = 0; ii < PackDepth::Count; ++ii)
        {
            m_drawDepth[ii].reset();
        }

        // Hblur.
        for(uint8_t ii = 0; ii < PackDepth::Count; ++ii)
        {
            m_hBlur[ii].reset();
        }

        // Vblur.
        for(uint8_t ii = 0; ii < PackDepth::Count; ++ii)
        {
            m_vBlur[ii].reset();
        }

        // Misc.
        m_black.reset();
        m_restore.reset();
    }

    gpu_program::ptr m_black;
    /// Copies a light's static copy back into its atlas tile (shadow map caching).
    gpu_program::ptr m_restore;
    gpu_program::ptr m_vBlur[PackDepth::Count];
    gpu_program::ptr m_hBlur[PackDepth::Count];
    gpu_program::ptr m_drawDepth[PackDepth::Count];
    gpu_program::ptr m_packDepth[DepthImpl::Count][PackDepth::Count];
    gpu_program::ptr m_packDepthSkinned[DepthImpl::Count][PackDepth::Count];
    gpu_program::ptr m_packDepthInstanced[DepthImpl::Count][PackDepth::Count];
    gpu_program::ptr m_packDepthCutout[DepthImpl::Count][PackDepth::Count];
    gpu_program::ptr m_packDepthSkinnedCutout[DepthImpl::Count][PackDepth::Count];
    gpu_program::ptr m_packDepthInstancedCutout[DepthImpl::Count][PackDepth::Count];
    /// Instanced skinned casters, the bones from the skinning palette buffer (vs_*_skinned_instanced*.sc). Reached from
    /// the settings table's skinned program through resolve_skinned_instanced_pack_program.
    gpu_program::ptr m_packDepthSkinnedInstanced[DepthImpl::Count][PackDepth::Count];
    gpu_program::ptr m_packDepthSkinnedInstancedCutout[DepthImpl::Count][PackDepth::Count];
};

struct ShadowMapSettings
{
#define SHADOW_FLOAT_PARAM(_name)                                                                                      \
    float _name{}, _name##Min{}, _name##Max{}, _name##Step                                                             \
    {                                                                                                                  \
    }
    SHADOW_FLOAT_PARAM(m_sizePwrTwo);
    SHADOW_FLOAT_PARAM(m_depthValuePow);
    SHADOW_FLOAT_PARAM(m_near);
    SHADOW_FLOAT_PARAM(m_far);
    SHADOW_FLOAT_PARAM(m_bias);
    SHADOW_FLOAT_PARAM(m_normalOffset);
    SHADOW_FLOAT_PARAM(m_customParam0);
    SHADOW_FLOAT_PARAM(m_customParam1);
    SHADOW_FLOAT_PARAM(m_xNum);
    SHADOW_FLOAT_PARAM(m_yNum);
    SHADOW_FLOAT_PARAM(m_xOffset);
    SHADOW_FLOAT_PARAM(m_yOffset);
    bool m_doBlur{};
    gpu_program* m_progPack{};
    gpu_program* m_progPackSkinned{};
    gpu_program* m_progPackInstanced{};
#undef SHADOW_FLOAT_PARAM
};

struct SceneSettings
{
    LightType::Enum m_lightType;
    DepthImpl::Enum m_depthImpl;
    SmImpl::Enum m_smImpl;
    float m_spotOuterAngle;
    float m_spotInnerAngle;
    float m_fovXAdjust;
    float m_fovYAdjust;
    float m_coverageSpotL;
    float m_splitDistribution;
    int m_numSplits;
    bool m_updateLights;
    bool m_updateScene;
    bool m_drawDepthBuffer;
    bool m_showSmCoverage;
    bool m_stencilPack;
    bool m_stabilize;
};

struct ClearValues
{
    ClearValues(uint32_t _clearRgba = 0x30303000, float _clearDepth = 1.0f, uint8_t _clearStencil = 0)
        : clear_rgba(_clearRgba)
        , clear_depth(_clearDepth)
        , clear_stencil(_clearStencil)
    {
    }

    uint32_t clear_rgba;
    float clear_depth;
    uint8_t clear_stencil;
};

struct frustum_calculation_method
{
    enum Enum
    {
        legacy,          // Original fixed frustum calculation
        adaptive,        // Altitude-aware adaptive frustum
        hybrid,          // Combination of both approaches

        count
    };
};

struct csm_optimization_flags
{
    enum Enum
    {
        none                    = 0,
        altitude_compensation   = 1 << 0,  // Adjust frustum based on camera altitude
        scene_bounds_fitting    = 1 << 1,  // Fit to actual scene bounds
        dynamic_split_weights   = 1 << 2,  // Adjust split distribution dynamically
        stabilized_light_matrix = 1 << 3,  // Improve light matrix positioning

        all = altitude_compensation | scene_bounds_fitting | dynamic_split_weights | stabilized_light_matrix
    };
};

struct adaptive_shadow_params
{
    float altitude_scale_factor = 0.5f;      // How much altitude affects frustum extension
    float min_altitude_boost = 0.05f;       // Minimum altitude compensation
    float max_altitude_boost = 100.0f;      // Maximum altitude compensation
    float scene_bounds_margin = 1.2f;       // Margin factor for scene bounds fitting
    float split_weight_bias = 0.1f;         // Bias for dynamic split weight adjustment
};

struct shadow_visibility_data
{
    entt::handle entity;
    /// The caster's world AABB, captured when the list was built so the per-light culling
    /// walks this list alone instead of looking the model component up again for every light.
    math::bbox world_bounds;
    /// Set for a mesh particle emitter: its particles cast instead of the entity's model.
    const particle_emitter_component* particles = nullptr;
};
using shadow_map_models_t = hpp::small_vector<shadow_visibility_data>;

/**
 * @brief Shadow map caching (graphics.shadow_map_caching): a light's map, or a cascade of the sun, keeps its content
 * across frames while nothing it shows changed, and is not rendered again. Off renders every map every frame.
 * Changing it makes every cached map stale.
 */
void set_map_caching_enabled(bool enabled);
auto is_map_caching_enabled() -> bool;

/**
 * @brief Makes every cached shadow map stale: the key a map is reused by includes this epoch. For changes no key
 * sees: a project opened, play starting or ending, the application returning from the background.
 */
void invalidate_cached_maps();
auto get_cached_maps_epoch() -> uint64_t;

/**
 * @brief Shadow mapping generator with improved algorithms for high-altitude cameras
 * 
 * This class provides three different frustum calculation methods:
 * 
 * 1. Legacy: Original fixed frustum calculation
 * 2. Adaptive: Camera altitude-aware frustum extension
 * 3. Hybrid: Blend between legacy and adaptive approaches
 * 
 * Usage examples:
 * 
 * // Enable adaptive shadows for high-altitude cameras
 * shadowmap_generator generator;
 * generator.enable_adaptive_shadows(true);
 * 
 * // Fine-tune adaptive parameters
 * adaptive_shadow_params params;
 * params.altitude_scale_factor = 0.3f;  // Less aggressive altitude compensation
 * params.max_altitude_boost = 50.0f;    // Cap the maximum boost
 * generator.set_adaptive_params(params);
 * 
 * // Use hybrid mode for gradual transition
 * generator.set_frustum_calculation_method(frustum_calculation_method::hybrid);
 * 
 * // Switch back to legacy for comparison
 * generator.set_frustum_calculation_method(frustum_calculation_method::legacy);
 */
class shadowmap_generator
{
public:
    /// submit_uniforms: leave the depth attachments unbound.
    static constexpr uint8_t no_compare_stage = UINT8_MAX;

    shadowmap_generator();
    ~shadowmap_generator();

    void init(rtti::context& ctx);
    void deinit();
    void deinit_textures();
    void deinit_uniforms();

    void update(const camera& cam, const light& l, const math::transform& ltrans, bool is_active);

    /**
     * @brief True when this light's shadow maps already hold their result for the current
     * render frame.
     *
     * Set where the light is FINISHED - generate_shadowmaps, or a caller finding there is
     * nothing to draw - and deliberately NOT by @ref update, which runs before the caller
     * knows whether the light matters to the view being rendered. A frame renders several
     * views (reflection probe faces, then the camera), and the shadow maps of a point / spot
     * light are the same for all of them, so the first view to resolve one wins. Latching on
     * @ref update instead let a probe face that could not see a light latch it out of the
     * main view's shadow generation, leaving that view sampling a stale map.
     */
    auto already_resolved() const -> bool;

    /// Marks the shadow maps as holding this frame's result without generating: the light is
    /// off, casts no shadows, or nothing is in range and the maps are already clear.
    void mark_resolved();

    /// True while the shadow maps hold anything but a cleared surface: casters rendered by the
    /// last generate_shadowmaps, or fresh render targets that were never cleared. A light whose
    /// caster list came back empty still has to generate once in that state, or the lighting
    /// pass keeps sampling whatever the maps held last.
    auto needs_clear() const -> bool;

    void generate_shadowmaps(const shadow_map_models_t& model, const camera& cam, ::unravel::rendering::pipeline_stats* stats = nullptr);

    auto get_depth_type() const -> PackDepth::Enum;
    auto get_rt_texture(uint8_t split) const -> bgfx::TextureHandle;
    auto get_depth_render_program(PackDepth::Enum depth) const -> gpu_program::ptr;
    /// Binds the stored-depth maps from @p stage on and, unless @p compare_stage is
    /// no_compare_stage, the maps' depth attachments with LEQUAL comparison samplers from
    /// @p compare_stage on (point-sampled maps only: hard, PCF and PCSS).
    void submit_uniforms(uint8_t stage, uint8_t compare_stage = no_compare_stage) const;


    /// Whether @p l renders its map into the shared shadow atlas (shadow_atlas): spot and point lights whose shadows
    /// compare depth (hard, PCF, PCSS). VSM and ESM lights store moments or blur their maps, and the cascades fit the
    /// view: those keep maps of their own.
    static auto uses_shadow_atlas(const light& l) -> bool;

    /// What @p l asks the atlas for in a view it reaches with @p importance (shadow_atlas_request::importance).
    auto make_atlas_request(const light& l, float importance) const -> shadow_atlas_request;

    /// Whether the light has a map to sample: its own, or its tile in the atlas.
    auto has_shadow_map() const -> bool;

    /// The light's map inside the texture get_rt_texture(0) returns, in uv (x, y, width, height): its atlas tile, or
    /// the whole texture for a map of its own.
    auto get_map_uv_rect() const -> math::vec4;

    /// The light's tile in the shadow atlas; none while it keeps a map of its own or has no tile.
    auto get_atlas_tile() const -> hpp::optional<shadow_atlas_tile>;

    /// Whether the map stores linear depth (sm_depth::linear): a receiver's stored depth is then z + 0.5 of its map
    /// coordinates, not z / w.
    auto has_linear_depth() const -> bool;

    /// World position -> map coordinates (before the divide by w) of map @p face: a spot light's one map (0), or a
    /// point light's tetrahedron face (0 green, 1 yellow, 2 blue, 3 red), as the lighting pass projects a receiver.
    auto get_world_to_map(uint8_t face) const -> math::mat4;

    /// Edge of every split's map in texels.
    auto get_shadow_map_size() const -> uint16_t
    {
        return current_shadow_map_size_;
    }

    // Configuration methods for improved shadow mapping
    void set_frustum_calculation_method(frustum_calculation_method::Enum method) { frustum_method_ = method; }
    auto get_frustum_calculation_method() const -> frustum_calculation_method::Enum { return frustum_method_; }
    
    void set_csm_optimization_flags(uint32_t flags) { csm_optimization_flags_ = flags; }
    auto get_csm_optimization_flags() const -> uint32_t { return csm_optimization_flags_; }
    
    void enable_adaptive_shadows(bool enable) { 
        if(enable) {
            frustum_method_ = frustum_calculation_method::adaptive;
            csm_optimization_flags_ = csm_optimization_flags::all;
        } else {
            frustum_method_ = frustum_calculation_method::legacy;
            csm_optimization_flags_ = csm_optimization_flags::none;
        }
    }

    // Adaptive parameter configuration methods
    void set_adaptive_params(const adaptive_shadow_params& params) { adaptive_params_ = params; }
    auto get_adaptive_params() const -> const adaptive_shadow_params& { return adaptive_params_; }
    
    void set_altitude_scale_factor(float factor) { adaptive_params_.altitude_scale_factor = factor; }
    auto get_altitude_scale_factor() const -> float { return adaptive_params_.altitude_scale_factor; }
    
    void set_altitude_boost_range(float min_boost, float max_boost) {
        adaptive_params_.min_altitude_boost = min_boost;
        adaptive_params_.max_altitude_boost = max_boost;
    }
    
    auto get_min_altitude_boost() const -> float { return adaptive_params_.min_altitude_boost; }
    auto get_max_altitude_boost() const -> float { return adaptive_params_.max_altitude_boost; }

private:
    /// What prepare_shadow_casters worked out for the views of this light (cascades, tetrahedron faces), and what
    /// collect_shadow_casters gathered.
    struct shadow_collection
    {
        bool anything_collected = false;
        bool has_static_batches = false;
        bool has_skinned_batches = false;
        /// Nothing any view shows changed since every view was last rendered (casters_key_): nothing is collected,
        /// every view keeps its content.
        bool reuse_all = false;
        /// Hash of every caster's facts this time (identity, LOD, change stamp, view mask) and of what decides them.
        uint64_t casters_key = 0;
        /// Per view: a hash of what its render would draw (shadow map caching).
        uint64_t view_hashes[ShadowMapRenderTargets::Count] = {};
        /// Hash of the casters that hold still (caster_facts::is_mover false) and of the views' settings: what the
        /// light's static copy holds.
        uint64_t static_key = 0;
        /// A caster that moves is in reach of some view.
        bool has_movers = false;
        /// How many casters that hold still are in reach of some view.
        uint32_t static_caster_count = 0;
    };

    /// What prepare_shadow_casters works out per caster before any of them is collected.
    struct caster_facts
    {
        entt::entity entity = entt::null;
        uint8_t view_mask = 0;
        uint32_t lod_index = 0;
        /// What the views holding it must know to keep their content: which model, its LOD and mesh, and when its
        /// shadow last changed (moved, re-posed, a material edited).
        uint64_t content_hash = 0;
        /// Its shadow changed within the last frames, or it is a particle emitter: it stays out of the static copy.
        bool is_mover = false;
        /// The last collection that took it drew it: when it is not collected, it is marked rendered as before.
        bool is_marked = false;
    };

    /// Which casters collect_shadow_casters takes.
    enum class caster_filter : uint8_t
    {
        all,
        /// The ones that hold still: the static copy's content.
        static_only,
        /// The ones that move: drawn over the restored static copy.
        movers_only,
    };

    /// A caster on the individual path (no batching), recorded by collect_shadow_casters and drawn by
    /// submit_shadow_casters into the views that render.
    struct individual_caster
    {
        const shadow_visibility_data* element = nullptr;
        uint32_t lod_index = 0;
        uint8_t view_mask = 0;
        /// Its skinned submeshes went to the skinned batches: only the others are drawn individually.
        bool skinned_batched = false;
    };

    /// The views this light renders into: 1 for a spot light, 4 tetrahedron faces, or the cascades.
    auto get_view_count() const -> uint8_t;

    /// Directional lights: the camera's reach test (shadow_reach); an empty one for the others.
    auto make_shadow_reach(const ShadowMapSettings* currentSmSettings, const camera* cam) const -> shadow_reach;

    /// Skinned casters batch when the instanced skinned program of this map's depth and packing exists.
    auto is_skinned_batching_enabled(const ShadowMapSettings* currentSmSettings) const -> bool;

    /// Works out every caster's facts and the keys of what each view would draw, without collecting anything; sets
    /// shadow_collection::reuse_all when every view keeps its content.
    auto prepare_shadow_casters(const shadow_map_models_t& models,
                                const math::frustum frustums[ShadowMapRenderTargets::Count],
                                const ShadowMapSettings* currentSmSettings,
                                const camera* cam) -> shadow_collection;

    /// Works out every caster's facts into caster_facts_ and the collection's keys: the casters key with the settings
    /// of every view, the views' content hashes and the static copy's key.
    /// @param reach Directional lights: the camera's reach test; the camera then enters the casters key.
    /// @param batching_bits Which batching paths are on: they change how the casters are drawn.
    void compute_caster_facts(const shadow_map_models_t& models,
                              const math::frustum frustums[ShadowMapRenderTargets::Count],
                              const shadow_reach& reach,
                              const camera* cam,
                              uint64_t batching_bits,
                              shadow_collection& collection);

    /// Collects the casters of @p filter into the batches and individual_casters_, without drawing anything. A
    /// directional light's view hashes take what was collected: its reach test keeps submeshes the facts do not see.
    void collect_shadow_casters(const shadow_map_models_t& models,
                                const math::frustum frustums[ShadowMapRenderTargets::Count],
                                ShadowMapSettings* currentSmSettings,
                                const camera* cam,
                                caster_filter filter,
                                shadow_collection& collection,
                                ::unravel::rendering::pipeline_stats* stats);

    /// Every view keeps its content when caching is on, every view has content and @p casters_key is the one that
    /// content was rendered under.
    auto can_reuse_all_views(uint64_t casters_key) const -> bool;

    /// Marks the casters the last collection drew as rendered this frame, as collecting them would.
    void mark_reused_casters(const shadow_map_models_t& models, uint64_t frame) const;

    /// The views (bits) whose content must be rendered this time: all of them without caching; otherwise those whose
    /// key changed (a local light's faces render together). Writes the keys to @p keys.
    auto select_views_to_render(const shadow_collection& collection, uint64_t keys[ShadowMapRenderTargets::Count]) const
        -> uint8_t;

    /// What a view's content depends on besides its casters: the light's matrices and map settings, the atlas
    /// placement and the cached maps epoch.
    auto compute_view_settings_key(uint8_t view) const -> uint64_t;

    /// The key a view's content is reused by: its settings key and what the view draws.
    auto compute_view_key(uint8_t view, const shadow_collection& collection) const -> uint64_t;

    /// A local light in the atlas with casters that move in reach renders them over its static copy, when enough
    /// casters hold still to pay for the restore.
    auto can_use_static_copy(const shadow_collection& collection) const -> bool;

    /// Renders the casters that hold still into the light's square of the static copy (shadow_atlas), when what it
    /// holds is not this collection's. Takes views of its own ahead of the map's.
    void refresh_static_copy(const shadow_map_models_t& models,
                             ShadowMapSettings* currentSmSettings,
                             const camera& cam,
                             shadow_collection& collection,
                             ::unravel::rendering::pipeline_stats* stats);

    /// Sets the rect, transform and frame buffer of a spot or point light's views: views[0] for the clear and the
    /// stencil mask, views[1..4] for the faces, in the light's tile at @p map_x, @p map_y of @p target.
    void set_local_light_views(const bgfx::ViewId views[5],
                               bgfx::FrameBufferHandle target,
                               uint16_t map_x,
                               uint16_t map_y) const;

    /// Draws the stencil mask that splits a stencil-packed point light's map between its faces.
    void submit_stencil_mask(bgfx::ViewId view) const;

    /// Writes the light's static copy into its tile: the depth, and the atlas colour rebuilt from it.
    void submit_static_copy_restore(bgfx::ViewId view, bool origin_bottom_left) const;

    /// The render state a face draws with; @p writes_color false for the static copy, which has no colour.
    auto get_face_render_state(uint8_t face, bool writes_color) const -> RenderState;

    /// Draws what collect_shadow_casters gathered into the views of @p view_mask.
    /// @param writes_color False for the static copy, which has no colour.
    void submit_shadow_casters(uint8_t view_mask,
                               bgfx::ViewId shadowmap_1_id,
                               const math::frustum frustums[ShadowMapRenderTargets::Count],
                               ShadowMapSettings* currentSmSettings,
                               const shadow_collection& collection,
                               bool writes_color,
                               ::unravel::rendering::pipeline_stats* stats);

    /// Draws one recorded individual caster into the views of @p view_mask.
    void submit_individual_caster(const individual_caster& caster,
                                  uint8_t view_mask,
                                  bgfx::ViewId shadowmap_1_id,
                                  const math::frustum frustums[ShadowMapRenderTargets::Count],
                                  ShadowMapSettings* currentSmSettings,
                                  bool writes_color,
                                  ::unravel::rendering::pipeline_stats* stats);

    /// @param skinned The collector holds skinned instances (cascade_skinned_batch_collectors_): drawn with the instanced
    ///        skinned programs, the bones from the skinning palette buffer.
    void submit_batched_shadow_geometry_cascade(shadow_batch_collector& collector,
                                               bgfx::ViewId viewId,
                                               ShadowMapSettings* currentSmSettings,
                                               const RenderState& renderState,
                                               bool skinned,
                                               ::unravel::rendering::pipeline_stats* stats = nullptr);

    /// Fills the geometric bias conversions (depth scale, map axes, perspective texel size)
    /// from the final light view / projection matrices of this update.
    void update_bias_uniforms(bool origin_bottom_left);

    ClearValues clear_values_;

    float color_[4];
    Light point_light_;
    Light directional_light_;

    float light_mtx_[16];
    float shadow_map_mtx_[ShadowMapRenderTargets::Count][16];

    ShadowMapSettings sm_settings_[LightType::Count][DepthImpl::Count][SmImpl::Count];
    SceneSettings settings_;

    uint16_t current_shadow_map_size_{};
    uint8_t current_num_splits_{};

    Uniforms uniforms_;
    Programs programs_;

    float light_view_[ShadowMapRenderTargets::Count][16];
    float light_proj_[ShadowMapRenderTargets::Count][16];

    math::frustum light_frustums_[ShadowMapRenderTargets::Count];

    bgfx::UniformHandle tex_color_{bgfx::kInvalidHandle};
    bgfx::UniformHandle shadow_map_[ShadowMapRenderTargets::Count];
    bgfx::UniformHandle shadow_depth_[ShadowMapRenderTargets::Count];
    bgfx::FrameBufferHandle rt_shadow_map_[ShadowMapRenderTargets::Count];
    bgfx::FrameBufferHandle rt_blur_{bgfx::kInvalidHandle};

    bool valid_{};
    /// See needs_clear.
    bool needs_clear_{true};

    /// The light renders into the shadow atlas (uses_shadow_atlas) instead of maps of its own.
    bool uses_atlas_{};
    /// Its tile this frame, when it has one.
    hpp::optional<shadow_atlas_tile> atlas_tile_;
    /// The atlas generation the tile's content was rendered under: under another one it is stale.
    uint64_t atlas_generation_ = UINT64_MAX;
    /// The tile's placement serial (shadow_atlas::find_placement): the map is only in the tile while it is current.
    uint64_t atlas_placement_ = 0;

    /// Per view: the key its content was last rendered under (compute_view_key), valid while view_key_valid_.
    uint64_t view_keys_[ShadowMapRenderTargets::Count] = {};
    bool view_key_valid_[ShadowMapRenderTargets::Count] = {};
    /// The shadow_collection::casters_key every view's content matches, valid while casters_key_valid_.
    uint64_t casters_key_ = 0;
    bool casters_key_valid_ = false;
    /// The shadow_collection::static_key the light's square of the static copy holds, rendered under
    /// static_copy_generation_ (shadow_atlas::get_static_generation); valid while static_copy_valid_.
    uint64_t static_copy_key_ = 0;
    uint64_t static_copy_generation_ = 0;
    bool static_copy_valid_ = false;
    /// s_staticDepth and u_restoreParams of the restore (fs_shadowmaps_restore).
    bgfx::UniformHandle static_depth_sampler_{bgfx::kInvalidHandle};
    bgfx::UniformHandle restore_params_{bgfx::kInvalidHandle};
    /// Scratch of collect_shadow_casters / submit_shadow_casters.
    std::vector<individual_caster> individual_casters_;
    std::vector<caster_facts> caster_facts_;
    /// Forgets every view key (new or lost render targets).
    void invalidate_view_keys();
    /// u_shadowAtlasRect: the tile in atlas uv (common_shadow.sh shadowMapFetchUV).
    bgfx::UniformHandle shadow_atlas_rect_{bgfx::kInvalidHandle};
    float atlas_rect_[4] = {0.0f, 0.0f, 1.0f, 1.0f};

    /// The key of this light's tile in the atlas.
    auto get_atlas_owner() const -> std::uintptr_t
    {
        return reinterpret_cast<std::uintptr_t>(this);
    }
    /// Frees the maps of its own (a light that moved into the atlas).
    void destroy_own_maps();
    /// Picks up its tile for this frame from the atlas (update).
    void update_atlas_tile();

    /// Render frame this light was last resolved in; see already_resolved.
    uint64_t last_resolve_frame_ = -1;
    
    // Static mesh batching system for shadow maps - one collector per cascade/view
    std::vector<shadow_batch_collector> cascade_batch_collectors_;
    /// The skinned casters' submeshes, one collector per cascade/view (skinned mesh batching).
    std::vector<shadow_batch_collector> cascade_skinned_batch_collectors_;
    
    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);

    // New configuration members
    frustum_calculation_method::Enum frustum_method_ = frustum_calculation_method::legacy;
    uint32_t csm_optimization_flags_ = csm_optimization_flags::none;
    
    // Additional parameters for adaptive calculations
    adaptive_shadow_params adaptive_params_;
};

} // namespace shadow
} // namespace unravel
