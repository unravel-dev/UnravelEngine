#include "surface_cache_system.h"

#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/model.h>

#include <logging/logging.h>

namespace
{
namespace ANONYMOUS
{
/// Layout of the instance buffer: a flat array of vec4, matching BUFFER_RO(_, vec4, _).
auto get_vec4_buffer_layout() -> const gfx::vertex_layout&
{
    static const gfx::vertex_layout layout = []()
    {
        gfx::vertex_layout decl;
        decl.begin().add(gfx::attribute::TexCoord0, 4, gfx::attribute_type::Float).end();
        return decl;
    }();
    return layout;
}

/// Writes row @p row of an affine transform as a vec4. glm stores column-major with
/// m[column][row], so a row is gathered across columns.
void write_affine_row(float* dst, const ::math::mat4& m, int row)
{
    dst[0] = m[0][row];
    dst[1] = m[1][row];
    dst[2] = m[2][row];
    dst[3] = m[3][row];
}
} // namespace ANONYMOUS
} // namespace

namespace unravel
{

auto surface_cache_system::init(rtti::context& ctx) -> bool
{
    // The whole feature is compute-shaped: compose, attributes, light voxels and probes are
    // dispatches, and even the debug views read SSBOs. A backend without compute (measured:
    // Mesa handing bgfx a GL 3.1 compatibility context) cannot run ANY of it - the dispatches
    // are silently dropped, every volume keeps its allocation garbage, and the views paint
    // that garbage with nothing in the log to say why. Refusing loudly here is the honest
    // degradation: no GI, no debug views, one line naming the reason.
    const auto* caps = bgfx::getCaps();
    supported_ = caps != nullptr && 0 != (caps->supported & BGFX_CAPS_COMPUTE);
    if(!supported_)
    {
        APPLOG_WARNING("[SurfaceCache] GI disabled: this renderer backend reports no compute "
                       "shader support. The surface cache, its debug views and every GI pass "
                       "stay off; use the Vulkan backend for GI on this machine.");
        return true;
    }
    sdf_atlas::settings atlas_settings;
    if(!atlas_.init(atlas_settings))
    {
        APPLOG_WARNING("[SurfaceCache] Atlas initialisation failed. Surface cache GI is unavailable.");
        return false;
    }
    sdf_instance_grid::settings grid_settings;
    grid_.init(grid_settings);
    if(!light_buffer_.init())
    {
        APPLOG_WARNING("[SurfaceCache] Light buffer initialisation failed. Traced hits cannot be lit.");
    }
    // Texture means. Never seeded from the CPU (bgfx forbids CPU updates on compute-writable
    // buffers): an instance carries slot 0 until its texture's capture has WRITTEN its slot,
    // and the attribute composer skips the multiply for slot 0 - so an unwritten slot is
    // never read at all.
    texture_mean_buffer_ = gfx::create_dynamic_vertex_buffer(texture_mean_capacity,
                                                             ANONYMOUS::get_vec4_buffer_layout(),
                                                             BGFX_BUFFER_COMPUTE_READ_WRITE);
    if(!bgfx::isValid(texture_mean_buffer_))
    {
        APPLOG_WARNING("[SurfaceCache] Texture mean buffer allocation failed. Bounce albedo "
                       "falls back to base colour factors.");
    }
    return true;
}

auto surface_cache_system::deinit(rtti::context& ctx) -> bool
{
    instances_.clear();
    clipmap_instances_.clear();
    clipmap_keepalive_.clear();
    residency_.clear();
    if(bgfx::isValid(instance_buffer_))
    {
        gfx::destroy(instance_buffer_);
        instance_buffer_ = {bgfx::kInvalidHandle};
    }
    if(bgfx::isValid(texture_mean_buffer_))
    {
        gfx::destroy(texture_mean_buffer_);
        texture_mean_buffer_ = {bgfx::kInvalidHandle};
    }
    texture_mean_slots_.clear();
    pending_texture_means_.clear();
    next_texture_mean_slot_ = 1;
    texture_mean_overflow_warned_ = false;
    instance_buffer_capacity_ = 0;
    instance_data_.clear();
    if(bgfx::isValid(grid_offset_buffer_))
    {
        gfx::destroy(grid_offset_buffer_);
        grid_offset_buffer_ = {bgfx::kInvalidHandle};
    }
    if(bgfx::isValid(grid_instance_buffer_))
    {
        gfx::destroy(grid_instance_buffer_);
        grid_instance_buffer_ = {bgfx::kInvalidHandle};
    }
    grid_offset_capacity_ = 0;
    grid_instance_capacity_ = 0;
    grid_bounds_.clear();
    grid_params_.fill(0.0f);
    light_buffer_.shutdown();
    atlas_.shutdown();
    return true;
}

auto surface_cache_system::acquire_field(const hpp::uuid& mesh_uid, const mesh& m, uint32_t submesh_index)
    -> uint32_t
{
    // Keyed by submesh as well as mesh: each submesh has its own field, and they are uploaded to
    // the atlas independently.
    const field_key key{mesh_uid, submesh_index};
    auto& record = residency_[key];
    // Stamped before any early return, including the failures. The sweep releases whatever was not
    // asked for this frame, and a mesh that is present but currently unuploadable is still asked
    // for -- forgetting to stamp it would make the sweep drop the record and lose the fact that it
    // has no field, so the whole check would run again from scratch next frame.
    record.last_used_frame = world_frame_;
    if(record.has_no_field)
    {
        return sdf_atlas::invalid_index;
    }
    if(record.header_index != sdf_atlas::invalid_index)
    {
        return record.header_index;
    }
    const auto& sdf = m.get_sdf(submesh_index);
    if(!sdf.is_valid())
    {
        // No baked field: either the asset opted out or the bake could not produce one. That is a
        // property of the mesh and can never change while it is loaded, so it is recorded and not
        // retried for the rest of the session.
        record.has_no_field = true;
        return sdf_atlas::invalid_index;
    }
    // Name the phantom fields, once each: a shell floored this fat means the geometry is far
    // below its own field's resolution (a rope or curtain submesh whose bounds span a building
    // bakes metre voxels), and the result is not a bad field but a PHANTOM - a metre-thick blob
    // that occludes rays and steals attribution over a whole neighbourhood (measured: Sponza's
    // parapet ropes painting the gallery floor red in the albedo view). The bake cannot do
    // better at that voxel size; the mesh needs a finer SDF resolution in its import settings,
    // or to opt out of GI entirely.
    if(sdf.is_two_sided && sdf.two_sided_thickness > 0.25f && !record.thickness_warned)
    {
        record.thickness_warned = true;
        APPLOG_WARNING("[SurfaceCache] Mesh {} submesh {} bakes a {:.2f} m thick shell "
                       "({}x{}x{} voxels of {:.2f} m): thin geometry this far below its field's "
                       "resolution becomes a phantom occluder and steals GI attribution nearby. "
                       "Raise its SDF resolution or exclude it from GI in the import settings.",
                       hpp::to_string(mesh_uid),
                       submesh_index,
                       sdf.two_sided_thickness,
                       sdf.grid_dim.x,
                       sdf.grid_dim.y,
                       sdf.grid_dim.z,
                       sdf.voxel_size);
    }
    // Budget deferral BEFORE the refusal cache: a field deferred to keep this frame's uploads
    // inside the renderer's staging scratch is not refused - it must retry next frame, so it
    // must NOT consume the generation stamp below (that stamp would silence the retry until
    // something unrelated released bricks).
    if(!atlas_.has_upload_budget(sdf))
    {
        return sdf_atlas::invalid_index;
    }
    // Reached on first use, or after a previous attempt was refused for want of atlas room. A
    // refusal describes the atlas at a moment rather than the mesh, so it must be retried once the
    // sweep frees the previous scene's fields -- but ONLY then. Nothing else can change the answer,
    // and retrying unconditionally is what a scene that overruns the atlas turns into thousands of
    // doomed uploads per frame, climbing the refusal counters into the billions.
    const uint32_t generation = atlas_.get_release_generation();
    if(record.attempt_generation == generation)
    {
        return sdf_atlas::invalid_index;
    }
    record.attempt_generation = generation;
    record.header_index = atlas_.upload(sdf);
    return record.header_index;
}

void surface_cache_system::release_unused_fields()
{
    APP_SCOPE_PERF("GI/SurfaceCache/Release Unused Fields");
    for(auto it = residency_.begin(); it != residency_.end();)
    {
        if(it->second.last_used_frame == world_frame_)
        {
            ++it;
            continue;
        }
        if(it->second.header_index != sdf_atlas::invalid_index)
        {
            atlas_.release(it->second.header_index);
        }
        it = residency_.erase(it);
    }
}


auto surface_cache_system::resolve_submesh_material(const model& mdl,
                                                     const model_component& model_comp,
                                                     const mesh& m,
                                                     uint32_t submesh_index) -> material::sptr
{
    // Per-submesh override first, exactly as the renderer does: an overridden submesh is painted
    // with the override, so that is the colour its bounced light must carry.
    const auto& overrides = model_comp.get_submesh_material_overrides();
    if(submesh_index < overrides.size() && overrides[submesh_index])
    {
        return overrides[submesh_index];
    }
    // Otherwise the material of the submesh's DATA GROUP, which is its material index. Submeshes
    // sharing a material share this entry, which is correct: they are painted the same.
    const auto* sub = m.get_submesh(submesh_index, 0);
    if(sub == nullptr)
    {
        return {};
    }
    return mdl.get_material_instance(sub->data_group_id);
}

auto surface_cache_system::acquire_texture_mean_slot(const asset_handle<gfx::texture>& color_map,
                                                      bool& out_captured) -> uint32_t
{
    out_captured = false;
    if(!color_map.is_valid() || !bgfx::isValid(texture_mean_buffer_))
    {
        return 0;
    }
    auto [it, inserted] = texture_mean_slots_.try_emplace(color_map.uid());
    auto& entry = it->second;
    if(inserted)
    {
        if(next_texture_mean_slot_ < texture_mean_capacity)
        {
            entry.slot = next_texture_mean_slot_++;
        }
        else
        {
            // Slot 0 is the seeded white, so overflow degrades to factor-only albedo.
            entry.slot = 0;
            entry.queued = true;
            entry.captured = true;
            if(!texture_mean_overflow_warned_)
            {
                texture_mean_overflow_warned_ = true;
                APPLOG_WARNING("[SurfaceCache] More than {} distinct colour maps; further "
                               "textures bounce their base colour factor only.",
                               texture_mean_capacity - 1);
            }
        }
    }
    if(!entry.queued && color_map.is_ready())
    {
        // Queue only once the texture is actually RESIDENT. The readiness gate is load-bearing:
        // get(false) returns a default-constructed placeholder for a still-streaming asset, not
        // null - capturing that would write a black mean and poison the slot for the session.
        auto texture = color_map.get(false);
        if(texture && texture->is_valid())
        {
            pending_texture_means_.push_back({texture, entry.slot});
            entry.queued = true;
        }
    }
    out_captured = entry.captured;
    // Slot 0 (the composer's skip-the-multiply case) until the capture has written the slot:
    // the mean buffer is compute-write-only, so there is no seeded value to read before then.
    return entry.captured ? entry.slot : 0u;
}

auto surface_cache_system::take_texture_mean_captures(uint32_t budget)
    -> std::vector<texture_mean_capture>
{
    std::vector<texture_mean_capture> captures;
    while(!pending_texture_means_.empty() && uint32_t(captures.size()) < budget)
    {
        texture_mean_capture capture = pending_texture_means_.front();
        pending_texture_means_.erase(pending_texture_means_.begin());
        // Cannot fire after the is_ready gate in acquire, but a slot must NEVER be marked
        // captured without its dispatch actually running - a marked-but-unwritten slot reads
        // garbage into every albedo that uses it.
        if(!capture.texture || !capture.texture->is_valid())
        {
            continue;
        }
        for(auto& [uid, entry] : texture_mean_slots_)
        {
            if(entry.slot == capture.slot)
            {
                entry.captured = true;
            }
        }
        captures.push_back(std::move(capture));
    }
    return captures;
}

void surface_cache_system::add_instance(uint32_t header_index,
                                         const mesh_sdf& sdf,
                                         const math::mat4& local_to_world,
                                         const std::shared_ptr<mesh>& owner,
                                         const material::sptr& mat)
{
    instance inst;
    // Colour lives on pbr_material, not on the material base. A material of some other kind keeps
    // the neutral default rather than guessing, which is the same answer this had before and is
    // strictly better than tinting the scene with a colour nothing is painted with.
    if(const auto* pbr = dynamic_cast<const pbr_material*>(mat.get()))
    {
        const auto& base_color = pbr->get_base_color();
        inst.albedo = math::vec3(base_color.value.r, base_color.value.g, base_color.value.b);
        inst.mean_slot = acquire_texture_mean_slot(pbr->get_color_map(), inst.mean_captured);
        // Pre-multiplied by intensity, so the shader stores radiance directly and never has to
        // know that emission is authored as a colour and a separate scale.
        const auto& emissive_color = pbr->get_emissive_color();
        inst.emissive = math::vec3(emissive_color.value.r, emissive_color.value.g, emissive_color.value.b) *
                        pbr->get_emissive_intensity();
    }
    inst.local_to_world = local_to_world;
    // glm::inverse explicitly: namespace math declares its own inverse() for math::transform,
    // which hides the glm overloads from qualified lookup as math::inverse.
    inst.world_to_local = glm::inverse(local_to_world);
    inst.header_index = header_index;
    // Scale is recovered from the matrix rather than from a transform object, because the
    // renderer hands out plain matrices for submesh nodes.
    const float scale_x = math::length(math::vec3(local_to_world[0]));
    const float scale_y = math::length(math::vec3(local_to_world[1]));
    const float scale_z = math::length(math::vec3(local_to_world[2]));
    // Smallest axis, not average: a distance measured in local space maps to at least this
    // much world distance, so using it keeps every step an under-estimate. The largest or the
    // mean would let a sphere trace overshoot a non-uniformly scaled instance and pass
    // through it.
    inst.local_to_world_scale = math::max(math::min(scale_x, math::min(scale_y, scale_z)), 1e-6f);
    // World-space AABB of the field's local bounds, for the tracer's broad phase. Built from
    // the transformed corners so a rotated instance still gets a bound that contains it.
    inst.world_bounds.reset();
    const auto corners = sdf.bounds.get_corners();
    for(const auto& corner : corners)
    {
        const math::vec4 world_corner = local_to_world * math::vec4(corner, 1.0f);
        inst.world_bounds.add_point(math::vec3(world_corner));
    }
    instances_.push_back(inst);
    // The clipmap composer borrows a raw mesh_sdf pointer, so the owning mesh has to be kept
    // alive for as long as the composition input list references it.
    clipmap_keepalive_.push_back(owner);
    global_sdf_instance clipmap_instance;
    clipmap_instance.sdf = &sdf;
    clipmap_instance.world_to_local = inst.world_to_local;
    clipmap_instance.world_bounds = inst.world_bounds;
    clipmap_instance.local_to_world_scale = inst.local_to_world_scale;
    // Attribute-voxel material (GI v2 plan 3.1): the same per-submesh values the tracer's
    // instance buffer carries, so a cascade surface voxel and a near-field hit agree on what
    // the surface looks like.
    clipmap_instance.albedo = inst.albedo;
    clipmap_instance.emissive = inst.emissive;
    clipmap_instance.mean_slot = inst.mean_slot;
    clipmap_instance.mean_captured = inst.mean_captured;
    clipmap_instances_.push_back(clipmap_instance);
}

void surface_cache_system::upload_instance_grid()
{
    APP_SCOPE_PERF("GI/SurfaceCache/Upload Instance Grid");
    // The grid is a pure function of the instance set (bounds are part of the packed data the
    // fingerprint covers), so an unchanged fingerprint means an identical grid: skip the CPU
    // rebuild and the multi-megabyte re-upload. Without this a static scene re-staged the whole
    // structure every frame - at Bistro scale that alone kept the Vulkan backend allocating
    // staging memory continuously.
    if(grid_uploaded_fingerprint_ == instance_fingerprint_ && grid_.is_valid() &&
       bgfx::isValid(grid_offset_buffer_) && bgfx::isValid(grid_instance_buffer_))
    {
        return;
    }
    grid_uploaded_fingerprint_ = instance_fingerprint_;
    grid_bounds_.clear();
    grid_bounds_.reserve(instances_.size());
    for(const auto& inst : instances_)
    {
        grid_bounds_.push_back(inst.world_bounds);
    }
    grid_.build(grid_bounds_);
    // w of the second vec4 gates the whole tier. Zeroed first so every early-out below leaves the
    // grid switched off rather than pointing a tracer at a stale structure -- a tracer that walks
    // last frame's cells finds last frame's instances, which is worse than not culling at all.
    grid_params_.fill(0.0f);
    if(!grid_.is_valid())
    {
        return;
    }
    const auto& offsets = grid_.get_cell_offsets();
    const auto& cell_instances = grid_.get_cell_instances();
    const auto ensure_capacity = [](gfx::dynamic_index_buffer_handle& buffer,
                                    uint32_t& capacity,
                                    uint32_t required) -> void
    {
        if(bgfx::isValid(buffer) && required <= capacity)
        {
            return;
        }
        if(bgfx::isValid(buffer))
        {
            gfx::destroy(buffer);
        }
        // Grow with slack, so a scene gaining a few instances per frame does not recreate the
        // buffer every frame.
        capacity = required + required / 2u + 64u;
        buffer = gfx::create_dynamic_index_buffer(capacity, BGFX_BUFFER_COMPUTE_READ | BGFX_BUFFER_INDEX32);
    };
    ensure_capacity(grid_offset_buffer_, grid_offset_capacity_, math::max(uint32_t(offsets.size()), 1u));
    ensure_capacity(grid_instance_buffer_,
                    grid_instance_capacity_,
                    math::max(uint32_t(cell_instances.size()), 1u));
    if(!bgfx::isValid(grid_offset_buffer_) || !bgfx::isValid(grid_instance_buffer_))
    {
        return;
    }
    gfx::update(grid_offset_buffer_, 0, gfx::copy(offsets.data(), uint32_t(offsets.size() * sizeof(uint32_t))));
    if(!cell_instances.empty())
    {
        gfx::update(grid_instance_buffer_,
                    0,
                    gfx::copy(cell_instances.data(), uint32_t(cell_instances.size() * sizeof(uint32_t))));
    }
    const auto& origin = grid_.get_origin();
    const auto& dim = grid_.get_dim();
    grid_params_[0] = origin.x;
    grid_params_[1] = origin.y;
    grid_params_[2] = origin.z;
    grid_params_[3] = grid_.get_cell_size();
    grid_params_[4] = float(dim.x);
    grid_params_[5] = float(dim.y);
    grid_params_[6] = float(dim.z);
    grid_params_[7] = 1.0f;
}

void surface_cache_system::upload_instances()
{
    APP_SCOPE_PERF("GI/SurfaceCache/Upload Instances");
    instance_data_.assign(size_t(instances_.size()) * instance_vec4_stride * 4u, 0.0f);
    for(size_t i = 0; i < instances_.size(); ++i)
    {
        const auto& inst = instances_[i];
        float* dst = instance_data_.data() + i * instance_vec4_stride * 4u;
        ANONYMOUS::write_affine_row(dst + 0, inst.world_to_local, 0);
        ANONYMOUS::write_affine_row(dst + 4, inst.world_to_local, 1);
        ANONYMOUS::write_affine_row(dst + 8, inst.world_to_local, 2);
        ANONYMOUS::write_affine_row(dst + 12, inst.local_to_world, 0);
        ANONYMOUS::write_affine_row(dst + 16, inst.local_to_world, 1);
        ANONYMOUS::write_affine_row(dst + 20, inst.local_to_world, 2);
        dst[24] = inst.world_bounds.min.x;
        dst[25] = inst.world_bounds.min.y;
        dst[26] = inst.world_bounds.min.z;
        dst[27] = float(inst.header_index);
        dst[28] = inst.world_bounds.max.x;
        dst[29] = inst.world_bounds.max.y;
        dst[30] = inst.world_bounds.max.z;
        dst[31] = inst.local_to_world_scale;
        dst[32] = inst.albedo.x;
        dst[33] = inst.albedo.y;
        dst[34] = inst.albedo.z;
        dst[35] = float(inst.mean_slot);
        dst[36] = inst.emissive.x;
        dst[37] = inst.emissive.y;
        dst[38] = inst.emissive.z;
        dst[39] = 0.0f;
    }
    // FNV-1a over the exact bytes the GPU receives, the light buffer's convention: any change
    // to a transform, material colour, bounds, or field index flips it.
    uint64_t fingerprint = 1469598103934665603ull;
    const auto* bytes = reinterpret_cast<const uint8_t*>(instance_data_.data());
    for(size_t i = 0; i < instance_data_.size() * sizeof(float); ++i)
    {
        fingerprint = (fingerprint ^ bytes[i]) * 1099511628211ull;
    }
    const uint32_t required_vec4 = math::max(uint32_t(instances_.size()) * instance_vec4_stride, 1u);
    bool recreated = false;
    if(!bgfx::isValid(instance_buffer_) || required_vec4 > instance_buffer_capacity_)
    {
        if(bgfx::isValid(instance_buffer_))
        {
            gfx::destroy(instance_buffer_);
        }
        // Grow with slack so a scene gaining a few instances per frame does not recreate the
        // buffer every frame.
        instance_buffer_capacity_ = required_vec4 + required_vec4 / 2u + 64u;
        instance_buffer_ = gfx::create_dynamic_vertex_buffer(instance_buffer_capacity_,
                                                             ANONYMOUS::get_vec4_buffer_layout(),
                                                             BGFX_BUFFER_COMPUTE_READ);
        recreated = true;
    }
    // Re-upload only what changed: a static scene keeps its instance set byte-identical frame
    // to frame, and re-staging it anyway is pure allocator pressure (see upload_instance_grid).
    if(!instance_data_.empty() && (recreated || fingerprint != instance_fingerprint_))
    {
        gfx::update(instance_buffer_,
                    0,
                    gfx::copy(instance_data_.data(), uint32_t(instance_data_.size() * sizeof(float))));
    }
    instance_fingerprint_ = fingerprint;
}

void surface_cache_system::update_world(scene& scn)
{
    APP_SCOPE_PERF("GI/SurfaceCache/Update World");
    // The debug views keep this alive even with GI off, but an unsupported backend has nothing
    // to keep alive: no dispatch downstream can consume what this uploads.
    if(!supported_)
    {
        return;
    }
    // Once per frame, however many cameras ask. All of this is a function of the scene, so a second
    // camera would rebuild an identical instance list and re-upload an identical grid.
    const uint64_t frame = uint64_t(gfx::get_render_frame());
    if(world_frame_ == frame)
    {
        return;
    }
    world_frame_ = frame;
    instances_.clear();
    clipmap_instances_.clear();
    clipmap_keepalive_.clear();
    if(!is_enabled())
    {
        return;
    }
    scn.registry->view<transform_component, model_component, active_component>().each(
        [&](auto entity, auto&& transform_comp, auto&& model_comp, auto&& active)
        {
            const auto& mdl = model_comp.get_model();
            if(!mdl.is_valid())
            {
                return;
            }
            // LOD0 always. The field is already a coarse approximation of the surface, so
            // tracing against a simplified LOD would compound two independent approximations
            // and make occlusion depend on camera distance -- which would break world-space
            // stability, since the same wall would occlude differently as the camera moves.
            const auto mesh_handle = mdl.get_lod(0);
            if(!mesh_handle.is_ready())
            {
                return;
            }
            const auto mesh_ptr = mesh_handle.get();
            if(!mesh_ptr)
            {
                return;
            }
            // Place each submesh's OWN field wherever that submesh is DRAWN, which means making
            // the same per-submesh decision model::submit makes (see the has_transforms branch
            // in submit_for_batching):
            //
            //   - A submesh with mapped node transforms is drawn once at each of them. A model
            //     is an entity hierarchy, and the geometry hangs off child entities carrying
            //     submesh_component; model::submit uses those children's global transforms
            //     DIRECTLY, without composing them with the root, and importers routinely bake
            //     an axis convention into that child node. The transforms also differ BETWEEN
            //     submeshes on a real model, so one whole-mesh field placed at each transform
            //     in turn would duplicate the entire model once per transform.
            //   - A submesh with none is drawn at the model's own transform.
            //
            // The test has to be on THIS submesh's transform list. Testing whether the outer
            // list is populated instead reads as "the hierarchy resolved" and is true for a
            // primitive, whose pose is sized to the submesh count but never mapped, because
            // nothing carries a submesh_component -- so every primitive silently vanished from
            // GI while still rendering normally.
            const auto& submesh_transforms = model_comp.get_submesh_transforms();
            const math::mat4& world_transform = transform_comp.get_transform_global().get_matrix();
            const uint32_t sdf_count = mesh_ptr->get_sdf_count();
            for(uint32_t submesh_index = 0; submesh_index < sdf_count; ++submesh_index)
            {
                const uint32_t header_index = acquire_field(mesh_handle.uid(), *mesh_ptr, submesh_index);
                if(header_index == sdf_atlas::invalid_index)
                {
                    continue;
                }
                const auto& sdf = mesh_ptr->get_sdf(submesh_index);
                // Resolved once per submesh rather than per placement: every instance of a
                // submesh is drawn with the same material.
                const auto mat = resolve_submesh_material(mdl, model_comp, *mesh_ptr, submesh_index);
                if(!submesh_transforms.has_transforms(submesh_index))
                {
                    add_instance(header_index, sdf, world_transform, mesh_ptr, mat);
                    continue;
                }
                const size_t transform_count = submesh_transforms.get_transform_count(submesh_index);
                for(size_t instance_index = 0; instance_index < transform_count; ++instance_index)
                {
                    // Null for an inactive or out-of-range instance, which is the same accessor
                    // and therefore the same answer the renderer gets: a submesh switched off is
                    // not drawn, so it must not occlude or bounce light either.
                    const math::mat4* transform_ptr =
                        submesh_transforms.get_transform(submesh_index, instance_index);
                    if(transform_ptr == nullptr)
                    {
                        continue;
                    }
                    add_instance(header_index, sdf, *transform_ptr, mesh_ptr, mat);
                }
            }
        });
    // Swept AFTER the walk, so the set of what is still wanted is complete. The cost is that a
    // scene change takes one extra frame to show GI: the incoming meshes are refused while the
    // outgoing ones still hold their bricks, and succeed on the retry once this has run. Sweeping
    // first would need the whole scene walked twice to know what to keep.
    release_unused_fields();
    atlas_.flush();
    light_buffer_.update(scn);
    upload_instances();
    upload_instance_grid();
    // The cascade is deliberately NOT composed here. It is centred on a viewer, so it belongs to
    // the camera rather than to the world -- see surface_cache_view.
}

} // namespace unravel
