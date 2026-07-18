#pragma once

#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/particles/ps/particle_system_soa.h>
#include <graphics/texture.h>
#include <engine/assets/asset_handle.h>
#include <bx/easing.h>
#include <math/math.h>
#include <base/basetypes.hpp>
#include <chrono>

namespace unravel
{

/**
 * @class particle_emitter_component
 * @brief Component that wraps the soa particle system emitter.
 */
class particle_emitter_component : public component_crtp<particle_emitter_component, owned_component>
{
public:
    enum class culling_mode : uint8_t
    {
        always_simulate,
        renderer_based,
    };

    static void on_create_component(entt::registry& r, entt::entity e);
    static void on_destroy_component(entt::registry& r, entt::entity e);

    void set_enabled(bool enabled);
    auto is_enabled() const -> bool;

    void set_culling_mode(culling_mode mode);
    auto get_culling_mode() const -> culling_mode;

    void set_last_render_frame(uint64_t frame);
    auto get_last_render_frame() const noexcept -> uint64_t;
    auto is_newly_created() const noexcept -> bool;
    auto was_used_last_frame() const noexcept -> bool;

    auto get_emitter_handle() const -> ps_soa::emitter_handle;

    void set_shape(ps_soa::emitter_shape shape);
    auto get_shape() const -> ps_soa::emitter_shape;

    void set_direction(ps_soa::emitter_direction direction);
    auto get_direction() const -> ps_soa::emitter_direction;

    void set_spawn_location(ps_soa::spawn_location spawn_location);
    auto get_spawn_location() const -> ps_soa::spawn_location;

    void set_max_particles(uint32_t max_particles);
    auto get_max_particles() const -> uint32_t;

    void set_emission_lifetime(std::chrono::duration<float> lifetime);
    auto get_emission_lifetime() const -> std::chrono::duration<float>;

    void set_gravity_scale(float scale);
    auto get_gravity_scale() const -> float;

    void set_emission_rate(float emission_rate);
    auto get_emission_rate() const -> float;

    void set_temporal_motion(float temporal_motion);
    auto get_temporal_motion() const -> float;

    void set_velocity_damping(float velocity_damping);
    auto get_velocity_damping() const -> float;

    void set_force_over_lifetime(const math::vec3& force);
    auto get_force_over_lifetime() const -> math::vec3;

    void set_emission_shape_position(const math::vec3& position);
    auto get_emission_shape_position() const -> math::vec3;

    void set_emission_shape_scale(const math::vec3& scale);
    auto get_emission_shape_scale() const -> math::vec3;

    void set_size_by_speed_range(const frange_t& size_range);
    auto get_size_by_speed_range() const -> const frange_t&;

    void set_size_by_speed_velocity_range(const frange_t& velocity_range);
    auto get_size_by_speed_velocity_range() const -> const frange_t&;

    void set_color_by_speed_gradient(const math::gradient<math::color>& gradient);
    auto get_color_by_speed_gradient() const -> const math::gradient<math::color>&;

    void set_color_by_speed_velocity_range(const frange_t& velocity_range);
    auto get_color_by_speed_velocity_range() const -> const frange_t&;

    void set_lifetime_by_emitter_speed_gradient(const math::gradient<float>& gradient);
    auto get_lifetime_by_emitter_speed_gradient() const -> const math::gradient<float>&;

    void set_lifetime_by_emitter_speed_range(const frange_t& speed_range);
    auto get_lifetime_by_emitter_speed_range() const -> const frange_t&;

    void set_lifetime(std::chrono::duration<float> lifetime);
    auto get_lifetime() const -> std::chrono::duration<float>;

    void set_velocity_gradient(const math::gradient<frange_t>& gradient);
    auto get_velocity_gradient() const -> const math::gradient<frange_t>&;

    void set_scale_gradient(const math::gradient<frange_t>& gradient);
    auto get_scale_gradient() const -> const math::gradient<frange_t>&;

    void set_initial_scale_3d(const math::vec3& scale);
    auto get_initial_scale_3d() const -> math::vec3;

    void set_opacity(float opacity);
    auto get_opacity() const -> float;

    void set_color_intensity(float intensity);
    auto get_color_intensity() const -> float;

    void play();
    void play_sub_emitters();
    void stop();
    void stop_sub_emitters();
    void stop_and_reset();
    void stop_and_reset_sub_emitters();
    void pause();
    void pause_sub_emitters();
    void resume();
    void resume_sub_emitters();
    auto is_playing() const -> bool;
    auto is_paused() const -> bool;
    auto is_stopped() const -> bool;

    void set_loop(bool loop);
    auto is_loop() const -> bool;

    void set_start_delay(std::chrono::duration<float> delay);
    auto get_start_delay() const -> std::chrono::duration<float>;

    void set_align_to_direction(bool align);
    auto get_align_to_direction() const -> bool;

    void set_pivot(const math::vec2& pivot);
    auto get_pivot() const -> math::vec2;

    void set_color_gradient(const math::gradient<math::color>& gradient);
    auto get_color_gradient() const -> const math::gradient<math::color>&;

    void set_position_easing(bx::Easing::Enum easing);
    auto get_position_easing() const -> bx::Easing::Enum;

    void set_simulation_space(ps_soa::simulation_space space);
    auto get_simulation_space() const -> ps_soa::simulation_space;

    /**
     * @brief CPU vs GPU particle pack/sim path for this emitter (artist-selectable).
     */
    void set_simulation_backend(ps_soa::particle_sim_backend backend);
    auto get_simulation_backend() const -> ps_soa::particle_sim_backend;

    auto get_num_particles() const -> uint32_t;
    auto get_world_bounds() const -> math::bbox;
    auto get_updated_world_bounds(const math::transform& world_transform) const -> math::bbox;

    void set_texture(const asset_handle<gfx::texture>& texture);
    auto get_texture() const -> const asset_handle<gfx::texture>&;

    void set_texture_mode(ps_soa::texture_mode mode);
    auto get_texture_mode() const -> ps_soa::texture_mode;

    void set_render_mode(ps_soa::render_mode mode);
    auto get_render_mode() const -> ps_soa::render_mode;

    void set_blend_mode(ps_soa::blend_mode mode);
    auto get_blend_mode() const -> ps_soa::blend_mode;

    void set_texture_sheet_tiles(math::vec2 tiles);
    auto get_texture_sheet_tiles() const -> math::vec2;

    void set_texture_sheet_cycles(float cycles);
    auto get_texture_sheet_cycles() const -> float;

    void set_texture_sheet_randomize(bool randomize);
    auto get_texture_sheet_randomize() const -> bool;

    void update_emitter(const math::transform& world_transform, delta_t dt);

    auto get_desc() const -> const ps_soa::emitter_desc&;

    void recreate_emitter();
    void reset_emitter();

private:
    bool enabled_ = true;
    culling_mode culling_mode_ = culling_mode::renderer_based;
    uint64_t last_render_frame_ = 0;
    ps_soa::emitter_shape shape_ = ps_soa::emitter_shape::sphere;
    ps_soa::emitter_direction direction_ = ps_soa::emitter_direction::up;
    uint32_t max_particles_ = 1024;
    ps_soa::particle_sim_backend simulation_backend_ = ps_soa::particle_sim_backend::cpu;
    ps_soa::emitter_handle emitter_handle_{};
    ps_soa::emitter_desc desc_{};
    ps_soa::emitter_transform_state transform_state_{};
    asset_handle<gfx::texture> texture_;
};

} // namespace unravel
