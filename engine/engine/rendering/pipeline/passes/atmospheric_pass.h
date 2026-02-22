#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gpu_program.h>
#include <graphics/render_view.h>

namespace unravel
{

class atmospheric_pass
{
public:
    struct run_params
    {
        math::vec3 light_direction = math::normalize(math::vec3(0.2f, -0.8f, 1.0f));

        // [1.9 - 10.0f]
        float turbidity = 1.9f;

        /// Cloud coverage factor [0, 1].
        float cloud_coverage = 0.5f;
        /// Cloud altitude in world units.
        float cloud_altitude = 3000.0f;
        /// Accumulated elapsed time (seconds) for cloud animation.
        float cloud_time = 0.0f;
        /// Cloud density/opacity multiplier.
        float cloud_density = 1.0f;
        /// Sky brightness multiplier (1.0 = neutral). Affects visible sky and irradiance.
        float sky_brightness = 1.0f;
    };

    auto init(rtti::context& ctx) -> bool;
    void run(gfx::frame_buffer::ptr input, const camera& camera, gfx::render_view& rview, delta_t dt, const run_params& params);

private:
    struct atmospheric_program : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(program.get(), u_parameters, "u_parameters", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_kr_and_intensity, "u_kr_and_intensity", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_turbidity_parameters1, "u_turbidity_parameters1", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_turbidity_parameters2, "u_turbidity_parameters2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_turbidity_parameters3, "u_turbidity_parameters3", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_cloud_params, "u_cloud_params", gfx::uniform_type::Vec4);
        }

        gfx::program::uniform_ptr u_parameters;
        gfx::program::uniform_ptr u_kr_and_intensity;
        gfx::program::uniform_ptr u_turbidity_parameters1;
        gfx::program::uniform_ptr u_turbidity_parameters2;
        gfx::program::uniform_ptr u_turbidity_parameters3;
        gfx::program::uniform_ptr u_cloud_params;


        std::unique_ptr<gpu_program> program;

    } atmospheric_program_;
};
} // namespace unravel
