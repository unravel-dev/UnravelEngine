#pragma once

#include "particle_types.h"

#include <cstdint>

namespace unravel
{
namespace ps_soa
{

/// Selects CPU vs resident-GPU simulation backend.
enum class particle_sim_backend : uint8_t
{
    cpu = 0,
    gpu = 1,
};

/// GPU instance row matches vs_particle_instanced i_data0..i_data5 (96 bytes).
constexpr uint16_t k_gpu_instance_stride = 96;
constexpr uint32_t k_gpu_instance_floats = k_gpu_instance_stride / sizeof(float);
constexpr uint32_t k_gpu_sim_floats_per_particle = 20;
constexpr uint32_t k_gpu_sim_vec4s_per_particle = 5;
constexpr uint32_t k_gpu_lut_size = 256;
constexpr uint32_t k_gpu_cs_threads = 64;

/**
 * @brief Packed sim particle for compute (80 bytes / 5 x vec4).
 *
 * Layout:
 *  [0] start.xyz, life
 *  [1] end0.xyz, lifespan
 *  [2] end1.xyz, scale_start
 *  [3] scale_end, texsheet_seed, pad, pad
 *  [4] rotation.xyzw (precomputed when align_to_direction)
 */
struct gpu_sim_particle
{
    float start_x, start_y, start_z, life;
    float end0_x, end0_y, end0_z, lifespan;
    float end1_x, end1_y, end1_z, scale_start;
    float scale_end, texsheet_seed, pad0, pad1;
    float rot_x, rot_y, rot_z, rot_w;
};

static_assert(sizeof(gpu_sim_particle) == k_gpu_sim_floats_per_particle * sizeof(float),
              "gpu_sim_particle size mismatch");

/// Uniform vec4 packing for compute (set as float[4] arrays).
struct gpu_pack_uniforms
{
    float opacity = 1.0f;
    float color_intensity = 1.0f;
    float avg_system_scale = 1.0f;
    float render_mode = 0.0f;

    float pivot_x = 0.5f;
    float pivot_y = 0.5f;
    float particle_count = 0.0f;
    float features = 0.0f;

    float scale3d_x = 1.0f;
    float scale3d_y = 1.0f;
    float scale3d_z = 1.0f;
    float tex_sheet_cycles = 0.0f;

    float tex_tiles_x = 1.0f;
    float tex_tiles_y = 1.0f;
    float tex_sheet_randomize = 0.0f;
    float pad0 = 0.0f;

    float size_by_speed_min = 1.0f;
    float size_by_speed_max = 1.0f;
    float inv_size_speed_span = 0.0f;
    float size_speed_min = 0.0f;

    float inv_color_speed_span = 0.0f;
    float color_speed_min = 0.0f;
    float pad1 = 0.0f;
    float pad2 = 0.0f;

    float local_to_world[16]{};
};

inline auto gpu_feature_mask(emitter_feature features) -> uint32_t
{
    return static_cast<uint32_t>(features);
}

} // namespace ps_soa
} // namespace unravel
