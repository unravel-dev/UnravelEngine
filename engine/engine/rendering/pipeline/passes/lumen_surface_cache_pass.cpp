#include "lumen_surface_cache_pass.h"

#include <graphics/graphics.h>

namespace unravel
{
namespace
{

/// What a getter returns while no shared cache is held.
auto get_no_texture() -> const gfx::texture::ptr&
{
    static const gfx::texture::ptr none;
    return none;
}

} // namespace

lumen_surface_cache_pass::~lumen_surface_cache_pass()
{
    release_targets();
}

auto lumen_surface_cache_pass::init(rtti::context& ctx) -> bool
{
    shared_ = lumen_surface_cache::acquire(ctx);
    const bool has_grid = object_grid_.init(ctx);
    return has_grid && shared_->is_ready();
}

void lumen_surface_cache_pass::set_cloud_shadow(const cloud_shadow& shadow)
{
    shared_->set_cloud_shadow(shadow);
}

auto lumen_surface_cache_pass::update(const surface_cache_system& gi_scene,
                                      const global_sdf_clipmap* clipmap,
                                      const math::vec3& view_origin,
                                      const math::frustum& view_frustum,
                                      const gi_settings::scene_settings& view_settings,
                                      const gi_project_settings& project_settings) -> bool
{
    if(!is_user_)
    {
        shared_->add_user();
        is_user_ = true;
    }
    const uint64_t frame = gfx::get_render_frame();
    lumen_scene::viewer viewer;
    viewer.origin = view_origin;
    viewer.frustum = view_frustum;
    shared_->register_viewer(this, viewer, clipmap, frame);
    if(!shared_->claim_update(frame))
    {
        return false;
    }
    shared_->update(gi_scene, frame, view_settings, project_settings);
    return true;
}

void lumen_surface_cache_pass::update_object_grid(const surface_cache_system& gi_scene,
                                                  const surface_cache_view& view_cache)
{
    object_grid_.update(gi_scene, view_cache);
}

auto lumen_surface_cache_pass::get_scene() const -> const lumen_scene&
{
    return shared_->get_scene();
}

void lumen_surface_cache_pass::get_visualized_cards(const math::vec3& view_origin,
                                                    float distance,
                                                    const math::frustum& view_frustum,
                                                    std::vector<lumen_scene::visualized_card>& out) const
{
    shared_->get_visualized_cards(view_origin, distance, view_frustum, out);
}

auto lumen_surface_cache_pass::get_capture_target() const -> const gfx::frame_buffer::ptr&
{
    return shared_->get_capture_target();
}

auto lumen_surface_cache_pass::compute_capture_view(const lumen_scene::capture& cap) const -> capture_view
{
    return shared_->compute_capture_view(cap);
}

void lumen_surface_cache_pass::copy_captures()
{
    shared_->copy_captures();
}

void lumen_surface_cache_pass::light(lighting_inputs inputs)
{
    inputs.object_grid = &object_grid_;
    shared_->light(inputs, this);
}

auto lumen_surface_cache_pass::run_debug(debug_params params) -> bool
{
    params.object_grid = &object_grid_;
    return shared_->run_debug(params);
}

auto lumen_surface_cache_pass::is_ready() const -> bool
{
    return shared_ && shared_->is_ready() && object_grid_.is_ready();
}

auto lumen_surface_cache_pass::has_targets() const -> bool
{
    return is_user_ && shared_->has_targets();
}

void lumen_surface_cache_pass::release_targets()
{
    if(is_user_ && shared_)
    {
        shared_->forget_viewer(this);
        shared_->remove_user();
    }
    is_user_ = false;
    object_grid_.release();
}

auto lumen_surface_cache_pass::get_scene_buffer() const -> bgfx::DynamicVertexBufferHandle
{
    return is_user_ ? shared_->get_scene_buffer() : bgfx::DynamicVertexBufferHandle{bgfx::kInvalidHandle};
}

auto lumen_surface_cache_pass::get_final_atlas() const -> const gfx::texture::ptr&
{
    return is_user_ ? shared_->get_final_atlas() : get_no_texture();
}

auto lumen_surface_cache_pass::get_depth_atlas() const -> const gfx::texture::ptr&
{
    return is_user_ ? shared_->get_depth_atlas() : get_no_texture();
}

auto lumen_surface_cache_pass::get_radiosity_sh(uint32_t channel) const -> const gfx::texture::ptr&
{
    return is_user_ ? shared_->get_radiosity_sh(channel) : get_no_texture();
}

auto lumen_surface_cache_pass::get_radiosity_layout() const -> const lumen_pass::radiosity_layout&
{
    return shared_->get_radiosity_layout();
}

void lumen_surface_cache_pass::get_visualized_pages(std::vector<math::vec4>& out) const
{
    shared_->get_visualized_pages(out);
}

auto lumen_surface_cache_pass::get_object_grid_params() const -> math::vec4
{
    return object_grid_.get_params(shared_->get_card_bias_scale());
}

auto lumen_surface_cache_pass::get_surface_cache_params() const -> math::vec4
{
    return shared_->get_surface_cache_params();
}

auto lumen_surface_cache_pass::has_lighting() const -> bool
{
    return is_user_ && shared_->has_lighting() && object_grid_.has_grid();
}

auto lumen_surface_cache_pass::get_feedback() -> lumen_surface_cache_feedback*
{
    return is_user_ ? shared_->get_feedback() : nullptr;
}

auto lumen_surface_cache_pass::get_card_index_revision() const -> uint64_t
{
    return shared_->get_card_index_revision();
}

void lumen_surface_cache_pass::bind_for_sampling(uint8_t scene_stage,
                                                 uint8_t final_stage,
                                                 uint8_t grid_stage,
                                                 bool enabled) const
{
    shared_->bind_for_sampling(scene_stage, final_stage, grid_stage, enabled && is_user_, object_grid_);
}

} // namespace unravel
