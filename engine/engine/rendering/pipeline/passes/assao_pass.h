#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gpu_program.h>
#include <graphics/render_view.h>

#include <array>
#include <cstddef>

namespace unravel
{

class assao_pass
{
public:

    struct settings
    {
        // clang-format off

        /**
         * World (view) space size of the occlusion sphere.
         * \note Range: [0.0, ~]
         */
        float radius{1.2f};

        /**
         * Effect strength linear multiplier.
         * \note Range: [0.0, 5.0]
         */
        float shadow_multiplier{1.0f};

        /**
         * Effect strength power modifier.
         * \note Range: [0.5, 5.0]
         */
        float shadow_power{1.0f};

        /**
         * Effect max limit (applied after multiplier but before blur).
         * \note Range: [0.0, 1.0]
         */
        float shadow_clamp{0.98f};

        /**
         * Limits self-shadowing.
         * Makes the sampling area less of a hemisphere, more of a spherical cone, to avoid self-shadowing and various artifacts due to low tessellation and depth buffer imprecision, etc.
         * \note Range: [0.0, 0.2]
         */
        float horizon_angle_threshold{0.06f};

        /**
         * Distance to start fading out the effect.
         * \note Range: [0.0, ~]
         */
        float fade_out_from{50.0f};

        /**
         * Distance at which the effect is faded out.
         * \note Range: [0.0, ~]
         */
        float fade_out_to{200.0f};

        /**
         * Effect quality.
         * -1: Lowest (low, half res checkerboard)
         * 0: Low
         * 1: Medium
         * 2: High
         * 3: Very high / adaptive
         * Each quality level is roughly 2x more costly than the previous, except q3 which is variable but generally above q2.
         * \note Range: [-1, 3]
         */
        int32_t quality_level{3};

        /**
         * Adaptive quality limit (only for Quality Level 3).
         * \note Range: [0.0, 1.0]
         */
        float adaptive_quality_limit{0.45f};

        /**
         * Number of edge-sensitive smart blur passes to apply.
         * \note Quality 0 is an exception with only one 'dumb' blur pass used.
         * \note Range: [0, 6]
         */
        int32_t blur_pass_count{2};

        /**
         * Sharpness (how much to bleed over edges).
         * 1: Not at all
         * 0.5: Half-half
         * 0.0: Completely ignore edges
         * \note Range: [0.0, 1.0]
         */
        float sharpness{0.98f};

        /**
         * Used to rotate sampling kernel.
         * If using temporal AA / supersampling, suggested to rotate by ((frame%3)/3.0*PI) or similar.
         * Kernel is already symmetrical, which is why we use PI and not 2*PI.
         * \note Range: [0.0, PI]
         */
        float temporal_supersampling_angle_offset{0.0f};

        /**
         * Used to scale sampling kernel.
         * If using temporal AA / supersampling, suggested to scale by (1.0f + (((frame%3)-1.0)/3.0)*0.1) or similar.
         * \note Range: [0.0, 2.0]
         */
        float temporal_supersampling_radius_offset{1.0f};

        /**
         * Used for high-res detail AO using neighboring depth pixels.
         * Adds a lot of detail but also reduces temporal stability (adds aliasing).
         * \note Range: [0.0, 5.0]
         */
        float detail_shadow_strength{0.5f};

        /**
         * If true, normals will be generated from depth.
         * \note Range: [true/false]
         */
        bool generate_normals{false};

        // clang-format on
    };

    struct run_params
    {
        gfx::texture* depth{};
        gfx::texture* normal{};

        settings params{};
    };


    assao_pass() = default;
    ~assao_pass()
    {
        shutdown();
    }

    auto init(rtti::context& ctx) -> bool;
    /// Runs the pass; without its compute programs (a device without compute) or under an orthographic camera it
    /// releases its targets instead, so the lighting reads no AO rather than a map nothing wrote or a wrong one.
    void run(const camera& camera, gfx::render_view& rview, const run_params& params);
    auto shutdown() -> int32_t;
    void release_resources(gfx::render_view& rview);
    /// Whether every compute program of the pass was built, as of the last run.
    auto has_programs() const -> bool;

    /// The full-resolution AO this view's last run produced (RGBA8, the visibility in every
    /// channel), or null when the pass is not running. The lighting reads it as the
    /// screen-space AO when GTAO is off.
    auto get_ao_texture(gfx::render_view& rview) const -> gfx::texture::ptr;

private:
    struct dimensions
    {
        int32_t size[2]{};
        int32_t halfSize[2]{};
        int32_t quarterSize[2]{};
        int32_t fullResOutScissorRect[4]{};
        int32_t halfResOutScissorRect[4]{};
    };
    /// The compute programs below.
    static constexpr std::size_t PROGRAM_COUNT = 25;
    /// Every program, null before init.
    auto get_programs() const -> std::array<gpu_program*, PROGRAM_COUNT>;
    /// The gate of a frame's work (gpu_program::begin): whether every program can run, picking up reloaded shaders.
    auto begin_programs() -> bool;
    void create_or_update_frame_buffers(gfx::render_view& rview, const dimensions& dims);
    void update_uniforms(int32_t _pass, const float* view, const float* proj, const dimensions& dims);

    struct uniforms
    {
        enum
        {
            NumVec4 = 19
        };

        void init()
        {
            u_params = bgfx::createUniform("u_params", bgfx::UniformType::Vec4, NumVec4);
        }

        void submit()
        {
            bgfx::setUniform(u_params, m_params, NumVec4);
        }

        void destroy()
        {
            bgfx::destroy(u_params);
        }

        // clang-format off
        union
        {
            struct
            {
                /*  0    */ struct { float m_viewportPixelSize[2]; float m_halfViewportPixelSize[2]; };
                /*  1    */ struct { float m_depthUnpackConsts[2]; float m_unused0[2]; };
                /*  2    */ struct { float m_ndcToViewMul[2]; float m_ndcToViewAdd[2]; };
                /*  3    */ struct { float m_perPassFullResCoordOffset[2]; float m_perPassFullResUVOffset[2]; };
                /*  4    */ struct { float m_viewport2xPixelSize[2]; float m_viewport2xPixelSize_x_025[2]; };
                /*  5    */ struct { float m_effectRadius; float m_effectShadowStrength; float m_effectShadowPow; float m_effectShadowClamp; };
                /*  6    */ struct { float m_effectFadeOutMul; float m_effectFadeOutAdd; float m_effectHorizonAngleThreshold; float m_effectSamplingRadiusNearLimitRec; };
                /*  7    */ struct { float m_depthPrecisionOffsetMod; float m_negRecEffectRadius; float m_loadCounterAvgDiv; float m_adaptiveSampleCountLimit; };
                /*  8    */ struct { float m_invSharpness; float m_passIndex; float m_quarterResPixelSize[2]; };
                /*  9-13 */ struct { float m_patternRotScaleMatrices[5][4]; };
                /* 14    */ struct { float m_normalsUnpackMul; float m_normalsUnpackAdd; float m_detailAOStrength; float m_layer; };
                /* 15-18 */ struct { float m_normalsWorldToViewspaceMatrix[16]; };
            };

            float m_params[NumVec4 * 4];
        };
        // clang-format on

        bgfx::UniformHandle u_params{bgfx::kInvalidHandle};
    };

    // Resource handles

    gpu_program::ptr m_prepareDepthsProgram;
    gpu_program::ptr m_prepareNormalsProgram;
    gpu_program::ptr m_prepareDepthsHalfProgram;
    gpu_program::ptr m_prepareDepthsAndNormalsHalfProgram;
    gpu_program::ptr m_prepareDepthMipProgram;
    gpu_program::ptr m_generateQ0Program;
    gpu_program::ptr m_generateQ1Program;
    gpu_program::ptr m_generateQ2Program;
    gpu_program::ptr m_generateQ3Program;
    gpu_program::ptr m_generateQ3BaseProgram;
    gpu_program::ptr m_generateQ0GBufferProgram;
    gpu_program::ptr m_generateQ1GBufferProgram;
    gpu_program::ptr m_generateQ2GBufferProgram;
    gpu_program::ptr m_generateQ3GBufferProgram;
    gpu_program::ptr m_generateQ3BaseGBufferProgram;

    gpu_program::ptr m_smartBlurProgram;
    gpu_program::ptr m_smartBlurWideProgram;
    gpu_program::ptr m_nonSmartBlurProgram;
    gpu_program::ptr m_applyProgram;
    gpu_program::ptr m_nonSmartApplyProgram;
    gpu_program::ptr m_nonSmartHalfApplyProgram;
    gpu_program::ptr m_generateImportanceMapProgram;
    gpu_program::ptr m_postprocessImportanceMapAProgram;
    gpu_program::ptr m_postprocessImportanceMapBProgram;
    gpu_program::ptr m_loadCounterClearProgram;

    // Shader uniforms
    bgfx::UniformHandle u_rect{bgfx::kInvalidHandle};

    // Uniforms to identify texture samples
    bgfx::UniformHandle s_normalmapSource{bgfx::kInvalidHandle};
    bgfx::UniformHandle s_blurInput{bgfx::kInvalidHandle};
    bgfx::UniformHandle s_finalSSAO{bgfx::kInvalidHandle};
    bgfx::UniformHandle s_depthSource{bgfx::kInvalidHandle};
    bgfx::UniformHandle s_viewspaceDepthSource{bgfx::kInvalidHandle};
    bgfx::UniformHandle s_viewspaceDepthSourceMirror{bgfx::kInvalidHandle};
    bgfx::UniformHandle s_importanceMap{bgfx::kInvalidHandle};

    bgfx::DynamicIndexBufferHandle m_loadCounter{bgfx::kInvalidHandle};

    settings m_settings{};
    uniforms m_uniforms{};
};
} // namespace unravel
