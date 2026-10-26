#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gpu_program.h>
#include <engine/rendering/pipeline/pre_exposure.h>
#include <graphics/frame_buffer.h>
#include <graphics/render_view.h>
#include <math/color.h>

#include <array>
#include <vector>

namespace unravel
{

namespace shadow
{
class shadowmap_generator;
} // namespace shadow

/// Froxel grid resolution of the volumetric fog (fog_pass::get_volumetric_grid).
enum class volumetric_fog_quality : std::uint8_t
{
    /// 72 cells high, 64 slices (128x72x64 at 16:9).
    low,
    /// 90 cells high, 64 slices (160x90x64 at 16:9).
    medium,
    /// 135 cells high, 64 slices (240x135x64 at 16:9).
    high,
    /// 135 cells high, 128 slices (240x135x128 at 16:9).
    ultra,
};

/// The shape of a fog volume (fog_pass::volume_settings).
enum class fog_volume_shape : std::uint8_t
{
    box,
    ellipsoid,
};

/// What a fog volume does to the fog it overlaps.
enum class fog_volume_mode : std::uint8_t
{
    /// Adds fog of its own color.
    add,
    /// Carves the fog out (volumetric fog only).
    subtract,
};

/**
 * @brief Exponential height fog, composited over the lit scene after the sky and clouds.
 *
 * The fog's density is constant below @ref settings::base_height and falls off exponentially above it. Two
 * layers render it:
 *
 * - **Analytic** (always): the optical depth along each view ray is integrated in closed form (fog/fog.sh), lit by
 *   the sun (Henyey-Greenstein phase) and the sky (the view's environment SH convolved with the same phase),
 *   unshadowed. One full-screen draw.
 * - **Volumetric** (@ref settings::volumetric): a camera-frustum grid of froxels out to
 *   @ref settings::volumetric_distance. A compute pass lights each froxel with the sun through its shadow cascades and
 *   the cloud shadow, the sky and the point and spot lights (@ref local_light, shadowed by their tiles in the shadow
 *   atlas), and blends it with the froxel's own history (no TAA needed). Each froxel visits only the lights that a
 *   culling dispatch found reaching its cluster of froxels, and keeps less history where a light changed since the
 *   previous frame (@ref find_changed_lights), so moving lights leave no lasting trail; a second
 *   integrates each column front to back (Hillaire's energy-conserving slab integral). The composite reads the column
 *   at each pixel's depth and continues with the analytic fog past the grid, from the same density, so the two meet
 *   without a seam.
 *
 * Fog volumes (@ref fog_volume, fog_volume_component) add boxes and ellipsoids of fog: the froxels sample them, the
 * analytic fog integrates their chords, from the same profile.
 *
 * Particles draw after the composite and fog themselves with the same uniforms (@ref bind_uniforms); unlit particles
 * read the froxel column too (@ref bind_particle_volume).
 */
class fog_pass
{
public:
    struct settings
    {
        /// Distance (m) over which the fog at and below the base height dims light to 1/e: its mean free path.
        /// Lower is denser.
        float attenuation_distance = 400.0f;
        /// World height (m) below which the fog has its full density.
        float base_height = 0.0f;
        /// World height (m) where the density has fallen to 0.1% of the base density. The density fades
        /// exponentially between the two heights.
        float maximum_height = 50.0f;
        /// The share of the light the fog scatters instead of absorbing, per channel (linear after the
        /// color's conversion): white fog scatters everything, darker fog looks like smoke.
        math::color albedo{1.0f, 1.0f, 1.0f, 1.0f};
        /// Henyey-Greenstein anisotropy: 0 scatters evenly in every direction, toward 1 a glow around the sun
        /// when looking toward it, below 0 a glow when looking away from it.
        float anisotropy = 0.2f;
        /// The most of the scene the fog may hide (0..1): 1 lets distant geometry vanish, lower keeps it
        /// visible through any amount of fog.
        float max_opacity = 1.0f;
        /// Distance (m) from the camera where the fog starts.
        float start_distance = 0.0f;
        /// Scale on the sun light the fog scatters.
        float sun_scattering = 1.0f;
        /// Scale on the sky light the fog scatters.
        float ambient_scattering = 1.0f;
        /// How much the fog covers the sky (0..1).
        float sky_affect = 1.0f;
        /// Distance (m) the sky is fogged at, as if it were that far away. Fog along an upward ray thins with
        /// height, so the sky above the fog stays clear.
        float sky_distance = 5000.0f;
        /// Froxel fog near the camera, shadowed by the sun's cascades and the clouds. Off, or where the view's
        /// feature limits or GPU do not allow it, the analytic fog covers every distance.
        bool volumetric = false;
        /// View depth (m) the volumetric fog covers; the analytic fog continues past it.
        float volumetric_distance = 64.0f;
        /// Froxel grid resolution.
        volumetric_fog_quality volumetric_quality = volumetric_fog_quality::medium;
        /// How the slices spread over the distance: 0 packs them near the camera, 1 spaces them evenly.
        float volumetric_slice_uniformity = 0.75f;
        /// Share of a froxel's lighting kept from the previous frames (0..0.98): higher is smoother and less noisy,
        /// lower follows moving light and shadows faster.
        float volumetric_history = 0.9f;
    };

    /// The sun the fog scatters: the strongest enabled directional light.
    struct sun_light
    {
        /// Direction toward the sun (world, unit).
        math::vec3 direction{0.0f, 1.0f, 0.0f};
        /// Illuminance (lux) per channel, linear: the light's color times its intensity. Zero without a sun.
        math::vec3 illuminance{0.0f, 0.0f, 0.0f};
        /// The sun's shadow cascades, which the volumetric fog reads; null without them.
        const shadow::shadowmap_generator* shadow = nullptr;
        /// Cascades of @ref shadow.
        std::uint32_t shadow_cascades = 0;
        /// Depth the cascades store at a receiver beyond its own (0.5 for linear-depth maps, see the lit particles).
        float shadow_depth_offset = 0.0f;
    };

    /// A point or spot light the volumetric fog scatters.
    struct local_light
    {
        /// The same from frame to frame (the light's entity): tells which light changed since the previous frame.
        std::uint32_t id = 0;
        /// The light's shadow map, when it sits in the shadow atlas and holds this frame's casters; null: unshadowed.
        const shadow::shadowmap_generator* shadow = nullptr;
        /// World position.
        math::vec3 position{0.0f};
        /// Distance (m) the light reaches.
        float range = 0.0f;
        /// The axis a spot light shines along (world, unit).
        math::vec3 direction{0.0f, 0.0f, 1.0f};
        bool is_spot = false;
        /// Cosines of a spot light's inner and outer half angles.
        float cos_inner = 1.0f;
        float cos_outer = 0.0f;
        /// Linear color x luminous intensity (cd) x the light's volumetric intensity.
        math::vec3 intensity{0.0f};
    };

    /// Point and spot lights the volumetric fog scatters at most: one bit each in a cluster's light mask
    /// (fog/fog_lights.sh FOG_MAX_LOCAL_LIGHTS).
    static constexpr std::uint32_t max_local_lights = 32;

    /// A fog volume's own settings (fog_volume_component).
    struct volume_settings
    {
        fog_volume_shape shape = fog_volume_shape::box;
        fog_volume_mode mode = fog_volume_mode::add;
        /// Half extents of the box, or the ellipsoid's radii (m), along the entity's local axes; its scale stretches
        /// them.
        math::vec3 extents{5.0f, 5.0f, 5.0f};
        /// Distance (m) over which the volume's fog dims light to 1/e at full density. Lower is denser.
        float attenuation_distance = 20.0f;
        /// The share of the light the volume's fog scatters, per channel (linear after the color's conversion).
        math::color albedo{1.0f, 1.0f, 1.0f, 1.0f};
        /// Share of the extents (0..1) over which the fog fades in from the surface: 0 is a hard edge.
        float edge_fade = 0.3f;
        /// How much noise breaks up the density (0..1, volumetric fog only): 0 is uniform.
        float noise_strength = 0.0f;
        /// Size (m) of the noise's features.
        float noise_size = 8.0f;
    };

    /// A fog volume in the world.
    struct fog_volume
    {
        /// The volume entity's world transform.
        math::transform world;
        volume_settings settings{};
    };

    /// Fog volumes the fog reads at most (fog.sh FOG_MAX_VOLUMES) and the vec4s each takes (FOG_VOLUME_VEC4S).
    static constexpr std::uint32_t max_fog_volumes = 16;
    static constexpr std::uint32_t fog_volume_vec4s = 5;
    using packed_volumes = std::array<math::vec4, max_fog_volumes * fog_volume_vec4s>;

    /// The cloud layer's shadow on the sun (cloud_shadow.sh), which the volumetric fog reads.
    struct cloud_shadow
    {
        gfx::texture::ptr map;
        /// u_cloudShadow: xy = map origin, z = 1 / extent, w = opacity.
        math::vec4 placement{0.0f};
        /// u_cloudShadow2: x = enabled, y = layer base height, z = border fade.
        math::vec4 layer{0.0f};
    };

    /// Froxel grid dimensions: columns across, rows down, slices deep.
    struct volumetric_grid
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t depth = 0;

        auto operator==(const volumetric_grid& other) const -> bool = default;
    };

    /**
     * @brief The fog as fog/fog.sh reads it (the u_fog_* uniforms), shared by the composite and the particles.
     *
     * A default-constructed value is fog off: the shaders skip the fog when @ref density x is 0.
     */
    struct shader_params
    {
        /// x = extinction at and below the base height (1/m), y = base height (m), z = 1 / scale height (1/m),
        /// w = the least transmittance the fog leaves (1 - max opacity).
        math::vec4 density{0.0f, 0.0f, 0.0f, 0.0f};
        /// x = start distance (m), y = sky distance (m), z = sky affect, w = phase anisotropy.
        math::vec4 range{0.0f, 0.0f, 0.0f, 0.0f};
        /// rgb = sun illuminance x sun scattering (lux, linear, absolute), w = ambient scattering: the light a unit
        /// of scattering receives, before any albedo.
        math::vec4 light{0.0f, 0.0f, 0.0f, 0.0f};
        /// xyz = direction toward the sun.
        math::vec4 sun_direction{0.0f, 1.0f, 0.0f, 0.0f};
        /// rgb = the height fog's albedo (linear).
        math::vec4 albedo{0.0f, 0.0f, 0.0f, 0.0f};
        /// xyz = camera world position.
        math::vec4 camera{0.0f, 0.0f, 0.0f, 0.0f};
        /// xyz = the camera's forward axis (world, unit): view depth = offset . forward.
        math::vec4 forward{0.0f, 0.0f, 1.0f, 0.0f};
        /// The froxel column: x = view depth it covers (m; 0 = no volumetric fog), y = slice distribution constant,
        /// z = 1 / log(y x + 1), w = slices.
        math::vec4 volume{0.0f, 0.0f, 0.0f, 0.0f};
        /// The integrated froxel column (pre-exposed in-scatter, transmittance) when @ref volume x is above 0.
        gfx::texture::ptr volume_texture;
        /// x = fog volumes in @ref volumes.
        math::vec4 volume_info{0.0f, 0.0f, 0.0f, 0.0f};
        /// The fog volumes, farthest first (@ref pack_fog_volumes).
        packed_volumes volumes{};

        auto is_enabled() const -> bool
        {
            return density.x > 0.0f;
        }

        auto is_volumetric() const -> bool
        {
            return volume.x > 0.0f && volume_texture != nullptr;
        }
    };

    struct run_params
    {
        settings config{};
        /// The lit scene, colour only: the composite samples the depth the depth attachment holds.
        gfx::frame_buffer::ptr target;
        /// The scene's device depth.
        gfx::texture::ptr depth;
        /// The view's environment SH (IRRADIANCE_SH): the sky the fog scatters, and in texel 9 the GPU's share of
        /// the pre-exposure.
        gfx::texture::ptr environment;
        pre_exposure_state pre_exposure{};
        sun_light sun{};
        cloud_shadow clouds{};
        /// The scene's point and spot lights that scatter in the fog; the volumetric fog picks the ones it reaches
        /// (@ref pick_local_lights).
        std::vector<local_light> local_lights;
        /// The fog volumes in the view; the fog keeps the nearest (@ref pick_fog_volumes).
        std::vector<fog_volume> volumes;
        /// The view may render volumetric fog: its feature limits allow it (the settings still ask for it).
        bool allows_volumetric = false;
        /// The camera jumped this frame: the froxels drop their history.
        bool is_camera_cut = false;
    };

    auto init(rtti::context& ctx) -> bool;

    /// Whether the device runs the volumetric fog (render_feature::volumetric_fog: compute, the froxel lighting's
    /// compute bindings and RGBA16F volumes it can write).
    static auto is_volumetric_supported() -> bool;

    /// The froxel grid of @p quality for a view of @p aspect (width / height).
    static auto get_volumetric_grid(volumetric_fog_quality quality, float aspect) -> volumetric_grid;

    /**
     * @brief The lights of @p lights whose reach comes within @p reach metres of @p camera_position, the brightest at
     * the camera first, at most @ref max_local_lights.
     */
    static auto pick_local_lights(std::vector<local_light> lights, const math::vec3& camera_position, float reach)
        -> std::vector<local_light>;

    /**
     * @brief Bit i set: light i of @p current (at most @ref max_local_lights) is new since @p previous, or moved, turned
     * or changed its reach or color there (matched by @ref local_light::id). The froxels such a light reaches keep less
     * history.
     */
    static auto find_changed_lights(const std::vector<local_light>& previous, const std::vector<local_light>& current)
        -> std::uint32_t;

    /// The nearest @ref max_fog_volumes of @p volumes to @p camera_position, the farthest first (the order the
    /// analytic fog inserts them in).
    static auto pick_fog_volumes(std::vector<fog_volume> volumes, const math::vec3& camera_position) -> std::vector<fog_volume>;

    /**
     * @brief u_fog_volumes for @p volumes, in their order, seen from @p camera_position: each volume's camera-relative
     * world to unit-shape matrix rows, (extinction, 1 / fade band, shape + 2 x mode, 1 / noise size) and (linear
     * albedo, noise strength).
     */
    static auto pack_fog_volumes(const std::vector<fog_volume>& volumes, const math::vec3& camera_position) -> packed_volumes;

    /// The shader parameters of @p config lit by @p sun, seen from @p camera_position (analytic layer only).
    static auto make_shader_params(const settings& config, const sun_light& sun, const math::vec3& camera_position)
        -> shader_params;

    /// Renders the fog over @p params.target: the froxels first when they are on; @return the shader parameters it
    /// fogged with.
    auto run(const camera& camera, gfx::render_view& rview, const run_params& params) -> shader_params;

    /// Frees @p rview's froxel volumes and history.
    void release_volumetric(gfx::render_view& rview);

    /// Sets the u_fog_* uniforms to @p params for the next draw.
    void bind_uniforms(const shader_params& params) const;

    /// Binds the integrated froxel column of @p params at @p stage for an unlit particle draw (a white stand-in
    /// without one: the sampler must hold a 3D texture).
    void bind_particle_volume(std::uint8_t stage, const shader_params& params) const;

private:
    /// Lights and integrates the froxels; @return the integrated column, null when the grid could not run.
    auto run_volumetric(const camera& camera, gfx::render_view& rview, const run_params& params, shader_params& shader)
        -> gfx::texture::ptr;

    struct fog_uniforms
    {
        void cache_uniforms();

        gfx::program::uniform_ptr u_fog_density;
        gfx::program::uniform_ptr u_fog_range;
        gfx::program::uniform_ptr u_fog_light;
        gfx::program::uniform_ptr u_fog_sun_direction;
        gfx::program::uniform_ptr u_fog_albedo;
        gfx::program::uniform_ptr u_fog_volume_info;
        gfx::program::uniform_ptr u_fog_volumes;
        gfx::program::uniform_ptr u_fog_camera;
        gfx::program::uniform_ptr u_fog_forward;
        gfx::program::uniform_ptr u_fog_volume;
        gfx::program::uniform_ptr s_particle_fog_volume;
    } fog_uniforms_;

    struct fog_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), s_fog_depth, "s_fog_depth", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_fog_environment, "s_fog_environment", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), s_fog_volume, "s_fog_volume", bgfx::UniformType::Sampler);
            cache_uniform(program.get(), u_pre_exposure, "u_pre_exposure", bgfx::UniformType::Vec4);
        }

        gfx::program::uniform_ptr s_fog_depth;
        gfx::program::uniform_ptr s_fog_environment;
        gfx::program::uniform_ptr s_fog_volume;
        gfx::program::uniform_ptr u_pre_exposure;
        std::unique_ptr<gpu_program> program;
        /// The same composite with the froxel column in front of the analytic fog (fs_fog_volumetric.sc).
        std::unique_ptr<gpu_program> volumetric_program;
    } fog_program_;

    /// The froxel lighting and integration dispatches (cs_fog_lighting.sc, cs_fog_integrate.sc).
    struct volumetric_programs : uniforms_cache
    {
        void cache_uniforms();

        gfx::program::uniform_ptr u_fog_grid;
        gfx::program::uniform_ptr u_fog_jitter;
        gfx::program::uniform_ptr u_fog_rays;
        gfx::program::uniform_ptr u_fog_prev_view_proj;
        gfx::program::uniform_ptr u_fog_prev_offset;
        gfx::program::uniform_ptr u_fog_shadow;
        gfx::program::uniform_ptr u_fog_light_info;
        gfx::program::uniform_ptr u_fog_lights;
        gfx::program::uniform_ptr u_pre_exposure;
        gfx::program::uniform_ptr u_cloud_shadow;
        gfx::program::uniform_ptr u_cloud_shadow_layer;
        gfx::program::uniform_ptr s_fog_history;
        gfx::program::uniform_ptr s_fog_lit;
        gfx::program::uniform_ptr s_fog_environment;
        gfx::program::uniform_ptr s_cloud_shadow;
        gfx::program::uniform_ptr s_fog_noise;
        gfx::program::uniform_ptr s_fog_shadow_atlas;
        /// The sun cascades' samplers and uniforms (shadowmap_generator::submit_uniforms sets them by name).
        std::array<gfx::program::uniform_ptr, 4> s_shadow_maps;
        std::array<gfx::program::uniform_ptr, 4> u_shadow_map_matrices;
        gfx::program::uniform_ptr u_params2;
        gfx::program::uniform_ptr u_shadow_bias_params;
        std::unique_ptr<gpu_program> lighting;
        std::unique_ptr<gpu_program> integrate;
        /// The point and spot lights per cluster of froxels, ahead of the lighting (cs_fog_cull.sc).
        std::unique_ptr<gpu_program> cull;
    } volumetric_;
};

} // namespace unravel
