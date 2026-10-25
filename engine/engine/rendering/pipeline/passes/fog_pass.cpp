#include "fog_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/shadow.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>

#include <algorithm>
#include <cmath>

namespace unravel
{

namespace
{
/// ln(1000): the density at the maximum height is 0.1% of the base density (settings::maximum_height).
constexpr float LOG_DENSITY_FALLOFF_AT_MAXIMUM_HEIGHT = 6.907755f;
/// Shortest mean free path (m): keeps the extinction finite.
constexpr float MIN_ATTENUATION_DISTANCE = 0.01f;
/// Thinnest falloff layer (m) between the base and the maximum height: keeps the inverse scale height finite.
constexpr float MIN_FALLOFF_THICKNESS = 0.01f;
/// Strongest anisotropy: the Henyey-Greenstein lobe turns into a spike at 1.
constexpr float MAX_ANISOTROPY = 0.95f;
/// Stages of the composite's samplers (fog/fog_composite.sh).
constexpr std::uint8_t FOG_DEPTH_STAGE = 0;
constexpr std::uint8_t FOG_ENVIRONMENT_STAGE = 1;
constexpr std::uint8_t FOG_VOLUME_STAGE = 2;
/// Stages of the froxel lighting (fog/cs_fog_lighting.sc). The images stay below 8 (GL image units).
constexpr std::uint8_t LIGHTING_OUTPUT_STAGE = 0;
constexpr std::uint8_t LIGHTING_HISTORY_STAGE = 2;
constexpr std::uint8_t LIGHTING_ENVIRONMENT_STAGE = 3;
constexpr std::uint8_t LIGHTING_CLOUD_SHADOW_STAGE = 4;
constexpr std::uint8_t LIGHTING_SHADOW_STAGE = 5;
constexpr std::uint8_t LIGHTING_NOISE_STAGE = 9;
constexpr std::uint8_t LIGHTING_SHADOW_ATLAS_STAGE = 10;
constexpr std::uint8_t LIGHTING_LIGHT_SHADOWS_STAGE = 11;
constexpr std::uint8_t LIGHTING_LIGHT_MASKS_STAGE = 12;
/// Stage of the froxel light culling's masks (fog/cs_fog_cull.sc).
constexpr std::uint8_t CULL_MASKS_STAGE = 0;
/// Stages of the froxel integration (fog/cs_fog_integrate.sc).
constexpr std::uint8_t INTEGRATE_OUTPUT_STAGE = 0;
constexpr std::uint8_t INTEGRATE_LIT_STAGE = 1;
/// Threads per group along each axis of the lighting and of the culling (4x4x4), and per column group of the
/// integration (8x8).
constexpr std::uint32_t LIGHTING_GROUP_SIZE = 4;
constexpr std::uint32_t CULL_GROUP_SIZE = 4;
constexpr std::uint32_t INTEGRATE_GROUP_SIZE = 8;
/// Froxels across and slices deep of a light cluster (fog/fog_lights.sh FOG_CLUSTER_COLUMNS, FOG_CLUSTER_SLICES).
constexpr std::uint32_t CLUSTER_COLUMNS = 8;
constexpr std::uint32_t CLUSTER_SLICES = 8;
/// Cascades the lighting reads (s_shadowMap0..3).
constexpr std::uint32_t MAX_SHADOW_CASCADES = 4;
/// Largest history weight: a froxel always takes some of each frame, so it never freezes.
constexpr float MAX_HISTORY_WEIGHT = 0.98f;
/// Frames of the Halton(2, 3, 5) jitter before it repeats.
constexpr std::uint32_t JITTER_SEQUENCE_LENGTH = 16;
/// Slice distribution constant (1/m) of evenly spaced slices: c -> 0 is linear.
constexpr float MIN_SLICE_DISTRIBUTION = 0.001f;
/// Shortest froxel column (m).
constexpr float MIN_VOLUMETRIC_DISTANCE = 1.0f;
/// Columns a grid keeps across, whatever the view's aspect.
constexpr std::uint32_t MIN_GRID_COLUMNS = 16;
constexpr std::uint32_t MAX_GRID_COLUMNS = 512;
/// Aspect of a view without a size.
constexpr float DEFAULT_ASPECT = 16.0f / 9.0f;
/// The render view's froxel volumes: the two lit volumes alternate between this frame's and the history.
constexpr std::array<const char*, 2> LIT_VOLUME_KEYS{"FOG_LIT_0", "FOG_LIT_1"};
constexpr const char* INTEGRATED_VOLUME_KEY = "FOG_INTEGRATED";
/// vec4s per light in u_fog_lights.
constexpr std::uint32_t LOCAL_LIGHT_VEC4S = 3;
/// The changed-light mask travels in two floats of u_fog_light_info, 16 bits each (exact in a float).
constexpr std::uint32_t CHANGED_LIGHTS_HALF_BITS = 16;
constexpr std::uint32_t CHANGED_LIGHTS_HALF_MASK = 0xFFFFu;
/// A point light's cos outer half angle: below every dot product, so its cone mask is 1.
constexpr float POINT_LIGHT_COS_OUTER = -2.0f;
/// vec4s per light in b_fog_light_shadows: the atlas rect, the parameters, then one matrix per map.
constexpr std::uint32_t LIGHT_SHADOW_VEC4S = 18;
constexpr std::uint32_t LIGHT_SHADOW_MATRICES_OFFSET = 2;
/// Maps of a spot light and of a point light (its tetrahedron faces).
constexpr std::uint8_t SPOT_SHADOW_MAPS = 1;
constexpr std::uint8_t POINT_SHADOW_MAPS = 4;
/// The kind of a light's shadow record (cs_fog_lighting.sc): none (unshadowed), a spot map, a point light's faces.
constexpr float UNSHADOWED_LIGHT = 0.0f;
constexpr float SPOT_LIGHT_SHADOW = 1.0f;
constexpr float POINT_LIGHT_SHADOW = 2.0f;
/// Smallest range and spot cone width a light is packed with: keeps the inverses finite.
constexpr float MIN_LIGHT_RANGE = 1.0e-4f;
constexpr float MIN_SPOT_CONE_WIDTH = 1.0e-4f;
/// Smallest fog volume extent, edge fade band and noise size: keep the inverses finite.
constexpr float MIN_VOLUME_EXTENT = 1.0e-3f;
constexpr float MIN_EDGE_FADE_BAND = 1.0e-3f;
constexpr float MIN_NOISE_SIZE = 0.01f;

/// A render view's froxel history (gfx::render_view::data()).
struct volumetric_view_state
{
    static constexpr const char* view_key = "VOLUMETRIC_FOG";

    /// Frames lit since the history started: the jitter's index.
    std::uint32_t frame = 0;
    /// The lit volume (LIT_VOLUME_KEYS) written last.
    std::uint32_t current = 0;
    bool has_history = false;
    /// The layout the history was lit in: a different one drops it.
    fog_pass::volumetric_grid grid{};
    float range = 0.0f;
    float slice_distribution = 0.0f;
    /// The point and spot lights lit last frame: a light that differs from its entry here changed.
    std::vector<fog_pass::local_light> lights;
};

/// Layout of b_fog_light_shadows: a flat array of vec4 (BUFFER_RO(_, vec4, _)).
auto get_vec4_buffer_layout() -> const bgfx::VertexLayout&
{
    static const bgfx::VertexLayout layout = []()
    {
        bgfx::VertexLayout vec4_layout;
        vec4_layout.begin().add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float).end();
        return vec4_layout;
    }();
    return layout;
}

/// Light clusters of @p grid: along x, y and z, and in all (fog/fog_lights.sh fog_cluster_counts).
struct cluster_counts
{
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t z = 0;

    auto get_total() const -> std::uint32_t
    {
        return x * y * z;
    }
};

auto get_cluster_counts(const fog_pass::volumetric_grid& grid) -> cluster_counts
{
    return {(grid.width + CLUSTER_COLUMNS - 1u) / CLUSTER_COLUMNS,
            (grid.height + CLUSTER_COLUMNS - 1u) / CLUSTER_COLUMNS,
            (grid.depth + CLUSTER_SLICES - 1u) / CLUSTER_SLICES};
}

/**
 * A render view's data of the lights its froxels scatter: their shadow records (b_fog_light_shadows) and the light
 * mask of each cluster (b_fog_light_masks, which the culling writes on the GPU). One set per view: bgfx applies
 * every buffer update of a frame before any view renders, so views sharing the records would all read the last's.
 */
struct local_light_buffers
{
    static constexpr const char* view_key = "VOLUMETRIC_FOG_LOCAL_LIGHTS";

    local_light_buffers()
        : shadows(bgfx::createDynamicVertexBuffer(fog_pass::max_local_lights * LIGHT_SHADOW_VEC4S,
                                                  get_vec4_buffer_layout(),
                                                  BGFX_BUFFER_COMPUTE_READ))
    {
    }

    ~local_light_buffers()
    {
        if(bgfx::isValid(shadows))
        {
            bgfx::destroy(shadows);
        }
        if(bgfx::isValid(masks))
        {
            bgfx::destroy(masks);
        }
    }

    local_light_buffers(const local_light_buffers&) = delete;
    auto operator=(const local_light_buffers&) -> local_light_buffers& = delete;

    /// The masks, with room for @p clusters of them; recreated when the grid outgrows them.
    auto get_masks(std::uint32_t clusters) -> bgfx::DynamicIndexBufferHandle
    {
        if(bgfx::isValid(masks) && clusters <= mask_capacity)
        {
            return masks;
        }
        if(bgfx::isValid(masks))
        {
            bgfx::destroy(masks);
        }
        mask_capacity = clusters;
        masks = bgfx::createDynamicIndexBuffer(clusters, BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_INDEX32);
        return masks;
    }

    bgfx::DynamicVertexBufferHandle shadows{bgfx::kInvalidHandle};
    bgfx::DynamicIndexBufferHandle masks{bgfx::kInvalidHandle};
    std::uint32_t mask_capacity = 0;
};

/// Element @p index (from 1) of the Halton sequence of @p base, in [0, 1).
auto get_halton(std::uint32_t index, std::uint32_t base) -> float
{
    float result = 0.0f;
    float fraction = 1.0f / float(base);
    while(index > 0)
    {
        result += fraction * float(index % base);
        index /= base;
        fraction /= float(base);
    }
    return result;
}

/// The slice distribution constant c (1/m) of @p uniformity (fog/fog.sh fog_slice_depth).
auto get_slice_distribution(float uniformity) -> float
{
    return math::max(2.0f - 2.0f * math::clamp(uniformity, 0.0f, 1.0f), MIN_SLICE_DISTRIBUTION);
}

/**
 * Camera-relative world rays through the grid's corners at view depth 1 (fog/fog_volume.sh u_fog_rays): screen uv
 * (0, 0), (1, 0), (0, 1), (1, 1), with uv row 0 where the render targets keep their first row.
 */
auto get_grid_corner_rays(const camera& camera) -> std::array<math::vec4, 4>
{
    const math::mat4 inverse_projection = glm::inverse(camera.get_projection_unjittered().get_matrix());
    const math::mat4 view_to_world = camera.get_view_inverse_relative().get_matrix();
    const bool is_origin_bottom_left = bgfx::getCaps()->originBottomLeft;
    const std::array<math::vec2, 4> corners{math::vec2(0.0f, 0.0f),
                                                math::vec2(1.0f, 0.0f),
                                                math::vec2(0.0f, 1.0f),
                                                math::vec2(1.0f, 1.0f)};
    // Inside the zero-to-one depth range of every projection the engine builds.
    constexpr float ndc_depth = 0.5f;
    std::array<math::vec4, 4> rays{};
    for(size_t i = 0; i < corners.size(); ++i)
    {
        const float ndc_x = corners[i].x * 2.0f - 1.0f;
        const float ndc_y = is_origin_bottom_left ? corners[i].y * 2.0f - 1.0f : 1.0f - corners[i].y * 2.0f;
        const math::vec4 point = inverse_projection * math::vec4(ndc_x, ndc_y, ndc_depth, 1.0f);
        math::vec3 view_ray = math::vec3(point) / point.w;
        view_ray /= std::abs(view_ray.z);
        rays[i] = view_to_world * math::vec4(view_ray, 0.0f);
    }
    return rays;
}

/// u_fog_lights for @p lights, with positions relative to @p camera_position.
auto pack_local_lights(const std::vector<fog_pass::local_light>& lights, const math::vec3& camera_position)
    -> std::array<math::vec4, fog_pass::max_local_lights * LOCAL_LIGHT_VEC4S>
{
    std::array<math::vec4, fog_pass::max_local_lights * LOCAL_LIGHT_VEC4S> packed{};
    for(size_t i = 0; i < lights.size() && i < fog_pass::max_local_lights; ++i)
    {
        const auto& light = lights[i];
        const float cos_outer = light.is_spot ? light.cos_outer : POINT_LIGHT_COS_OUTER;
        const float inverse_cone_width =
            light.is_spot ? 1.0f / math::max(light.cos_inner - light.cos_outer, MIN_SPOT_CONE_WIDTH) : 1.0f;
        packed[i * LOCAL_LIGHT_VEC4S] = math::vec4(light.position - camera_position, 1.0f / math::max(light.range, MIN_LIGHT_RANGE));
        packed[i * LOCAL_LIGHT_VEC4S + 1] = math::vec4(light.direction, cos_outer);
        packed[i * LOCAL_LIGHT_VEC4S + 2] = math::vec4(light.intensity, inverse_cone_width);
    }
    return packed;
}

/**
 * b_fog_light_shadows for @p lights, LIGHT_SHADOW_VEC4S per light in their order: (the map's rect in the shadow atlas,
 * uv), (kind, 1 for a linear-depth map, half a texel of the map in its own uv, 0), then the world -> map matrix of each
 * map by columns (shadowmap_generator::get_world_to_map). World space: the same for every view of the frame.
 */
auto pack_light_shadows(const std::vector<fog_pass::local_light>& lights) -> std::vector<math::vec4>
{
    const size_t count = math::min(lights.size(), size_t(fog_pass::max_local_lights));
    std::vector<math::vec4> packed(count * LIGHT_SHADOW_VEC4S, math::vec4(0.0f));
    for(size_t i = 0; i < count; ++i)
    {
        const auto& light = lights[i];
        const auto tile = light.shadow ? light.shadow->get_atlas_tile() : hpp::optional<shadow::shadow_atlas_tile>{};
        if(!tile)
        {
            packed[i * LIGHT_SHADOW_VEC4S + 1].x = UNSHADOWED_LIGHT;
            continue;
        }
        auto* record = packed.data() + i * LIGHT_SHADOW_VEC4S;
        record[0] = light.shadow->get_map_uv_rect();
        record[1] = math::vec4(light.is_spot ? SPOT_LIGHT_SHADOW : POINT_LIGHT_SHADOW,
                               light.shadow->has_linear_depth() ? 1.0f : 0.0f,
                               0.5f / float(tile->size),
                               0.0f);
        const std::uint8_t maps = light.is_spot ? SPOT_SHADOW_MAPS : POINT_SHADOW_MAPS;
        for(std::uint8_t map = 0; map < maps; ++map)
        {
            const math::mat4 world_to_map = light.shadow->get_world_to_map(map);
            for(int column = 0; column < 4; ++column)
            {
                record[LIGHT_SHADOW_MATRICES_OFFSET + map * 4 + column] = world_to_map[column];
            }
        }
    }
    return packed;
}

auto get_group_count(std::uint32_t items, std::uint32_t group_size) -> std::uint32_t
{
    return (items + group_size - 1u) / group_size;
}
} // namespace

void fog_pass::fog_uniforms::cache_uniforms()
{
    u_fog_density = std::make_shared<gfx::uniform>("u_fog_density", bgfx::UniformType::Vec4);
    u_fog_range = std::make_shared<gfx::uniform>("u_fog_range", bgfx::UniformType::Vec4);
    u_fog_light = std::make_shared<gfx::uniform>("u_fog_light", bgfx::UniformType::Vec4);
    u_fog_sun_direction = std::make_shared<gfx::uniform>("u_fog_sun_direction", bgfx::UniformType::Vec4);
    u_fog_albedo = std::make_shared<gfx::uniform>("u_fog_albedo", bgfx::UniformType::Vec4);
    u_fog_volume_info = std::make_shared<gfx::uniform>("u_fog_volume_info", bgfx::UniformType::Vec4);
    u_fog_volumes = std::make_shared<gfx::uniform>("u_fog_volumes",
                                                   bgfx::UniformType::Vec4,
                                                   std::uint16_t(max_fog_volumes * fog_volume_vec4s));
    u_fog_camera = std::make_shared<gfx::uniform>("u_fog_camera", bgfx::UniformType::Vec4);
    u_fog_forward = std::make_shared<gfx::uniform>("u_fog_forward", bgfx::UniformType::Vec4);
    u_fog_volume = std::make_shared<gfx::uniform>("u_fog_volume", bgfx::UniformType::Vec4);
    s_particle_fog_volume = std::make_shared<gfx::uniform>("s_particle_fog_volume", bgfx::UniformType::Sampler);
}

void fog_pass::volumetric_programs::cache_uniforms()
{
    cache_uniform(lighting.get(), u_fog_grid, "u_fog_grid", bgfx::UniformType::Vec4);
    cache_uniform(lighting.get(), u_fog_jitter, "u_fog_jitter", bgfx::UniformType::Vec4);
    cache_uniform(lighting.get(), u_fog_rays, "u_fog_rays", bgfx::UniformType::Vec4, 4);
    cache_uniform(lighting.get(), u_fog_prev_view_proj, "u_fog_prev_view_proj", bgfx::UniformType::Mat4);
    cache_uniform(lighting.get(), u_fog_prev_offset, "u_fog_prev_offset", bgfx::UniformType::Vec4);
    cache_uniform(lighting.get(), u_fog_shadow, "u_fog_shadow", bgfx::UniformType::Vec4);
    cache_uniform(lighting.get(), u_fog_light_info, "u_fog_light_info", bgfx::UniformType::Vec4);
    cache_uniform(lighting.get(),
                  u_fog_lights,
                  "u_fog_lights",
                  bgfx::UniformType::Vec4,
                  std::uint16_t(max_local_lights * LOCAL_LIGHT_VEC4S));
    cache_uniform(lighting.get(), u_pre_exposure, "u_pre_exposure", bgfx::UniformType::Vec4);
    cache_uniform(lighting.get(), u_cloud_shadow, "u_cloudShadow", bgfx::UniformType::Vec4);
    cache_uniform(lighting.get(), u_cloud_shadow_layer, "u_cloudShadow2", bgfx::UniformType::Vec4);
    cache_uniform(lighting.get(), s_fog_history, "s_fog_history", bgfx::UniformType::Sampler);
    cache_uniform(integrate.get(), s_fog_lit, "s_fog_lit", bgfx::UniformType::Sampler);
    cache_uniform(lighting.get(), s_fog_environment, "s_fog_environment", bgfx::UniformType::Sampler);
    cache_uniform(lighting.get(), s_cloud_shadow, "s_cloudShadow", bgfx::UniformType::Sampler);
    cache_uniform(lighting.get(), s_fog_shadow_atlas, "s_fog_shadow_atlas", bgfx::UniformType::Sampler);
    cache_uniform(lighting.get(), s_fog_noise, "s_fog_noise", bgfx::UniformType::Sampler);
    for(size_t i = 0; i < s_shadow_maps.size(); ++i)
    {
        cache_uniform(lighting.get(), s_shadow_maps[i], "s_shadowMap" + std::to_string(i), bgfx::UniformType::Sampler);
        cache_uniform(lighting.get(),
                      u_shadow_map_matrices[i],
                      "u_shadowMapMtx" + std::to_string(i),
                      bgfx::UniformType::Mat4);
    }
    cache_uniform(lighting.get(), u_params2, "u_params2", bgfx::UniformType::Vec4);
    cache_uniform(lighting.get(), u_shadow_bias_params, "u_shadowBiasParams", bgfx::UniformType::Vec4);
}

auto fog_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    auto fs_fog = am.get_asset<gfx::shader>("engine:/data/shaders/fog/fs_fog.sc");
    auto fs_fog_volumetric = am.get_asset<gfx::shader>("engine:/data/shaders/fog/fs_fog_volumetric.sc");
    auto cs_fog_lighting = am.get_asset<gfx::shader>("engine:/data/shaders/fog/cs_fog_lighting.sc");
    auto cs_fog_integrate = am.get_asset<gfx::shader>("engine:/data/shaders/fog/cs_fog_integrate.sc");
    auto cs_fog_cull = am.get_asset<gfx::shader>("engine:/data/shaders/fog/cs_fog_cull.sc");
    // Uniforms before programs (GL uniform order contract, gpu_program.h). The particle programs read the
    // u_fog_* uniforms too: the pipeline initializes this pass before it creates them.
    fog_uniforms_.cache_uniforms();
    fog_program_.cache_uniforms();
    volumetric_.cache_uniforms();
    fog_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_fog);
    fog_program_.volumetric_program = std::make_unique<gpu_program>(vs_clip_quad, fs_fog_volumetric);
    volumetric_.lighting = std::make_unique<gpu_program>(cs_fog_lighting);
    volumetric_.integrate = std::make_unique<gpu_program>(cs_fog_integrate);
    volumetric_.cull = std::make_unique<gpu_program>(cs_fog_cull);
    return fog_program_.program->is_valid();
}

auto fog_pass::is_volumetric_supported() -> bool
{
    const auto* caps = bgfx::getCaps();
    constexpr std::uint32_t volume_caps = BGFX_CAPS_FORMAT_TEXTURE_3D | BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE;
    const std::uint32_t format_caps = caps->formats[bgfx::TextureFormat::RGBA16F];
    return (caps->supported & BGFX_CAPS_COMPUTE) != 0 && (format_caps & volume_caps) == volume_caps;
}

auto fog_pass::get_volumetric_grid(volumetric_fog_quality quality, float aspect) -> volumetric_grid
{
    struct grid_preset
    {
        std::uint32_t rows;
        std::uint32_t slices;
    };
    grid_preset preset{90, 64};
    switch(quality)
    {
        case volumetric_fog_quality::low:
            preset = {72, 64};
            break;
        case volumetric_fog_quality::medium:
            preset = {90, 64};
            break;
        case volumetric_fog_quality::high:
            preset = {135, 64};
            break;
        case volumetric_fog_quality::ultra:
            preset = {135, 128};
            break;
    }
    const float safe_aspect = std::isfinite(aspect) && aspect > 0.0f ? aspect : DEFAULT_ASPECT;
    const auto columns = static_cast<std::uint32_t>(std::lround(float(preset.rows) * safe_aspect));
    return {math::clamp(columns, MIN_GRID_COLUMNS, MAX_GRID_COLUMNS), preset.rows, preset.slices};
}

auto fog_pass::pick_local_lights(std::vector<local_light> lights, const math::vec3& camera_position, float reach)
    -> std::vector<local_light>
{
    const auto is_out_of_reach = [&](const local_light& light)
    {
        return math::distance(light.position, camera_position) - light.range > reach;
    };
    lights.erase(std::remove_if(lights.begin(), lights.end(), is_out_of_reach), lights.end());
    // Brightest at the camera: the luminous intensity over the squared distance, not closer than a metre.
    const auto get_score = [&](const local_light& light)
    {
        const float luminance = 0.2126f * light.intensity.x + 0.7152f * light.intensity.y + 0.0722f * light.intensity.z;
        const math::vec3 to_light = light.position - camera_position;
        return luminance / math::max(math::dot(to_light, to_light), 1.0f);
    };
    std::stable_sort(lights.begin(),
                     lights.end(),
                     [&](const local_light& lhs, const local_light& rhs)
                     {
                         return get_score(lhs) > get_score(rhs);
                     });
    if(lights.size() > max_local_lights)
    {
        lights.resize(max_local_lights);
    }
    return lights;
}

auto fog_pass::find_changed_lights(const std::vector<local_light>& previous, const std::vector<local_light>& current)
    -> std::uint32_t
{
    // What the froxels see of a light: its placement, reach, cone and color. Exact comparisons: a light nothing
    // touched rebuilds the same values from the same transform every frame.
    const auto is_same = [](const local_light& lhs, const local_light& rhs)
    {
        return lhs.position == rhs.position && lhs.direction == rhs.direction && lhs.range == rhs.range &&
               lhs.is_spot == rhs.is_spot && lhs.cos_inner == rhs.cos_inner && lhs.cos_outer == rhs.cos_outer &&
               lhs.intensity == rhs.intensity;
    };
    std::uint32_t changed = 0;
    const size_t count = math::min(current.size(), size_t(max_local_lights));
    for(size_t i = 0; i < count; ++i)
    {
        const auto& light = current[i];
        const auto before = std::find_if(previous.begin(),
                                         previous.end(),
                                         [&](const local_light& candidate)
                                         {
                                             return candidate.id == light.id;
                                         });
        if(before == previous.end() || !is_same(*before, light))
        {
            changed |= 1u << i;
        }
    }
    return changed;
}

auto fog_pass::pick_fog_volumes(std::vector<fog_volume> volumes, const math::vec3& camera_position) -> std::vector<fog_volume>
{
    const auto get_distance = [&](const fog_volume& volume)
    {
        return math::distance(volume.world.get_position(), camera_position);
    };
    std::stable_sort(volumes.begin(),
                     volumes.end(),
                     [&](const fog_volume& lhs, const fog_volume& rhs)
                     {
                         return get_distance(lhs) < get_distance(rhs);
                     });
    if(volumes.size() > max_fog_volumes)
    {
        volumes.resize(max_fog_volumes);
    }
    std::reverse(volumes.begin(), volumes.end());
    return volumes;
}

auto fog_pass::pack_fog_volumes(const std::vector<fog_volume>& volumes, const math::vec3& camera_position) -> packed_volumes
{
    packed_volumes packed{};
    const math::mat4 from_camera = glm::translate(math::mat4(1.0f), camera_position);
    for(size_t i = 0; i < volumes.size() && i < max_fog_volumes; ++i)
    {
        const auto& settings = volumes[i].settings;
        const math::vec3 extents = math::max(settings.extents, math::vec3(MIN_VOLUME_EXTENT));
        // Camera-relative offset -> world -> the entity's local space -> the unit shape.
        const math::mat4 to_unit = glm::scale(math::mat4(1.0f), 1.0f / extents) *
                                   glm::inverse(volumes[i].world.get_matrix()) * from_camera;
        const size_t base = i * fog_volume_vec4s;
        for(int row = 0; row < 3; ++row)
        {
            packed[base + row] = math::vec4(to_unit[0][row], to_unit[1][row], to_unit[2][row], to_unit[3][row]);
        }
        const bool is_ellipsoid = settings.shape == fog_volume_shape::ellipsoid;
        const bool subtracts = settings.mode == fog_volume_mode::subtract;
        const float fade = math::clamp(settings.edge_fade, 0.0f, 1.0f);
        // An ellipsoid fades with 1 - |unit|^2 (fog.sh fog_volume_profile): its band starts where |unit| = 1 - fade.
        const float fade_band = is_ellipsoid ? 1.0f - (1.0f - fade) * (1.0f - fade) : fade;
        packed[base + 3] = math::vec4(1.0f / math::max(settings.attenuation_distance, MIN_ATTENUATION_DISTANCE),
                                      1.0f / math::max(fade_band, MIN_EDGE_FADE_BAND),
                                      (is_ellipsoid ? 1.0f : 0.0f) + (subtracts ? 2.0f : 0.0f),
                                      1.0f / math::max(settings.noise_size, MIN_NOISE_SIZE));
        const auto albedo = settings.albedo.to_linear().value;
        packed[base + 4] = math::vec4(albedo.x, albedo.y, albedo.z, math::clamp(settings.noise_strength, 0.0f, 1.0f));
    }
    return packed;
}

auto fog_pass::make_shader_params(const settings& config, const sun_light& sun, const math::vec3& camera_position)
    -> shader_params
{
    const float extinction = 1.0f / math::max(config.attenuation_distance, MIN_ATTENUATION_DISTANCE);
    const float falloff_thickness = math::max(config.maximum_height - config.base_height, MIN_FALLOFF_THICKNESS);
    const float inverse_scale_height = LOG_DENSITY_FALLOFF_AT_MAXIMUM_HEIGHT / falloff_thickness;
    const float min_transmittance = 1.0f - math::clamp(config.max_opacity, 0.0f, 1.0f);
    const auto albedo = config.albedo.to_linear().value;
    const math::vec3 albedo_rgb(albedo.x, albedo.y, albedo.z);
    shader_params params;
    params.density = math::vec4(extinction, config.base_height, inverse_scale_height, min_transmittance);
    params.range = math::vec4(math::max(config.start_distance, 0.0f),
                              math::max(config.sky_distance, 0.0f),
                              math::clamp(config.sky_affect, 0.0f, 1.0f),
                              math::clamp(config.anisotropy, -MAX_ANISOTROPY, MAX_ANISOTROPY));
    params.light = math::vec4(sun.illuminance * math::max(config.sun_scattering, 0.0f), math::max(config.ambient_scattering, 0.0f));
    params.sun_direction = math::vec4(sun.direction, 0.0f);
    params.albedo = math::vec4(albedo_rgb, 0.0f);
    params.camera = math::vec4(camera_position, 1.0f);
    return params;
}

void fog_pass::bind_uniforms(const shader_params& params) const
{
    gfx::set_uniform(fog_uniforms_.u_fog_density, params.density);
    gfx::set_uniform(fog_uniforms_.u_fog_range, params.range);
    gfx::set_uniform(fog_uniforms_.u_fog_light, params.light);
    gfx::set_uniform(fog_uniforms_.u_fog_sun_direction, params.sun_direction);
    gfx::set_uniform(fog_uniforms_.u_fog_albedo, params.albedo);
    gfx::set_uniform(fog_uniforms_.u_fog_volume_info, params.volume_info);
    gfx::set_uniform(fog_uniforms_.u_fog_volumes, params.volumes.data(), std::uint16_t(params.volumes.size()));
    gfx::set_uniform(fog_uniforms_.u_fog_camera, params.camera);
    gfx::set_uniform(fog_uniforms_.u_fog_forward, params.forward);
    gfx::set_uniform(fog_uniforms_.u_fog_volume, params.volume);
}

void fog_pass::bind_particle_volume(std::uint8_t stage, const shader_params& params) const
{
    const auto& volume = params.is_volumetric() ? params.volume_texture : default_textures::get().white_texture_3d();
    gfx::set_texture(fog_uniforms_.s_particle_fog_volume, stage, volume);
}

void fog_pass::release_volumetric(gfx::render_view& rview)
{
    for(const char* key : LIT_VOLUME_KEYS)
    {
        rview.tex_remove(key);
    }
    rview.tex_remove(INTEGRATED_VOLUME_KEY);
    rview.data().get_or_emplace<volumetric_view_state>(volumetric_view_state::view_key) = {};
}

auto fog_pass::run_volumetric(const camera& camera, gfx::render_view& rview, const run_params& params, shader_params& shader)
    -> gfx::texture::ptr
{
    const auto target_size = params.target->get_size();
    const float aspect = target_size.height > 0 ? float(target_size.width) / float(target_size.height) : DEFAULT_ASPECT;
    const auto grid = get_volumetric_grid(params.config.volumetric_quality, aspect);
    const float range = math::max(params.config.volumetric_distance, MIN_VOLUMETRIC_DISTANCE);
    const float slice_distribution = get_slice_distribution(params.config.volumetric_slice_uniformity);
    shader.volume = math::vec4(range,
                               slice_distribution,
                               1.0f / std::log(slice_distribution * range + 1.0f),
                               float(grid.depth));

    auto& state = rview.data().get_or_emplace<volumetric_view_state>(volumetric_view_state::view_key);
    const bool is_same_layout =
        state.grid == grid && state.range == range && state.slice_distribution == slice_distribution;
    const bool has_history = state.has_history && is_same_layout && !params.is_camera_cut;
    const std::uint32_t current = has_history ? 1u - state.current : 0u;
    const gfx::texture_desc desc{.size = usize32_t(grid.width, grid.height),
                                 .format = bgfx::TextureFormat::RGBA16F,
                                 .flags = BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_UVW_CLAMP,
                                 .depth = std::uint16_t(grid.depth),
                                 .kind = gfx::texture_kind::tex_3d};
    const gfx::texture::ptr history = rview.tex_acquire(LIT_VOLUME_KEYS[1u - current], desc);
    const gfx::texture::ptr lit = rview.tex_acquire(LIT_VOLUME_KEYS[current], desc);
    const gfx::texture::ptr integrated = rview.tex_acquire(INTEGRATED_VOLUME_KEY, desc);
    if(!history || !lit || !integrated)
    {
        return {};
    }
    if(!has_history)
    {
        state = {};
    }

    const auto rays = get_grid_corner_rays(camera);
    const math::vec4 grid_uniform(float(grid.width), float(grid.height), float(grid.depth), 0.0f);
    // The longest ray through the grid at view depth 1: a light reaching that far past the column's end touches it.
    float longest_ray = 0.0f;
    for(const auto& ray : rays)
    {
        longest_ray = math::max(longest_ray, math::length(math::vec3(ray)));
    }
    const auto lights = pick_local_lights(params.local_lights, camera.get_position(), range * longest_ray);
    const auto packed_lights = pack_local_lights(lights, camera.get_position());
    const auto packed_shadows = pack_light_shadows(lights);
    auto& light_buffers = rview.data().get_or_emplace<local_light_buffers>(local_light_buffers::view_key);
    if(!packed_shadows.empty() && bgfx::isValid(light_buffers.shadows))
    {
        bgfx::update(light_buffers.shadows,
                     0,
                     bgfx::copy(packed_shadows.data(), std::uint32_t(packed_shadows.size() * sizeof(math::vec4))));
    }
    const std::uint32_t changed = find_changed_lights(state.lights, lights);
    const math::vec4 light_info(float(lights.size()),
                                float(changed & CHANGED_LIGHTS_HALF_MASK),
                                float(changed >> CHANGED_LIGHTS_HALF_BITS),
                                0.0f);
    // Bound even without lights, which the lighting then never reads.
    const auto clusters = get_cluster_counts(grid);
    const auto light_masks = light_buffers.get_masks(clusters.get_total());
    if(!lights.empty())
    {
        gfx::render_pass pass("Fog/Light Culling");
        volumetric_.cull->begin();
        bgfx::setBuffer(CULL_MASKS_STAGE, light_masks, bgfx::Access::Write);
        gfx::set_uniform(volumetric_.u_fog_light_info, light_info);
        gfx::set_uniform(volumetric_.u_fog_lights, packed_lights.data(), std::uint16_t(packed_lights.size()));
        gfx::set_uniform(volumetric_.u_fog_grid, grid_uniform);
        gfx::set_uniform(volumetric_.u_fog_rays, rays.data(), std::uint16_t(rays.size()));
        bind_uniforms(shader);
        bgfx::dispatch(pass.id,
                       volumetric_.cull->native_handle(),
                       get_group_count(clusters.x, CULL_GROUP_SIZE),
                       get_group_count(clusters.y, CULL_GROUP_SIZE),
                       get_group_count(clusters.z, CULL_GROUP_SIZE));
        volumetric_.cull->end();
    }
    {
        gfx::render_pass pass("Fog/Froxel Lighting");
        volumetric_.lighting->begin();
        gfx::set_image_3d(LIGHTING_OUTPUT_STAGE, lit->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
        gfx::set_texture(volumetric_.s_fog_history, LIGHTING_HISTORY_STAGE, history);
        gfx::set_texture(volumetric_.s_fog_environment, LIGHTING_ENVIRONMENT_STAGE, params.environment);
        const auto& white = default_textures::get().white_texture();
        gfx::set_texture(volumetric_.s_cloud_shadow, LIGHTING_CLOUD_SHADOW_STAGE, params.clouds.map ? params.clouds.map : white);
        gfx::set_uniform(volumetric_.u_cloud_shadow, params.clouds.placement);
        gfx::set_uniform(volumetric_.u_cloud_shadow_layer, params.clouds.layer);
        // Stand-ins keep every cascade sampler bound; the cascades the sun has replace them.
        for(std::uint8_t i = 0; i < MAX_SHADOW_CASCADES; ++i)
        {
            gfx::set_texture(volumetric_.s_shadow_maps[i], std::uint8_t(LIGHTING_SHADOW_STAGE + i), white);
        }
        const auto& noise = default_textures::get().cloud_noise().base_noise;
        gfx::set_texture(volumetric_.s_fog_noise,
                         LIGHTING_NOISE_STAGE,
                         noise ? noise.get() : default_textures::get().transparent_texture_3d().get());
        // The point and spot lights' maps, point sampled as the lighting pass reads them; a stand-in keeps the sampler
        // bound while no light has a tile (every record then says unshadowed).
        const auto atlas_texture = shadow::shadow_atlas::get().get_color_texture();
        if(bgfx::isValid(atlas_texture) && volumetric_.s_fog_shadow_atlas)
        {
            bgfx::setTexture(LIGHTING_SHADOW_ATLAS_STAGE,
                             volumetric_.s_fog_shadow_atlas->native_handle(),
                             atlas_texture,
                             BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
        }
        else
        {
            gfx::set_texture(volumetric_.s_fog_shadow_atlas, LIGHTING_SHADOW_ATLAS_STAGE, white);
        }
        if(bgfx::isValid(light_buffers.shadows))
        {
            bgfx::setBuffer(LIGHTING_LIGHT_SHADOWS_STAGE, light_buffers.shadows, bgfx::Access::Read);
        }
        if(bgfx::isValid(light_masks))
        {
            bgfx::setBuffer(LIGHTING_LIGHT_MASKS_STAGE, light_masks, bgfx::Access::Read);
        }
        const bool has_shadow = params.sun.shadow != nullptr && params.sun.shadow_cascades > 0;
        if(has_shadow)
        {
            params.sun.shadow->submit_uniforms(LIGHTING_SHADOW_STAGE);
        }
        const float cascades = has_shadow ? float(math::min(params.sun.shadow_cascades, MAX_SHADOW_CASCADES)) : 0.0f;
        gfx::set_uniform(volumetric_.u_fog_shadow, math::vec4(cascades, params.sun.shadow_depth_offset, 0.0f, 0.0f));
        gfx::set_uniform(volumetric_.u_fog_light_info, light_info);
        gfx::set_uniform(volumetric_.u_fog_lights, packed_lights.data(), std::uint16_t(packed_lights.size()));
        const std::uint32_t jitter_index = state.frame % JITTER_SEQUENCE_LENGTH + 1u;
        const float history_weight = has_history ? math::clamp(params.config.volumetric_history, 0.0f, MAX_HISTORY_WEIGHT) : 0.0f;
        const math::vec4 jitter(get_halton(jitter_index, 2), get_halton(jitter_index, 3), get_halton(jitter_index, 5), history_weight);
        gfx::set_uniform(volumetric_.u_fog_jitter, jitter);
        gfx::set_uniform(volumetric_.u_fog_grid, grid_uniform);
        gfx::set_uniform(volumetric_.u_fog_rays, rays.data(), std::uint16_t(rays.size()));
        gfx::set_uniform(volumetric_.u_fog_prev_view_proj, camera.get_prev_view_projection_relative_unjittered().get_matrix());
        const math::vec3 previous_position = math::inverse(camera.get_prev_view()).get_position();
        const float is_origin_bottom_left = bgfx::getCaps()->originBottomLeft ? 1.0f : 0.0f;
        gfx::set_uniform(volumetric_.u_fog_prev_offset, math::vec4(camera.get_position() - previous_position, is_origin_bottom_left));
        const auto pre_exposure = params.pre_exposure.to_uniform();
        gfx::set_uniform(volumetric_.u_pre_exposure, pre_exposure.data());
        bind_uniforms(shader);
        bgfx::dispatch(pass.id,
                       volumetric_.lighting->native_handle(),
                       get_group_count(grid.width, LIGHTING_GROUP_SIZE),
                       get_group_count(grid.height, LIGHTING_GROUP_SIZE),
                       get_group_count(grid.depth, LIGHTING_GROUP_SIZE));
        volumetric_.lighting->end();
    }
    {
        gfx::render_pass pass("Fog/Froxel Integration");
        volumetric_.integrate->begin();
        gfx::set_image_3d(INTEGRATE_OUTPUT_STAGE, integrated->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
        gfx::set_texture(volumetric_.s_fog_lit, INTEGRATE_LIT_STAGE, lit);
        gfx::set_uniform(volumetric_.u_fog_grid, grid_uniform);
        gfx::set_uniform(volumetric_.u_fog_rays, rays.data(), std::uint16_t(rays.size()));
        bind_uniforms(shader);
        bgfx::dispatch(pass.id,
                       volumetric_.integrate->native_handle(),
                       get_group_count(grid.width, INTEGRATE_GROUP_SIZE),
                       get_group_count(grid.height, INTEGRATE_GROUP_SIZE),
                       1);
        volumetric_.integrate->end();
    }
    state.frame++;
    state.current = current;
    state.has_history = true;
    state.grid = grid;
    state.range = range;
    state.slice_distribution = slice_distribution;
    state.lights = lights;
    return integrated;
}

auto fog_pass::run(const camera& camera, gfx::render_view& rview, const run_params& params) -> shader_params
{
    auto shader = make_shader_params(params.config, params.sun, camera.get_position());
    shader.forward = math::vec4(camera.get_view_inverse().z_unit_axis(), 0.0f);
    const auto volumes = pick_fog_volumes(params.volumes, camera.get_position());
    shader.volume_info = math::vec4(float(volumes.size()), 0.0f, 0.0f, 0.0f);
    shader.volumes = pack_fog_volumes(volumes, camera.get_position());
    if(!shader.is_enabled() || !params.target || !params.depth || !params.environment ||
       !fog_program_.program || !fog_program_.program->is_valid())
    {
        release_volumetric(rview);
        return {};
    }
    const bool has_volumetric_programs = volumetric_.lighting && volumetric_.lighting->is_valid() && volumetric_.integrate &&
                                         volumetric_.integrate->is_valid() && volumetric_.cull && volumetric_.cull->is_valid() &&
                                         fog_program_.volumetric_program && fog_program_.volumetric_program->is_valid();
    if(params.config.volumetric && params.allows_volumetric && has_volumetric_programs && is_volumetric_supported())
    {
        shader.volume_texture = run_volumetric(camera, rview, params, shader);
    }
    else
    {
        release_volumetric(rview);
    }
    if(!shader.volume_texture)
    {
        shader.volume = {};
    }
    auto& program = shader.is_volumetric() ? *fog_program_.volumetric_program : *fog_program_.program;
    gfx::render_pass pass("Fog/Composite");
    pass.bind(params.target.get());
    // Camera-relative: the shader reconstructs each pixel's offset from the camera, which keeps its precision far
    // from the world origin.
    pass.set_view_proj(camera.get_view_relative(), camera.get_projection());
    program.begin();
    gfx::set_texture(fog_program_.s_fog_depth, FOG_DEPTH_STAGE, params.depth);
    // The SH is read with texelFetch: the sampler flags do not matter.
    gfx::set_texture(fog_program_.s_fog_environment, FOG_ENVIRONMENT_STAGE, params.environment);
    if(shader.is_volumetric())
    {
        gfx::set_texture(fog_program_.s_fog_volume, FOG_VOLUME_STAGE, shader.volume_texture);
    }
    const auto pre_exposure = params.pre_exposure.to_uniform();
    gfx::set_uniform(fog_program_.u_pre_exposure, pre_exposure.data());
    bind_uniforms(shader);
    const auto output_size = params.target->get_size();
    bgfx::setScissor(0, 0, uint16_t(output_size.width), uint16_t(output_size.height));
    const auto topology = gfx::clip_quad(1.0f);
    // Premultiplied: frame = fog in-scatter + frame * fog transmittance.
    bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_SRC_ALPHA));
    bgfx::submit(pass.id, program.native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    program.end();
    return shader;
}

} // namespace unravel
