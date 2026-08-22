/*
 * This example demonstrates:
 * - Usage of Perez sky model [1] to render a dynamic sky.
 * - Rendering a mesh with a lightmap, shading of which is driven by the same parameters as the sky.
 *
 * Typically, the sky is rendered using cubemaps or other environment maps.
 * This approach can provide a high-quality sky, but the downside is that the
 * image is static. To achieve daytime changes in sky appearance, there is a need
 * in a dynamic model.
 *
 * Perez "An All-Weather Model for Sky Luminance Distribution" is a simple,
 * but good enough model which is, in essence, a function that
 * interpolates a sky color. As input, it requires several turbidity
 * coefficients, a color at zenith and direction to the sun.
 * Turbidity coefficients are taken from [2], which are computed using more
 * complex physically based models. Color at zenith depends on daytime and can
 * vary depending on many factors.
 *
 * In the code below, there are two tables that contain sky and sun luminance
 * which were computed using code from [3]. Luminance in those tables
 * represents actual scale of light energy that comes from sun compared to
 * the sky.
 *
 * The sky is driven by luminance of the sky, while the material of the
 * landscape is driven by both, the luminance of the sky and the sun. The
 * lightening model is very simple and consists of two parts: directional
 * light and hemisphere light. The first is used for the sun while the second
 * is used for the sky. Additionally, the second part is modulated by a
 * lightmap to achieve ambient occlusion effect.
 *
 * References
 * ==========
 *
 * [1] R. Perez, R. Seals, and J. Michalsky."An All-Weather Model for Sky Luminance Distribution".
 *     Solar Energy, Volume 50, Number 3 (March 1993), pp. 235-245.
 *
 * [2] A. J. Preetham, Peter Shirley, and Brian Smits. "A Practical Analytic Model for Daylight",
 *     Proceedings of the 26th Annual Conference on Computer Graphics and Interactive Techniques,
 *     1999, pp. 91-100.
 *     https://www.cs.utah.edu/~shirley/papers/sunsky/sunsky.pdf
 *
 * [3] E. Lengyel, Game Engine Gems, Volume One. Jones & Bartlett Learning, 2010. pp. 219 - 234
 *
 */

#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gpu_program.h>
#include <engine/rendering/perez_luminance.h>

#include <graphics/index_buffer.h>
#include <graphics/texture.h>
#include <graphics/vertex_buffer.h>
#include <graphics/vertex_decl.h>
#include <graphics/render_view.h>

namespace unravel
{

namespace detail
{

// Controls sun position according to time, month, and observer's latitude.
// Sun position computation based on Earth's orbital elements:
// https://nssdc.gsfc.nasa.gov/planetary/factsheet/earthfact.html
class sun_controller
{
public:
    enum class month : int
    {
        january = 0,
        jebruary,
        march,
        april,
        may,
        june,
        july,
        august,
        september,
        october,
        november,
        december
    };

    sun_controller()
        : north_dir_(1.0f, 0.0f, 0.0f)
        , sun_dir_(0.0f, -1.0f, 0.0f)
        , up_dir_(0.0f, 1.0f, 0.0f)
        , latitude_(50.0f)
        , month_(month::june)
        , ecliptic_obliquity_(bx::toRad(23.4f))
        , delta_(0.0f)
    {
    }

    void update(float _time)
    {
        calculate_sun_orbit();
        update_sun_position(_time - 12.0f);
    }

    bx::Vec3 north_dir_;
    bx::Vec3 sun_dir_;
    bx::Vec3 up_dir_;
    float latitude_;
    month month_;

private:
    void calculate_sun_orbit()
    {
        const float day = 30.0f * float(month_) + 15.0f;
        float lambda = 280.46f + 0.9856474f * day;
        lambda = bx::toRad(lambda);
        delta_ = bx::asin(bx::sin(ecliptic_obliquity_) * bx::sin(lambda));
    }

    void update_sun_position(float _hour)
    {
        const float latitude = bx::toRad(latitude_);
        const float hh = _hour * bx::kPi / 12.0f;
        const float azimuth =
            bx::atan2(bx::sin(hh), bx::cos(hh) * bx::sin(latitude) - bx::tan(delta_) * bx::cos(latitude));

        const float altitude =
            bx::asin(bx::sin(latitude) * bx::sin(delta_) + bx::cos(latitude) * bx::cos(delta_) * bx::cos(hh));

        const bx::Quaternion rot0 = bx::fromAxisAngle(up_dir_, -azimuth);
        const bx::Vec3 dir = bx::mul(north_dir_, rot0);
        const bx::Vec3 uxd = bx::cross(up_dir_, dir);

        const bx::Quaternion rot1 = bx::fromAxisAngle(uxd, altitude);
        sun_dir_ = bx::mul(dir, rot1);
    }

    float ecliptic_obliquity_;
    float delta_;
};

} // namespace detail

class atmospheric_pass_perez
{
public:
    atmospheric_pass_perez() = default;
    ~atmospheric_pass_perez();

    struct run_params
    {
        math::vec3 light_direction = math::normalize(math::vec3(0.2f, -0.8f, 1.0f));

        // [1.9 - 10.0f]
        float turbidity = 1.9f;

        /// Cloud mode: 0=none, 1=flat, 2=volumetric. Defaults mirror skylight_component; the
        /// pipeline always copies the component values (see deferred::run_atmospherics_pass).
        int cloud_mode = 2;
        /// Cloud coverage [0 = clear sky, 1 = overcast].
        float cloud_coverage = 0.4f;
        /// Weather-scale coverage variation [0, 1.5].
        float cloud_macro_variation = 1.5f;
        /// Layer base, height above the camera (the sky is rendered camera-relative).
        float cloud_base_altitude = 27500.0f;
        /// Layer thickness, base to top.
        float cloud_thickness = 40000.0f;
        /// Typical size of a cloud mass in world units (world-to-noise scale).
        float cloud_size = 20000.0f;
        /// Edge ramp width.
        float cloud_softness = 0.8f;
        /// Detail erosion strength.
        float cloud_detail_erode = 0.7f;
        /// Extinction scale.
        float cloud_density = 1.5f;
        /// Sun-path extinction as a fraction of the view extinction.
        float cloud_shadow_strength = 0.25f;
        /// Layer altitudes measured from world y = 0 (true) or from the camera (false).
        bool cloud_world_space_altitude = true;
        /// Project the cloud layer as a shadow on the scene (directional light).
        bool cloud_shadows = false;
        /// Opacity of the projected cloud shadow [0, 1].
        float cloud_shadow_opacity = 1.0f;
        /// Wind offset of the noise field in noise units (wrapped to the tile period).
        math::vec2 cloud_wind_offset{0.0f, 0.0f};
        /// Accumulated time (seconds), periodic; drives the star twinkle.
        float cloud_time = 0.0f;
        /// Sky brightness multiplier (1.0 = neutral). Affects visible sky and irradiance.
        float sky_brightness = 1.0f;
    };

    /// Cloud shadow map of one frame: sun transmittance of the cloud layer over a square of
    /// the world around the camera (fs_cloud_shadow.sc). Texel (u, v) is the entry point of
    /// the sun ray at the layer base at world (origin + (uv - 0.5) * extent); the lighting
    /// pass projects surface points up the sun direction to read it, the irradiance bake
    /// reads the lowest mip as the sky's cloud coverage.
    struct cloud_shadow_result
    {
        gfx::texture::ptr map;
        /// World xz of the map centre.
        math::vec2 origin{0.0f, 0.0f};
        /// World extent of the map (square).
        float extent{1.0f};
        /// World y of the layer base.
        float base_world_y{0.0f};
        /// Opacity of the projected shadow.
        float opacity{1.0f};
        /// False when there is no cloud layer this frame (map may be null).
        bool valid{false};
        /// Whether the directional light should apply it (the component checkbox).
        bool apply_to_lights{false};
    };

    auto init(rtti::context& ctx) -> bool;
    void run(gfx::frame_buffer::ptr input, const camera& camera, gfx::render_view& rview, delta_t dt, const run_params& params);

    /// Renders the cloud shadow map for this frame (before the lighting passes).
    auto run_cloud_shadow_pass(const camera& camera, gfx::render_view& rview, const run_params& params)
        -> cloud_shadow_result;

private:

    struct atmospheric_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_sunLuminance, "u_sunLuminance", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_skyLuminanceXYZ, "u_skyLuminanceXYZ", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_skyLuminance, "u_skyLuminance", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sunDirection, "u_sunDirection", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_parameters, "u_parameters", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_perezCoeff, "u_perezCoeff", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_cloudParams, "u_cloudParams", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_cloudParams2, "u_cloudParams2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_cloudParams3, "u_cloudParams3", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_cloudParams4, "u_cloudParams4", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_cloudCamera, "u_cloudCamera", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_cloudNoise2D, "s_cloudNoise2D", gfx::uniform_type::Sampler);
        }

        gfx::program::uniform_ptr u_sunLuminance;
        gfx::program::uniform_ptr u_skyLuminanceXYZ;
        gfx::program::uniform_ptr u_skyLuminance;
        gfx::program::uniform_ptr u_sunDirection;

        gfx::program::uniform_ptr u_parameters;
        gfx::program::uniform_ptr u_perezCoeff;
        gfx::program::uniform_ptr u_cloudParams;
        gfx::program::uniform_ptr u_cloudParams2;
        gfx::program::uniform_ptr u_cloudParams3;
        gfx::program::uniform_ptr u_cloudParams4;
        gfx::program::uniform_ptr u_cloudCamera;
        gfx::program::uniform_ptr s_cloudNoise2D;

        std::unique_ptr<gpu_program> program;

    } atmospheric_program_;

    struct cloud_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_skyLuminanceXYZ, "u_skyLuminanceXYZ", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_skyLuminance, "u_skyLuminance", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sunLuminance, "u_sunLuminance", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sunDirection, "u_sunDirection", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_parameters, "u_parameters", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_perezCoeff, "u_perezCoeff", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_cloudParams, "u_cloudParams", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_cloudParams2, "u_cloudParams2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_cloudParams3, "u_cloudParams3", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_cloudParams4, "u_cloudParams4", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_cloudNoise, "s_cloudNoise", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_cloudNoise2D, "s_cloudNoise2D", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_cloudHistory, "s_cloudHistory", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_cloudHistoryAux, "s_cloudHistoryAux", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_depth, "s_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_cloudFrame, "u_cloudFrame", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_cloudCamera, "u_cloudCamera", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_cloudHistory, "u_cloudHistory", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_prevViewProj, "u_prevViewProj", gfx::uniform_type::Mat4);
        }

        gfx::program::uniform_ptr u_skyLuminanceXYZ;
        gfx::program::uniform_ptr u_skyLuminance;
        gfx::program::uniform_ptr u_sunLuminance;
        gfx::program::uniform_ptr u_sunDirection;
        gfx::program::uniform_ptr u_parameters;
        gfx::program::uniform_ptr u_perezCoeff;
        gfx::program::uniform_ptr u_cloudParams;
        gfx::program::uniform_ptr u_cloudParams2;
        gfx::program::uniform_ptr u_cloudParams3;
        gfx::program::uniform_ptr u_cloudParams4;
        gfx::program::uniform_ptr u_cloudFrame;
        gfx::program::uniform_ptr u_cloudCamera;
        gfx::program::uniform_ptr u_cloudHistory;
        gfx::program::uniform_ptr u_prevViewProj;
        gfx::program::uniform_ptr s_cloudNoise;
        gfx::program::uniform_ptr s_cloudNoise2D;
        gfx::program::uniform_ptr s_cloudHistory;
        gfx::program::uniform_ptr s_cloudHistoryAux;
        gfx::program::uniform_ptr s_depth;

        std::unique_ptr<gpu_program> program;

    } cloud_program_;

    struct cloud_composite_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_cloudComposite, "u_cloudComposite", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_cloudTex, "s_cloudTex", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_cloudAux, "s_cloudAux", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_depth, "s_depth", gfx::uniform_type::Sampler);
        }

        gfx::program::uniform_ptr u_cloudComposite;
        gfx::program::uniform_ptr s_cloudTex;
        gfx::program::uniform_ptr s_cloudAux;
        gfx::program::uniform_ptr s_depth;

        std::unique_ptr<gpu_program> program;

    } cloud_composite_program_;

    struct cloud_shadow_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_sunDirection, "u_sunDirection", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_cloudParams, "u_cloudParams", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_cloudParams2, "u_cloudParams2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_cloudParams3, "u_cloudParams3", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_cloudParams4, "u_cloudParams4", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_cloudCamera, "u_cloudCamera", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_cloudShadowMap, "u_cloudShadowMap", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_cloudNoise, "s_cloudNoise", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_cloudNoise2D, "s_cloudNoise2D", gfx::uniform_type::Sampler);
        }

        gfx::program::uniform_ptr u_sunDirection;
        gfx::program::uniform_ptr u_cloudParams;
        gfx::program::uniform_ptr u_cloudParams2;
        gfx::program::uniform_ptr u_cloudParams3;
        gfx::program::uniform_ptr u_cloudParams4;
        gfx::program::uniform_ptr u_cloudCamera;
        gfx::program::uniform_ptr u_cloudShadowMap;
        gfx::program::uniform_ptr s_cloudNoise;
        gfx::program::uniform_ptr s_cloudNoise2D;

        std::unique_ptr<gpu_program> program;

    } cloud_shadow_program_;

    /// Uniform payload shared by the sky pass and the cloud pre-pass.
    struct cloud_uniform_block
    {
        float exposition[4];
        float cloud_params[4];
        float cloud_params2[4];
        float cloud_params3[4];
        float cloud_params4[4];
        float camera[4];
    };

    /// Packs the cloud uniforms shared by every cloud program.
    static auto make_cloud_uniforms(const run_params& params, const camera& camera, float exposition, float hour)
        -> cloud_uniform_block;

    /// Output of the volumetric pre-pass for the composite.
    struct cloud_prepass_result
    {
        gfx::texture::ptr cloud;
        gfx::texture::ptr aux;
        usize32_t size{1, 1};
    };

    /// Volumetric pre-pass: half-res march + temporal accumulation into the ping-pong
    /// history owned by the render view. Returns the textures the composite pass reads.
    auto run_cloud_prepass(const camera& camera,
                           gfx::render_view& rview,
                           const usize32_t& output_size,
                           const gfx::texture::ptr& depth,
                           const irradiance_perez_params& perez,
                           const cloud_uniform_block& uniforms,
                           const run_params& params) -> cloud_prepass_result;

    /// Blends the pre-pass result over the whole frame with a depth-aware upsample.
    void run_cloud_composite(gfx::frame_buffer* surface,
                             const camera& camera,
                             const usize32_t& output_size,
                             const gfx::texture::ptr& depth,
                             const cloud_prepass_result& prepass);

    /// World y of the layer base for the altitude mode of `params`.
    static auto layer_base_world_y(const run_params& params, const camera& camera) -> float;

    /// Drops the per-view cloud history when the volumetric path is not in use.
    static void release_cloud_resources(gfx::render_view& rview);

    std::unique_ptr<gfx::vertex_buffer> vb_;
    std::unique_ptr<gfx::index_buffer> ib_;

    detail::sun_controller sun_;
};
} // namespace unravel
