#include "morph_deformer.h"

#include <engine/assets/asset_manager.h>
#include <engine/ecs/scene.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/systems/model_system.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/pipeline/passes/pass_helpers.h>
#include <engine/rendering/render_feature_support.h>

#include <graphics/graphics.h>
#include <hpp/optional.hpp>
#include <graphics/render_pass.h>
#include <graphics/vertex_buffer.h>
#include <logging/logging.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace unravel
{
namespace
{

/// Threads per group of the morph compute shaders (NUM_THREADS(64, 1, 1)).
constexpr uint32_t MORPH_GROUP_SIZE = 64;
/// The uints of a delta in the GPU copy (cs_morph_accumulate.eshader): slot, position (3 floats), packed normal.
constexpr uint32_t DELTA_UINTS = 5;
/// The accumulator's uints per slot: the position sum, then the normal sum.
constexpr uint32_t SLOT_UINTS = 6;
/// The deform stream's stride in uints.
constexpr uint32_t DEFORMED_UINTS = 6;
/// The largest integer one accumulated component reaches: room below 2^31 for the sum's rounding.
constexpr double MAX_ACCUMULATED = double(1u << 30);
/// A model's streams are released this many render frames after it last deformed.
constexpr uint32_t UNUSED_FRAMES = 120;

/// Whether @p layout keeps position and basis in its first 24 bytes, as the compute shaders read them.
auto has_mesh_vertex_prefix(const bgfx::VertexLayout& layout) -> bool
{
    return layout.has(bgfx::Attrib::Position) && layout.has(bgfx::Attrib::Normal) && layout.has(bgfx::Attrib::Tangent) &&
           layout.has(bgfx::Attrib::Bitangent) && layout.getOffset(bgfx::Attrib::Position) == 0 &&
           layout.getOffset(bgfx::Attrib::Normal) == 12 && layout.getOffset(bgfx::Attrib::Tangent) == 16 &&
           layout.getOffset(bgfx::Attrib::Bitangent) == 20 && layout.getStride() % 4 == 0;
}

/// The compute shaders read counts and offsets from float uniforms, which hold every integer up to 2^24 exactly.
constexpr uint64_t MAX_EXACT_FLOAT_INTEGER = uint64_t(1) << 24;

/// Whether the compute shaders can address every delta, slot and vertex of a mesh; a larger one deforms on the CPU.
auto has_exact_float_indices(const morph_target_data& targets, uint32_t vertex_count) -> bool
{
    return targets.deltas.size() <= MAX_EXACT_FLOAT_INTEGER && targets.morphed_vertices.size() <= MAX_EXACT_FLOAT_INTEGER &&
           vertex_count <= MAX_EXACT_FLOAT_INTEGER;
}

/// The vertices of @p mesh_layout's buffer read with position and basis skipped: no attribute is in two streams.
auto make_rest_layout(const bgfx::VertexLayout& mesh_layout) -> bgfx::VertexLayout
{
    struct attribute
    {
        bgfx::Attrib::Enum attrib;
        uint16_t offset;
    };
    std::vector<attribute> attributes;
    for(uint32_t a = 0; a < bgfx::Attrib::Count; ++a)
    {
        const auto attrib = bgfx::Attrib::Enum(a);
        if(mesh_layout.has(attrib))
        {
            attributes.push_back({attrib, mesh_layout.getOffset(attrib)});
        }
    }
    std::sort(attributes.begin(),
              attributes.end(),
              [](const attribute& lhs, const attribute& rhs)
              {
                  return lhs.offset < rhs.offset;
              });
    bgfx::VertexLayout layout;
    layout.begin();
    uint16_t written = 0;
    for(const auto& entry : attributes)
    {
        if(entry.offset > written)
        {
            layout.skip(uint8_t(entry.offset - written));
            written = entry.offset;
        }
        uint8_t num = 0;
        bgfx::AttribType::Enum type{};
        bool normalized = false;
        bool as_int = false;
        mesh_layout.decode(entry.attrib, num, type, normalized, as_int);
        const bool is_deformed = entry.attrib == bgfx::Attrib::Position || entry.attrib == bgfx::Attrib::Normal ||
                                 entry.attrib == bgfx::Attrib::Tangent || entry.attrib == bgfx::Attrib::Bitangent;
        // The size the attribute takes: bgfx pads 8 and 16-bit attributes to 4 bytes.
        bgfx::VertexLayout single;
        single.begin().add(entry.attrib, num, type, normalized, as_int).end();
        const uint16_t size = single.getStride();
        if(is_deformed)
        {
            layout.skip(uint8_t(size));
        }
        else
        {
            layout.add(entry.attrib, num, type, normalized, as_int);
        }
        written = uint16_t(written + size);
    }
    if(mesh_layout.getStride() > written)
    {
        layout.skip(uint8_t(mesh_layout.getStride() - written));
    }
    layout.end();
    return layout;
}

auto make_previous_layout() -> bgfx::VertexLayout
{
    bgfx::VertexLayout layout;
    const auto& deform = get_morph_deform_layout();
    layout.begin()
        .add(bgfx::Attrib::TexCoord7, 3, bgfx::AttribType::Float)
        .skip(uint8_t(deform.getStride() - 3 * sizeof(float)))
        .end();
    return layout;
}

/// The vertices a mesh's targets move, first to last, so the CPU path uploads one range.
auto get_morphed_range(const morph_target_data& targets) -> std::pair<uint32_t, uint32_t>
{
    if(targets.morphed_vertices.empty())
    {
        return {0, 0};
    }
    return {targets.morphed_vertices.front(), targets.morphed_vertices.back() + 1};
}

/// Whether @p weight moves its target's vertices: a weight that is not finite moves nothing, so no scale the pass
/// computes from the active weights is ever infinite or NaN.
auto is_active_morph_weight(float weight) -> bool
{
    return std::isfinite(weight) && std::abs(weight) > k_morph_weight_threshold;
}

auto has_weight(const std::vector<float>& weights) -> bool
{
    return std::any_of(weights.begin(), weights.end(), is_active_morph_weight);
}

void release_handle(bgfx::DynamicVertexBufferHandle& handle)
{
    if(bgfx::isValid(handle))
    {
        bgfx::destroy(handle);
        handle = BGFX_INVALID_HANDLE;
    }
}

void release_handle(bgfx::DynamicIndexBufferHandle& handle)
{
    if(bgfx::isValid(handle))
    {
        bgfx::destroy(handle);
        handle = BGFX_INVALID_HANDLE;
    }
}

void release_handle(bgfx::IndexBufferHandle& handle)
{
    if(bgfx::isValid(handle))
    {
        bgfx::destroy(handle);
        handle = BGFX_INVALID_HANDLE;
    }
}

/// Whether @p registry is the registry of a live scene: only then may its components be touched.
auto is_live_registry(const void* registry) -> bool
{
    const auto& scenes = scene::get_all_scenes();
    return std::any_of(scenes.begin(),
                       scenes.end(),
                       [registry](const scene* scn)
                       {
                           return scn != nullptr && scn->registry.get() == registry;
                       });
}

} // namespace

/// The GPU copy of a mesh's deltas and the vertex of each slot.
struct morph_deformer::target_buffers
{
    std::shared_ptr<const morph_target_data> targets;
    bgfx::IndexBufferHandle deltas = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle vertices = BGFX_INVALID_HANDLE;
    uint32_t last_used_frame{0};
};

/// One morphed model's streams.
struct morph_deformer::model_streams
{
    /// Kept alive while the streams deform it: draws compare it to the mesh they draw.
    std::shared_ptr<mesh> deformed_mesh;
    std::shared_ptr<const morph_target_data> targets;
    bgfx::VertexLayoutHandle rest_layout = BGFX_INVALID_HANDLE;
    std::array<bgfx::DynamicVertexBufferHandle, 2> streams{{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    /// Compute: the per-slot sums.
    bgfx::DynamicIndexBufferHandle accumulator = BGFX_INVALID_HANDLE;
    /// Whether the compute programs write the streams (else the CPU does).
    bool on_gpu{false};
    /// Compute: whether each stream still needs the rest vertices copied in.
    std::array<bool, 2> needs_rest{{true, true}};
    /// CPU: the deformed vertices, every one of the mesh's, at rest where no target moves them.
    std::vector<uint8_t> staging;
    std::vector<float> sums;
    /// The stream draws read this frame, and the one holding the shape they drew last frame: the same one while the
    /// weights hold still.
    uint32_t current{0};
    uint32_t previous{0};
    /// The weights version the current stream holds.
    uint64_t version{std::numeric_limits<uint64_t>::max()};
    uint32_t last_used_frame{0};
};

void morph_deformer::compute_programs::cache_uniforms()
{
    cache_uniform(rest.get(), u_morph_rest, "u_morph_rest", bgfx::UniformType::Vec4);
    cache_uniform(accumulate.get(), u_morph_accumulate, "u_morph_accumulate", bgfx::UniformType::Vec4);
    cache_uniform(finish.get(), u_morph_finish, "u_morph_finish", bgfx::UniformType::Vec4);
}

morph_deformer::morph_deformer() = default;

morph_deformer::~morph_deformer()
{
    deinit();
}

auto morph_deformer::init(rtti::context& ctx) -> bool
{
    previous_layout_ = make_previous_layout();
    previous_layout_handle_ = bgfx::createVertexLayout(previous_layout_);
    uses_compute_ = get_render_feature_support().is_available(render_feature::morph_compute);
    if(!uses_compute_)
    {
        return true;
    }
    auto& am = ctx.get_cached<asset_manager>();
    // Uniforms before programs: the OpenGL renderer wires a program's uniforms at link time.
    programs_.cache_uniforms();
    programs_.rest = std::make_unique<gpu_program>(am.get_asset<gfx::shader>("engine:/data/shaders/morph/cs_morph_rest.eshader"));
    programs_.accumulate =
        std::make_unique<gpu_program>(am.get_asset<gfx::shader>("engine:/data/shaders/morph/cs_morph_accumulate.eshader"));
    programs_.finish = std::make_unique<gpu_program>(am.get_asset<gfx::shader>("engine:/data/shaders/morph/cs_morph_finish.eshader"));
    clear_.init(ctx);
    const bool loaded = programs_.rest->is_valid() && programs_.accumulate->is_valid() && programs_.finish->is_valid() &&
                        clear_.is_ready();
    if(!loaded)
    {
        APPLOG_WARNING("The morph target compute programs failed to load, so morph targets deform on the CPU.");
        uses_compute_ = false;
    }
    return true;
}

void morph_deformer::deinit()
{
    for(auto& [key, streams] : streams_)
    {
        if(streams && is_live_registry(key.first))
        {
            auto& registry = *static_cast<entt::registry*>(const_cast<void*>(key.first));
            if(registry.valid(key.second))
            {
                if(auto* model = registry.try_get<model_component>(key.second))
                {
                    model->set_morph_deform_binding({});
                }
            }
        }
        if(streams)
        {
            destroy_streams(*streams);
        }
    }
    streams_.clear();
    for(auto& [data, buffers] : target_buffers_)
    {
        release_handle(buffers->deltas);
        release_handle(buffers->vertices);
    }
    target_buffers_.clear();
    for(auto& [hash, layout] : rest_layouts_)
    {
        if(bgfx::isValid(layout.second))
        {
            bgfx::destroy(layout.second);
        }
    }
    rest_layouts_.clear();
    if(bgfx::isValid(previous_layout_handle_))
    {
        bgfx::destroy(previous_layout_handle_);
        previous_layout_handle_ = BGFX_INVALID_HANDLE;
    }
    programs_.rest.reset();
    programs_.accumulate.reset();
    programs_.finish.reset();
    clear_.release();
    updated_registries_.clear();
}

auto morph_deformer::uses_compute() const -> bool
{
    return uses_compute_;
}

auto morph_deformer::get_rest_layout(const bgfx::VertexLayout& mesh_layout) -> bgfx::VertexLayoutHandle
{
    auto found = rest_layouts_.find(mesh_layout.m_hash);
    if(found == rest_layouts_.end())
    {
        auto layout = make_rest_layout(mesh_layout);
        const auto handle = bgfx::createVertexLayout(layout);
        found = rest_layouts_.emplace(mesh_layout.m_hash, std::make_pair(layout, handle)).first;
    }
    return found->second.second;
}

auto morph_deformer::get_target_buffers(const std::shared_ptr<const morph_target_data>& targets) -> target_buffers*
{
    auto& entry = target_buffers_[targets.get()];
    if(!entry)
    {
        entry = std::make_unique<target_buffers>();
        entry->targets = targets;
        std::vector<uint32_t> deltas;
        deltas.reserve(targets->deltas.size() * DELTA_UINTS);
        for(const auto& delta : targets->deltas)
        {
            uint32_t position[3];
            std::memcpy(position, delta.position.data(), sizeof(position));
            deltas.insert(deltas.end(), {delta.slot, position[0], position[1], position[2], delta.normal});
        }
        // A target that moves nothing still makes a valid buffer.
        if(deltas.empty())
        {
            deltas.assign(DELTA_UINTS, 0u);
        }
        entry->deltas = bgfx::createIndexBuffer(bgfx::copy(deltas.data(), uint32_t(deltas.size() * sizeof(uint32_t))),
                                                BGFX_BUFFER_COMPUTE_READ | BGFX_BUFFER_INDEX32);
        std::vector<uint32_t> vertices = targets->morphed_vertices;
        if(vertices.empty())
        {
            vertices.push_back(0);
        }
        entry->vertices = bgfx::createIndexBuffer(bgfx::copy(vertices.data(), uint32_t(vertices.size() * sizeof(uint32_t))),
                                                  BGFX_BUFFER_COMPUTE_READ | BGFX_BUFFER_INDEX32);
    }
    return entry.get();
}

auto morph_deformer::create_streams(const std::shared_ptr<mesh>& deformed_mesh,
                                    const std::shared_ptr<const morph_target_data>& targets) -> std::unique_ptr<model_streams>
{
    const auto& layout = deformed_mesh->get_vertex_format();
    const uint32_t vertex_count = deformed_mesh->get_vertex_count();
    const auto vb = deformed_mesh->get_hardware_vb();
    if(vertex_count == 0 || !vb || !vb->is_valid() || !has_mesh_vertex_prefix(layout))
    {
        return nullptr;
    }
    auto streams = std::make_unique<model_streams>();
    streams->deformed_mesh = deformed_mesh;
    streams->targets = targets;
    streams->rest_layout = get_rest_layout(layout);
    streams->on_gpu = uses_compute_ && has_exact_float_indices(*targets, vertex_count);
    const auto& deform_layout = get_morph_deform_layout();
    if(streams->on_gpu)
    {
        for(auto& stream : streams->streams)
        {
            // Readable too: the editor's selection wireframe pulls the deformed vertices from it as a buffer.
            stream = bgfx::createDynamicVertexBuffer(vertex_count, deform_layout, BGFX_BUFFER_COMPUTE_READ_WRITE);
        }
        const uint32_t sums = std::max<uint32_t>(1, uint32_t(targets->morphed_vertices.size()) * SLOT_UINTS);
        streams->accumulator = bgfx::createDynamicIndexBuffer(sums, BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_INDEX32);
    }
    else
    {
        const uint8_t* rest = deformed_mesh->get_system_vb();
        if(rest == nullptr)
        {
            return nullptr;
        }
        streams->staging.resize(size_t(vertex_count) * deform_layout.getStride());
        write_rest_vertices(layout, rest, vertex_count, streams->staging.data());
        for(auto& stream : streams->streams)
        {
            stream = bgfx::createDynamicVertexBuffer(bgfx::copy(streams->staging.data(), uint32_t(streams->staging.size())),
                                                     deform_layout);
        }
    }
    return streams;
}

void morph_deformer::destroy_streams(model_streams& streams)
{
    for(auto& stream : streams.streams)
    {
        release_handle(stream);
    }
    release_handle(streams.accumulator);
    streams.deformed_mesh.reset();
    streams.targets.reset();
}

auto morph_deformer::write_streams_on_cpu(model_streams& streams, uint32_t stream, const std::vector<float>& weights)
    -> bool
{
    const auto& targets = *streams.targets;
    const auto& mesh_layout = streams.deformed_mesh->get_vertex_format();
    const uint8_t* rest = streams.deformed_mesh->get_system_vb();
    if(rest == nullptr)
    {
        return false;
    }
    accumulate_morph_deltas(targets, weights, streams.sums);
    write_morphed_vertices(targets, mesh_layout, rest, streams.sums, streams.staging.data());
    const auto [first, end] = get_morphed_range(targets);
    if(end <= first)
    {
        return true;
    }
    const uint32_t stride = get_morph_deform_layout().getStride();
    bgfx::update(streams.streams[stream],
                 first,
                 bgfx::copy(streams.staging.data() + size_t(first) * stride, (end - first) * stride));
    return true;
}

auto morph_deformer::write_streams_on_gpu(model_streams& streams,
                                          uint32_t stream,
                                          const std::vector<float>& weights,
                                          uint16_t view) -> bool
{
    const auto& targets = *streams.targets;
    const auto vb = streams.deformed_mesh->get_hardware_vb();
    const auto& mesh_layout = streams.deformed_mesh->get_vertex_format();
    const uint32_t mesh_stride = mesh_layout.getStride() / 4;
    const uint32_t vertex_count = streams.deformed_mesh->get_vertex_count();
    const uint32_t slots = uint32_t(targets.morphed_vertices.size());
    if(!begin_program(programs_.rest) || !begin_program(programs_.accumulate) || !begin_program(programs_.finish) ||
       !clear_.begin() || !vb || !vb->is_valid())
    {
        return false;
    }
    auto* buffers = get_target_buffers(streams.targets);
    struct end_programs
    {
        compute_programs& programs;
        ~end_programs()
        {
            programs.rest->end();
            programs.accumulate->end();
            programs.finish->end();
        }
    } const ending{programs_};
    if(streams.needs_rest[stream])
    {
        bgfx::setBuffer(0, streams.streams[stream], bgfx::Access::Write);
        bgfx::setBuffer(1, vb->native_handle(), bgfx::Access::Read);
        gfx::set_uniform(programs_.u_morph_rest, math::vec4(float(vertex_count), float(mesh_stride), 0.0f, 0.0f));
        gfx::dispatch(view, programs_.rest->native_handle(), compute_group_count(vertex_count, MORPH_GROUP_SIZE), 1, 1);
        streams.needs_rest[stream] = false;
    }
    if(slots == 0)
    {
        return true;
    }
    // Scales that keep every sum below 2^30: each component of a slot's sum is at most the sum over the active targets
    // of |weight| times the largest move (normals: times 2).
    double position_reach = 0.0;
    double normal_reach = 0.0;
    for(size_t t = 0; t < targets.targets.size() && t < weights.size(); ++t)
    {
        if(is_active_morph_weight(weights[t]))
        {
            position_reach += std::abs(double(weights[t])) * double(targets.targets[t].max_displacement);
            normal_reach += std::abs(double(weights[t])) * 2.0;
        }
    }
    const double position_scale = MAX_ACCUMULATED / std::max(position_reach, 1e-9);
    const double normal_scale = MAX_ACCUMULATED / std::max(normal_reach, 1e-9);
    clear_.dispatch(view, streams.accumulator, 0, slots * SLOT_UINTS);
    for(size_t t = 0; t < targets.targets.size() && t < weights.size(); ++t)
    {
        const auto& target = targets.targets[t];
        if(!is_active_morph_weight(weights[t]) || target.delta_count == 0)
        {
            continue;
        }
        bgfx::setBuffer(0, buffers->deltas, bgfx::Access::Read);
        bgfx::setBuffer(1, streams.accumulator, bgfx::Access::ReadWrite);
        gfx::set_uniform(programs_.u_morph_accumulate,
                         math::vec4(float(target.first_delta),
                                    float(target.delta_count),
                                    float(double(weights[t]) * position_scale),
                                    float(double(weights[t]) * normal_scale)));
        gfx::dispatch(view,
                      programs_.accumulate->native_handle(),
                      compute_group_count(target.delta_count, MORPH_GROUP_SIZE),
                      1,
                      1);
    }
    bgfx::setBuffer(0, streams.accumulator, bgfx::Access::Read);
    bgfx::setBuffer(1, buffers->vertices, bgfx::Access::Read);
    bgfx::setBuffer(2, vb->native_handle(), bgfx::Access::Read);
    bgfx::setBuffer(3, streams.streams[stream], bgfx::Access::Write);
    gfx::set_uniform(programs_.u_morph_finish,
                     math::vec4(float(slots), float(mesh_stride), float(1.0 / position_scale), float(1.0 / normal_scale)));
    gfx::dispatch(view, programs_.finish->native_handle(), compute_group_count(slots, MORPH_GROUP_SIZE), 1, 1);
    return true;
}

auto morph_deformer::write_streams(model_streams& streams, uint32_t stream, const std::vector<float>& weights, uint16_t view)
    -> bool
{
    return streams.on_gpu ? write_streams_on_gpu(streams, stream, weights, view)
                          : write_streams_on_cpu(streams, stream, weights);
}

void morph_deformer::touch_streams(model_streams& streams, uint32_t frame)
{
    streams.last_used_frame = frame;
    if(streams.on_gpu)
    {
        get_target_buffers(streams.targets)->last_used_frame = frame;
    }
}

auto morph_deformer::make_binding(const model_streams& streams) const -> morph_deform_binding
{
    morph_deform_binding binding;
    binding.deformed_mesh = streams.deformed_mesh.get();
    binding.current = streams.streams[streams.current];
    binding.previous = streams.streams[streams.previous];
    binding.rest_layout = streams.rest_layout;
    binding.previous_layout = previous_layout_handle_;
    binding.is_shader_readable = streams.on_gpu;
    return binding;
}

auto morph_deformer::is_drawn_from(const model_component& model, const model_streams& streams) -> bool
{
    const auto* binding = model.get_morph_deform_binding();
    return binding != nullptr && binding->deformed_mesh == streams.deformed_mesh.get() &&
           binding->current.idx == streams.streams[streams.current].idx;
}

auto morph_deformer::is_updated_in(const void* registry_key, uint32_t frame) -> bool
{
    if(updated_frame_ != frame)
    {
        updated_frame_ = frame;
        updated_registries_.clear();
    }
    if(std::find(updated_registries_.begin(), updated_registries_.end(), registry_key) != updated_registries_.end())
    {
        return true;
    }
    updated_registries_.push_back(registry_key);
    return false;
}

void morph_deformer::update_model(const void* registry_key,
                                  entt::entity entity,
                                  model_component& model,
                                  uint32_t frame,
                                  uint16_t view)
{
    const auto targets = model.get_morph_targets();
    if(!targets)
    {
        model.set_morph_deform_binding({});
        return;
    }
    const std::pair<const void*, entt::entity> key{registry_key, entity};
    auto found = streams_.find(key);
    const auto lod = model.get_model().get_lod(0).get(false);
    if(found != streams_.end() && (found->second->deformed_mesh != lod || found->second->targets != targets))
    {
        // The mesh was replaced or reloaded: its streams go before new ones may reuse their handles.
        model.set_morph_deform_binding({});
        destroy_streams(*found->second);
        streams_.erase(found);
        found = streams_.end();
    }
    const uint64_t version = model.get_morph_weights_version();
    if(found != streams_.end() && found->second->version == version && is_drawn_from(model, *found->second))
    {
        // Still since last frame: the drawn stream is the previous shape too, and nothing is written.
        auto& streams = *found->second;
        streams.previous = streams.current;
        touch_streams(streams, frame);
        model.set_morph_deform_binding(make_binding(streams));
        return;
    }
    model.resolve_morph_weights(*targets, weights_);
    // A weight that is not finite (a broken file's default) moves nothing, on the CPU path as on the GPU's.
    std::replace_if(
        weights_.begin(),
        weights_.end(),
        [](float weight)
        {
            return !std::isfinite(weight);
        },
        0.0f);
    if(!has_weight(weights_))
    {
        // At rest: the mesh's own buffer draws it, and it batches again.
        model.set_morph_deform_binding({});
        return;
    }
    if(found == streams_.end())
    {
        auto created = create_streams(lod, targets);
        if(!created)
        {
            model.set_morph_deform_binding({});
            return;
        }
        found = streams_.emplace(key, std::move(created)).first;
    }
    auto& streams = *found->second;
    // Streams the off-screen gate skipped hold a shape from before the model left view: no motion from it.
    const bool was_skipped = frame - streams.last_used_frame > 1;
    touch_streams(streams, frame);
    if(was_skipped || !is_drawn_from(model, streams))
    {
        // New streams, a model back in view, a model that drew its mesh at rest last frame, or a new component on the
        // entity: one stream takes this frame's shape and stands for the previous one too, so the model has no motion
        // of its own.
        if(!write_streams(streams, streams.current, weights_, view))
        {
            model.set_morph_deform_binding({});
            return;
        }
        streams.previous = streams.current;
        streams.version = version;
    }
    else if(write_streams(streams, 1 - streams.current, weights_, view))
    {
        // The other stream takes this frame's shape; the one drawn last frame becomes the previous.
        streams.previous = streams.current;
        streams.current = 1 - streams.current;
        streams.version = version;
        model.mark_motion(true);
    }
    else
    {
        // The programs are not ready: last frame's shape stays, and the next frame writes this one.
        streams.previous = streams.current;
    }
    model.set_morph_deform_binding(make_binding(streams));
}

void morph_deformer::update(scene& scn)
{
    if(!scn.registry)
    {
        return;
    }
    const uint32_t frame = gfx::get_render_frame();
    const void* registry_key = scn.registry.get();
    if(is_updated_in(registry_key, frame))
    {
        return;
    }
    APP_SCOPE_PERF("Rendering/Morph Deform");

    // One view every frame, touched while idle, so the views after it keep their ids.
    gfx::render_pass pass("Morph Deform");
    pass.touch();

    // The cameras' frustums, gathered the first time a model off screen last frame needs them.
    hpp::optional<std::vector<math::frustum>> camera_frustums;
    auto is_entering_camera_view = [&](const model_component& model)
    {
        if(!camera_frustums)
        {
            camera_frustums = model_system::get_camera_frustums(scn);
        }
        return model_system::is_in_any_view(*camera_frustums, model.get_world_bounds());
    };
    scn.registry->view<model_component>().each(
        [&](entt::entity entity, model_component& model)
        {
            // A model no view drew last frame (off screen and outside every shadow) keeps its last shape and costs
            // nothing, unless it comes into a camera's view this frame: it takes its shape then, as skinning takes its
            // pose.
            if(!model.was_used_last_frame() && !is_entering_camera_view(model))
            {
                return;
            }
            update_model(registry_key, entity, model, frame, pass.id);
        });
    release_unused(frame);
}

void morph_deformer::release_unused(uint32_t frame)
{
    for(auto it = streams_.begin(); it != streams_.end();)
    {
        auto& streams = it->second;
        const bool is_unused = streams == nullptr || frame - streams->last_used_frame > UNUSED_FRAMES;
        if(!is_unused)
        {
            ++it;
            continue;
        }
        // A model still in a live scene draws its mesh at rest from now on; one whose scene is gone has no binding left.
        if(is_live_registry(it->first.first))
        {
            auto& registry = *static_cast<entt::registry*>(const_cast<void*>(it->first.first));
            if(registry.valid(it->first.second))
            {
                if(auto* model = registry.try_get<model_component>(it->first.second))
                {
                    const auto* binding = model->get_morph_deform_binding();
                    if(binding != nullptr && streams != nullptr && binding->current.idx == streams->streams[streams->current].idx)
                    {
                        model->set_morph_deform_binding({});
                    }
                }
            }
        }
        if(streams)
        {
            destroy_streams(*streams);
        }
        it = streams_.erase(it);
    }
    for(auto it = target_buffers_.begin(); it != target_buffers_.end();)
    {
        if(frame - it->second->last_used_frame > UNUSED_FRAMES)
        {
            release_handle(it->second->deltas);
            release_handle(it->second->vertices);
            it = target_buffers_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

} // namespace unravel
