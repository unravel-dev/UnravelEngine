#include "gi_cache_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>

#include <graphics/graphics.h>

namespace unravel
{

auto gi_cache_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto cs_insert = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_cache_insert.sc");
    auto cs_update = am.get_asset<gfx::shader>("engine:/data/shaders/gi/cs_gi_cache_update.sc");
    insert_program_.program = std::make_unique<gpu_program>(cs_insert);
    insert_program_.cache_uniforms();
    update_program_.program = std::make_unique<gpu_program>(cs_update);
    update_program_.cache_uniforms();
    return insert_program_.is_valid() && update_program_.is_valid();
}

auto gi_cache_pass::run(gfx::render_view& rview, const run_params& params) -> bool
{
    APP_SCOPE_PERF("Rendering/GI/Cache Pass");
    if(!insert_program_.is_valid() || !update_program_.is_valid() || !params.g_buffer || !params.cam ||
       !params.surface_cache || !params.view_cache)
    {
        return false;
    }
    auto& surface_cache = *params.surface_cache;
    if(!surface_cache.is_enabled())
    {
        return false;
    }
    auto& cache = surface_cache.get_radiance_cache();
    if(!cache.is_valid())
    {
        return false;
    }
    const auto& instances = surface_cache.get_instances();
    if(instances.empty())
    {
        return false;
    }
    auto& atlas = surface_cache.get_atlas();
    const auto& clipmap = params.view_cache->get_clipmap();
    const auto& clipmap_gpu = params.view_cache->get_clipmap_gpu();
    const auto& light_buffer = surface_cache.get_light_buffer();
    const auto& s = params.settings;

    // From the cache, not from this pass's settings: the resolve pass derives keys from the same
    // values, and any drift between them would make every lookup miss.
    const auto& key_settings = cache.get_settings();
    const float cache_params[4] = {float(cache.get_capacity() - 1u),
                                   key_settings.base_cell_size,
                                   key_settings.base_distance,
                                   float(key_settings.max_level)};
    // w carries the level cross-fade band, which is a KEY parameter and so comes from the cache
    // rather than from this pass -- a writer and a reader that disagree on it insert and look up at
    // different levels near a boundary and never meet.
    const float cache_params2[4] = {float(gfx::get_render_frame()),
                                    s.min_alpha,
                                    s.max_samples,
                                    key_settings.level_blend};
    // Shared by BOTH dispatches. Insertion resolves the G-buffer surface onto the field before
    // deriving a key, so it needs the field exactly as the update pass does.
    const bool clipmap_ready = clipmap_gpu.is_valid();
    const float* clipmap_params = clipmap_gpu.get_sampling_params();

    // --- Register the surfaces currently on screen ---
    {
        gfx::render_pass pass("GI/Cache Insert");
        // REQUIRED even though this is a compute dispatch: the shader reconstructs world
        // positions from depth via u_invViewProj, which bgfx supplies per VIEW. Without this
        // the matrix is whatever the view last held, so every reconstructed position -- and
        // therefore every key -- is nonsense, and no lookup can ever match one.
        pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
        cache.clear(pass.id);
        insert_program_.program->begin();
        const auto size = params.g_buffer->get_size();
        gfx::set_texture(insert_program_.s_gi_depth, 8, params.g_buffer->get_texture(4));
        gfx::set_texture(insert_program_.s_gi_normal, 9, params.g_buffer->get_texture(1));
        gfx::set_texture(insert_program_.s_gi_base_color, 10, params.g_buffer->get_texture(0));
        gfx::set_texture(insert_program_.s_gi_emissive, 11, params.g_buffer->get_texture(2));
        gfx::set_texture(insert_program_.s_sdf_clipmap,
                         4,
                         clipmap_ready ? clipmap_gpu.get_texture() : atlas.get_atlas_texture());
        gfx::set_uniform(insert_program_.u_sdf_clipmap_levels,
                         clipmap_gpu.get_level_params(),
                         global_sdf_clipmap::level_count);
        gfx::set_uniform(insert_program_.u_sdf_clipmap_params, clipmap_params);
        gfx::set_buffer(6, cache.get_keys_buffer(), gfx::access::ReadWrite);
        gfx::set_buffer(7, cache.get_data_buffer(), gfx::access::ReadWrite);
        gfx::set_uniform(insert_program_.u_gi_cache_params, cache_params);
        gfx::set_uniform(insert_program_.u_gi_cache_params2, cache_params2);
        // Capped by what the cascade can actually address. A surface is registered by resolving it
        // onto the field's isosurface, and beyond the outermost level there is no isosurface to
        // resolve onto -- the shader now detects that and skips, but only after paying a full
        // resolve (four iterations of seven cascade samples each) per sampled pixel. The G-buffer
        // routinely sees several times further than the cascade reaches, so rejecting those on
        // distance first is the difference between one length() and 28 texture fetches.
        //
        // Derived rather than configured: a setting larger than the cascade cannot mean anything,
        // and this one used to read 200 m against a cascade covering 64.
        const float clipmap_reach =
            clipmap.get_level_extent(global_sdf_clipmap::level_count - 1u) * 0.5f;
        const float insert_params[4] = {float(size.width),
                                        float(size.height),
                                        s.insert_stride,
                                        math::min(s.insert_max_distance, clipmap_reach)};
        gfx::set_uniform(insert_program_.u_gi_insert_params, insert_params);
        const auto camera_position = params.cam->get_position();
        const float camera[4] = {camera_position.x, camera_position.y, camera_position.z, 0.0f};
        gfx::set_uniform(insert_program_.u_gi_camera_position, camera);
        // One thread per SAMPLED pixel, not per pixel.
        const uint32_t sampled_x = uint32_t(std::ceil(float(size.width) / s.insert_stride));
        const uint32_t sampled_y = uint32_t(std::ceil(float(size.height) / s.insert_stride));
        gfx::dispatch(pass.id,
                      insert_program_.program->native_handle(),
                      (sampled_x + 7u) / 8u,
                      (sampled_y + 7u) / 8u,
                      1);
        insert_program_.program->end();
    }

    // --- Light every resident entry ---
    {
        gfx::render_pass pass("GI/Cache Update");
        update_program_.program->begin();
        gfx::set_texture(update_program_.s_sdf_atlas, 0, atlas.get_atlas_texture());
        gfx::set_buffer(1, atlas.get_header_buffer(), gfx::access::Read);
        gfx::set_buffer(2, atlas.get_indirection_buffer(), gfx::access::Read);
        gfx::set_buffer(3, surface_cache.get_instance_buffer(), gfx::access::Read);
        gfx::set_texture(update_program_.s_sdf_clipmap,
                         4,
                         clipmap_ready ? clipmap_gpu.get_texture() : atlas.get_atlas_texture());
        if(light_buffer.is_valid())
        {
            gfx::set_buffer(5, light_buffer.get_buffer(), gfx::access::Read);
        }
        gfx::set_buffer(6, cache.get_keys_buffer(), gfx::access::ReadWrite);
        gfx::set_buffer(7, cache.get_data_buffer(), gfx::access::ReadWrite);

        const float sdf_params[4] = {float(atlas.get_atlas_brick_dim()),
                                     float(atlas.get_atlas_voxel_dim()),
                                     float(instances.size()),
                                     0.0f};
        gfx::set_uniform(update_program_.u_sdf_params, sdf_params);
        gfx::set_buffer(12, surface_cache.get_grid_offset_buffer(), gfx::access::Read);
        gfx::set_buffer(13, surface_cache.get_grid_instance_buffer(), gfx::access::Read);
        gfx::set_uniform(update_program_.u_sdf_grid_params, surface_cache.get_grid_params(), 2);
        gfx::set_uniform(update_program_.u_sdf_clipmap_levels,
                         clipmap_gpu.get_level_params(),
                         global_sdf_clipmap::level_count);
        gfx::set_uniform(update_program_.u_sdf_clipmap_params, clipmap_params);
        const float light_params[4] = {light_buffer.is_valid() ? float(light_buffer.get_light_count()) : 0.0f,
                                       0.0f,
                                       0.0f,
                                       0.0f};
        gfx::set_uniform(update_program_.u_gpu_light_params, light_params);
        // Shadow rays are budgeted tightly: there is one per light per entry, and they only
        // need to answer hit or miss.
        const float shadow_params[4] = {s.shadow_distance,
                                        s.shadow_normal_bias_voxels,
                                        s.shadow_near_field,
                                        s.shadow_max_steps};
        gfx::set_uniform(update_program_.u_gi_shadow_params, shadow_params);
        const float shadow_params2[4] = {s.shadow_surface_bias, s.shadow_step_relaxation, 0.0f, 0.0f};
        gfx::set_uniform(update_program_.u_gi_shadow_params2, shadow_params2);
        gfx::set_uniform(update_program_.u_gi_cache_params, cache_params);
        gfx::set_uniform(update_program_.u_gi_cache_params2, cache_params2);
        const float update_params[4] = {float(cache.get_capacity()),
                                        s.surface_offset_cells,
                                        s.bounce_rays,
                                        s.default_albedo};
        gfx::set_uniform(update_program_.u_gi_update_params, update_params);
        const float bounce_params[4] = {s.bounce_distance,
                                        s.bounce_near_field,
                                        s.bounce_max_steps,
                                        s.bounce_surface_bias};
        gfx::set_uniform(update_program_.u_gi_update_bounce, bounce_params);
        const auto update_camera_position = params.cam->get_position();
        const float update_camera[4] = {update_camera_position.x,
                                        update_camera_position.y,
                                        update_camera_position.z,
                                        0.0f};
        gfx::set_uniform(update_program_.u_gi_update_camera, update_camera);

        gfx::dispatch(pass.id, update_program_.program->native_handle(), (cache.get_capacity() + 63u) / 64u, 1, 1);
        update_program_.program->end();
    }
    return true;
}

} // namespace unravel
