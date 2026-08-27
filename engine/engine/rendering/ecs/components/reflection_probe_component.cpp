#include "reflection_probe_component.h"
#include "graphics/render_pass.h"

namespace unravel
{

auto reflection_probe_component::get_bounds() const -> math::bbox
{
    if(probe_.type == probe_type::sphere)
    {
        auto sphere = math::bsphere(math::vec3(0.0f, 0.0f, 0.0f), probe_.sphere_data.range);
        math::bbox result;
        result.from_sphere(sphere.position, sphere.radius);
        return result;
    }
    else if(probe_.type == probe_type::box)
    {
        math::bbox result;
        result.min = -probe_.box_data.extents;
        result.max = probe_.box_data.extents;
        return result;
    }

    return {};
}

auto reflection_probe_component::compute_projected_sphere_rect(irect32_t& rect,
                                                               const math::vec3& position,
                                                               const math::vec3& scale,
                                                               const math::vec3& view_origin,
                                                               const math::transform& view,
                                                               const math::transform& proj) const -> int
{
    if(probe_.type == probe_type::sphere)
    {
        return math::compute_projected_sphere_rect(rect.left,
                                                   rect.right,
                                                   rect.top,
                                                   rect.bottom,
                                                   position,
                                                   probe_.sphere_data.range * math::max(scale.x, math::max(scale.y, scale.z)),
                                                   view_origin,
                                                   view,
                                                   proj);
    }
    else if(probe_.type == probe_type::box)
    {
        float w2 = math::pow(scale.x * probe_.box_data.extents.x * 2.0f, 2.0f);
        float h2 = math::pow(scale.y * probe_.box_data.extents.y * 2.0f, 2.0f);
        float l2 = math::pow(scale.z * probe_.box_data.extents.z * 2.0f, 2.0f);
        float d2 = w2 + h2 + l2;
        float d = math::sqrt(d2);

        return math::compute_projected_sphere_rect(rect.left,
                                                   rect.right,
                                                   rect.top,
                                                   rect.bottom,
                                                   position,
                                                   d,
                                                   view_origin,
                                                   view,
                                                   proj);
    }
    else
    {
        return 1;
    }
}

auto reflection_probe_component::get_render_view(size_t idx) -> gfx::render_view&
{
    return face_rviews_[idx];
}

auto reflection_probe_component::get_cubemap() -> const  gfx::texture::ptr&
{
    // Product of the bake, not auto-collected: it goes unaccessed for as long as the
    // probe is frustum-culled, and already_generated() would not know to rebuild it.
    auto& tex = rview_.tex_get_or_emplace("CUBEMAP", false);
    const uint16_t size = probe_resolution_to_size(resolution_);
    constexpr gfx::texture_format format = gfx::texture_format::RGBA16F;
    if(gfx::needs_recreate(tex, {size, size}, format))
    {
        tex.reset();
        tex = std::make_shared<gfx::texture>(size,
                                             true,
                                             1,
                                             format,
                                             BGFX_TEXTURE_COMPUTE_WRITE |BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_RT);
    }

    return tex;
}

auto reflection_probe_component::get_cubemap_prefiltered() -> const  gfx::texture::ptr&
{
    // Product of the bake, not auto-collected (see get_cubemap).
    auto& tex = rview_.tex_get_or_emplace("CUBEMAP_PREFILTERED", false);
    const uint16_t size = probe_resolution_to_size(resolution_);
    constexpr gfx::texture_format format = gfx::texture_format::RGBA16F;
    if(gfx::needs_recreate(tex, {size, size}, format))
    {
        tex.reset();
        tex = std::make_shared<gfx::texture>(size,
                                             true,
                                             1,
                                             format,
                                             BGFX_TEXTURE_COMPUTE_WRITE | BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_RT);
    }

    return tex;
}

auto reflection_probe_component::get_cubemap_fbo(size_t face) -> const gfx::frame_buffer::ptr&
{
    // Bake scratch, deliberately auto-collected (the default), unlike the product
    // cubemaps in rview_: every cycle starts by recreating and clearing the faces
    // here, and only the prefilter reads them, at cycle end. The dormant-only purge
    // in update() keeps a stalled mid-cycle bake's captured faces intact.
    auto& fbo = face_rviews_[face].fbo_get_or_emplace("CUBEMAP", false);
    auto& tex = face_rviews_[face].tex_get_or_emplace("CUBEMAP_FACE", false);
    const uint16_t size = probe_resolution_to_size(resolution_);
    constexpr gfx::texture_format format = gfx::texture_format::RGBA16F;
    const bool recreate_texture = gfx::needs_recreate(tex, {size, size}, format);
    if(recreate_texture)
    {
        tex.reset();
        tex = std::make_shared<gfx::texture>(size,
                                             size,
                                             true,
                                             1,
                                             format,
                                             BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_RT | BGFX_TEXTURE_COMPUTE_WRITE);
   
        gfx::fbo_attachment att;
        att.layer = 0;
        att.texture = tex;
        att.generate_mips = true;

        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({att});
    }


    return fbo;
}

void reflection_probe_component::update(float dt)
{
    // Dormant path: no active bake. Drive the realtime refresh timer and release transient resources.
    if(!has_pending_bake_)
    {
        if(update_mode_ == probe_update_mode::realtime)
        {
            time_since_last_refresh_ += dt;
            if(time_since_last_refresh_ >= update_interval_)
            {
                // Realtime refreshes start a new bake; keep time-slicing (first_generation_ stays false).
                has_pending_bake_ = true;
                time_since_last_refresh_ = 0.0f;
            }

        }
        // The face views hold a full pipeline scratch set each (G-buffer, lighting
        // targets) from the last bake; nothing touches them between bakes, so they age
        // out here. Dormant-only on purpose: a bake stalled mid-cycle (probe culled
        // with faces half-emitted) must keep its captured faces for the prefilter.
        // The product cubemaps in rview_ opt out of collection at their creation
        // sites, so purging the view is safe.
        for(auto& face_rview : face_rviews_)
        {
            face_rview.release_unused(gfx::get_render_frame());
        }
        rview_.release_unused(gfx::get_render_frame());
        return;
    }

    // Active bake path: check whether all six faces have been emitted this cycle.
    bool fully_generated = true;
    for(auto& frame : generated_frame_)
    {
        fully_generated &= frame != uint64_t(-1);
    }

    if(fully_generated)
    {
        // Bake cycle finished. Reset per-frame tracking and go dormant; on_demand/once probes stay
        // quiet until someone calls mark_dirty() again, realtime probes will reschedule themselves.
        for(auto& frame : generated_frame_)
        {
            frame = uint64_t(-1);
        }
        first_generation_ = false;
        has_pending_bake_ = false;
        time_since_last_refresh_ = 0.0f;
    }

    generated_faces_count_ = 0;
}

void reflection_probe_component::release_resources()
{
    face_rviews_ = {};
    rview_ = {};
}

auto reflection_probe_component::get_probe() const -> const reflection_probe&
{
    return probe_;
}

void reflection_probe_component::set_probe(const reflection_probe& probe)
{
    if(probe == probe_)
    {
        return;
    }

    touch();

    probe_ = probe;

    // Edits to the probe data trigger an automatic rebuild, except for "on_demand" probes which
    // are fully manual: the owner must call mark_dirty() explicitly (e.g. from script or from the
    // editor Bake button). Switching INTO on_demand leaves the existing bake alone.
    if(update_mode_ != probe_update_mode::on_demand)
    {
        mark_dirty();
    }
}

void reflection_probe_component::set_update_mode(probe_update_mode mode)
{
    if(update_mode_ == mode)
    {
        return;
    }

    touch();

    const auto previous = update_mode_;
    update_mode_ = mode;
    time_since_last_refresh_ = 0.0f;

    // Transitioning out of on_demand means the probe should start honoring its schedule immediately,
    // so kick off a bake. Transitioning into on_demand stops future automatic bakes but leaves the
    // current cubemap intact.
    if(previous == probe_update_mode::on_demand && mode != probe_update_mode::on_demand)
    {
        mark_dirty();
    }
}

auto reflection_probe_component::already_generated() const -> bool
{
    // Short-circuit: probes with no outstanding bake are entirely "already generated" as far as
    // the pipeline is concerned - they do not need any per-face rendering this frame.
    if(!has_pending_bake_)
    {
        return true;
    }

    bool generated = true;
    for(size_t i = 0; i < generated_frame_.size(); ++i)
    {
        generated &= already_generated(i);
    }
    return generated;
}

auto reflection_probe_component::already_generated(size_t face) const -> bool
{
    if(!has_pending_bake_)
    {
        return true;
    }

    // Time-slice: once we've emitted the per-frame budget, treat remaining faces as done-for-now.
    // first_generation_ waives the budget so the initial bake produces a usable result in one frame.
    if(!first_generation_)
    {
        if(faces_per_frame_ > 0 && generated_faces_count_ >= faces_per_frame_)
        {
            return true;
        }
    }

    return generated_frame_[face] != uint64_t(-1);
}
void reflection_probe_component::set_generation_frame(size_t face, uint64_t frame)
{
    generated_frame_[face] = frame;
    generated_faces_count_++;
}

void reflection_probe_component::mark_dirty(bool force_full_first_frame)
{
    // Reset per-face tracking so the pipeline starts a new bake cycle.
    for(auto& frame : generated_frame_)
    {
        frame = uint64_t(-1);
    }
    generated_faces_count_ = 0;
    has_pending_bake_ = true;
    time_since_last_refresh_ = 0.0f;

    if(force_full_first_frame)
    {
        // first_generation_ semantics: when true, no per-frame face budget is enforced,
        // so the pipeline pushes all six faces in a single frame. Good for manual "Bake now".
        first_generation_ = true;
    }
}

auto reflection_probe_component::is_dirty() const -> bool
{
    return has_pending_bake_;
}

auto reflection_probe_component::is_bake_cycle_unstarted() const -> bool
{
    if(!has_pending_bake_)
    {
        return false;
    }

    for(const auto frame : generated_frame_)
    {
        if(frame != uint64_t(-1))
        {
            return false;
        }
    }

    return true;
}

auto reflection_probe_component::is_bake_complete() const -> bool
{
    if(!has_pending_bake_)
    {
        return true;
    }

    for(const auto frame : generated_frame_)
    {
        if(frame == uint64_t(-1))
        {
            return false;
        }
    }

    return true;
}

void reflection_probe_component::set_resolution(probe_resolution resolution)
{
    if(resolution_ == resolution)
    {
        return;
    }

    touch();
    resolution_ = resolution;
    release_resources();

    if(update_mode_ != probe_update_mode::on_demand)
    {
        mark_dirty();
    }
}

void reflection_probe_component::set_capture_sky(bool capture)
{
    if(capture_sky_ == capture)
    {
        return;
    }

    touch();
    capture_sky_ = capture;

    if(update_mode_ != probe_update_mode::on_demand)
    {
        mark_dirty();
    }
}

void reflection_probe_component::set_capture_shadows(bool capture)
{
    if(capture_shadows_ == capture)
    {
        return;
    }

    touch();
    capture_shadows_ = capture;

    if(update_mode_ != probe_update_mode::on_demand)
    {
        mark_dirty();
    }
}
} // namespace unravel
