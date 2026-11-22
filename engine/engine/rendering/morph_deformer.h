#pragma once

#include <engine/rendering/gpu_program.h>
#include <engine/rendering/morph_targets.h>
#include <engine/rendering/pipeline/passes/buffer_clear.h>

#include <context/context.hpp>
#include <entt/entity/fwd.hpp>

#include <array>
#include <map>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace unravel
{
class mesh;
class model_component;
struct scene;

/**
 * @brief The morph deform pass: once a render frame, writes the morphed vertices of every model whose weights move its
 *        mesh into deform streams the model's draws bind (morph_deform_binding), so every pass and view reads one
 *        result and no mesh shader changes.
 *
 * Each morphed model gets two streams of 24-byte vertices (position and packed basis) that take turns. A weight change
 * writes the stream the draws did not read last frame, and the velocity pass reads the other one as the previous
 * shape; while the weights hold still both name the same stream, so a still model costs nothing. A model that did not
 * draw from its streams last frame (a new one, or one that drew its mesh at rest) writes one stream for both, so it
 * has no motion of its own that frame. A model no view drew last frame (off screen and outside every shadow) is not
 * deformed: it keeps the shape it left with until it comes back, and takes its shape the frame it enters a camera's
 * view (in a shadow alone, one frame late), with no motion of its own. A model whose weights are all 0 draws its mesh at
 * rest and keeps batching. With
 * compute, three dispatches per write accumulate every active target's deltas with integer atomics and finish the
 * vertices; without it (WebGL 2) the CPU does the same and uploads the range of vertices the targets move.
 */
class morph_deformer
{
public:
    morph_deformer();
    ~morph_deformer();
    morph_deformer(const morph_deformer&) = delete;
    auto operator=(const morph_deformer&) -> morph_deformer& = delete;

    /// Loads the compute programs where the device runs them.
    auto init(rtti::context& ctx) -> bool;
    /// Releases every stream and program; call it before the renderer shuts down.
    void deinit();

    /// Deforms the morphed models of @p scn, once per render frame for each scene, after the models' frame update.
    void update(scene& scn);

    /// Whether the GPU deforms (else the CPU does).
    auto uses_compute() const -> bool;

private:
    struct target_buffers;
    struct model_streams;

    auto get_target_buffers(const std::shared_ptr<const morph_target_data>& targets) -> target_buffers*;
    auto get_rest_layout(const bgfx::VertexLayout& mesh_layout) -> bgfx::VertexLayoutHandle;
    auto create_streams(const std::shared_ptr<mesh>& deformed_mesh, const std::shared_ptr<const morph_target_data>& targets)
        -> std::unique_ptr<model_streams>;
    void destroy_streams(model_streams& streams);
    /// Whether this frame's shape went into @p stream; false while the compute programs are not ready.
    auto write_streams(model_streams& streams, uint32_t stream, const std::vector<float>& weights, uint16_t view) -> bool;
    auto write_streams_on_cpu(model_streams& streams, uint32_t stream, const std::vector<float>& weights) -> bool;
    auto write_streams_on_gpu(model_streams& streams, uint32_t stream, const std::vector<float>& weights, uint16_t view)
        -> bool;
    /// Marks @p streams, and the GPU copy of their deltas, used in @p frame.
    void touch_streams(model_streams& streams, uint32_t frame);
    /// What the draws of a model bind to read @p streams.
    auto make_binding(const model_streams& streams) const -> morph_deform_binding;
    /// Whether @p model drew from @p streams last frame: its binding names their current stream.
    static auto is_drawn_from(const model_component& model, const model_streams& streams) -> bool;
    /// Deforms @p model, one of the models of the registry @p registry_key, in render frame @p frame.
    void update_model(const void* registry_key, entt::entity entity, model_component& model, uint32_t frame, uint16_t view);
    /// Whether @p registry_key was deformed in @p frame already, and marks it so.
    auto is_updated_in(const void* registry_key, uint32_t frame) -> bool;
    void release_unused(uint32_t frame);

    struct compute_programs : uniforms_cache
    {
        void cache_uniforms();
        gfx::program::uniform_ptr u_morph_rest;
        gfx::program::uniform_ptr u_morph_accumulate;
        gfx::program::uniform_ptr u_morph_finish;
        std::unique_ptr<gpu_program> rest;
        std::unique_ptr<gpu_program> accumulate;
        std::unique_ptr<gpu_program> finish;
    };

    bool uses_compute_{false};
    compute_programs programs_;
    buffer_clear clear_;
    /// The previous stream's layout: the position as TexCoord7, the basis skipped.
    bgfx::VertexLayout previous_layout_;
    bgfx::VertexLayoutHandle previous_layout_handle_ = BGFX_INVALID_HANDLE;
    /// Each mesh layout's rest layout (position and basis skipped), by the layout's hash.
    std::unordered_map<uint32_t, std::pair<bgfx::VertexLayout, bgfx::VertexLayoutHandle>> rest_layouts_;
    /// The GPU copies of each mesh's deltas, while a model deforms it.
    std::unordered_map<const morph_target_data*, std::unique_ptr<target_buffers>> target_buffers_;
    /// The streams of each morphed model, by its registry and entity.
    std::map<std::pair<const void*, entt::entity>, std::unique_ptr<model_streams>> streams_;
    /// The registries deformed in render frame updated_frame_.
    uint32_t updated_frame_{0};
    std::vector<const void*> updated_registries_;
    /// Scratch for one model's weights.
    std::vector<float> weights_;
};

} // namespace unravel
