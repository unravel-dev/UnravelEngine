#pragma once

#include <engine/rendering/gi/gi_pass_constants.h>
#include <engine/rendering/gpu_program.h>
#include <engine/rendering/pipeline/passes/gi_run_params.h>

#include <graphics/texture.h>

#include <array>
#include <vector>

namespace unravel
{

/**
 * @brief The GI radiance cache: sparse, world-locked radiance probes on 4 camera-centred clipmaps of 48^3
 *        cells (1.04 m doubling), marked by the screen probes that will read them, new probes traced the frame
 *        they appear, the rest re-traced oldest first within a budget that grows with the final gather update
 *        speed, filtered over their face neighbours and stored with an octahedral border for bilinear reads.
 *        Lit surfaces outside the G-buffer (the translucency pass) mark the probes around them too, and read each
 *        probe's L1 SH, projected when it is traced.
 *
 * Owned by gi_gather_pass, which updates it after the probe placement and binds it to the probe trace
 * for the hand-off. The probes' rays read the surface cache at global distance field hits.
 */
class gi_radiance_cache
{
public:
    /// The screen-probe layout the marking pass reads (gi_common.sh uniforms).
    struct frame_inputs
    {
        const gi_run_params* params{};
        gfx::texture::ptr probe_records;
        uint32_t probes_x{};
        /// Rows of the probe atlas: the uniform probes and the adaptive ones below them all mark.
        uint32_t probe_rows{};
        const float* frame{};
        const float* probes{};
        const float* view{};
        /// The probes' radiance texels per axis (gi_pass::get_radiance_cache_probe_resolution, a multiple of 16).
        uint32_t probe_resolution = uint32_t(gi::GI_RADIANCE_CACHE_PROBE_RES);
        /// World points (xyz) of the lit surfaces outside the G-buffer that read the cache this frame
        /// (bind_for_surfaces): each marks the probes around it. Null or empty marks none.
        const std::vector<math::vec4>* surface_points{};
    };

    /// The most surface points a frame marks (frame_inputs::surface_points): about a quarter of the probe pool at most,
    /// the rest staying the screen probes'.
    static constexpr uint32_t max_surface_points = 2048;

    /// The cell size of @p clipmap (0 the finest): its probes' spacing in world units.
    static auto get_cell_size(uint32_t clipmap) -> float;
    /// How far from the camera, along each axis, a lookup may still read @p clipmap (GiRcClipmapOf): the most it covers
    /// wherever its snapping puts it.
    static auto get_reach(uint32_t clipmap) -> float;
    /// How far from the camera, along each axis, a lookup reads @p clipmap or a finer one wherever the snapping puts
    /// them: the least it covers.
    static auto get_inner_reach(uint32_t clipmap) -> float;
    /// Appends to @p points the surface points (frame_inputs::surface_points) of a model with @p bounds seen from
    /// @p eye: per clipmap, a grid of its share of the box @p spacing cells apart (1 marks every probe between points,
    /// 2 nearly every one), near the box's faces only where it is several cells wide on every axis. False, with
    /// @p points partly filled, when they would pass @p budget.
    static auto make_surface_points(const math::bbox& bounds,
                                    const math::vec3& eye,
                                    size_t budget,
                                    float spacing,
                                    std::vector<math::vec4>& points) -> bool;
    /// Appends to @p points the surface points of what fills @p bounds, as particles fill their emitter's box, with
    /// the camera at @p eye: one inside every lattice cube of the cache that the box reaches, in the clipmap a lookup
    /// there reads, so every probe such a lookup interpolates is marked. The points are the lattice's own: boxes that
    /// overlap get the same points where they do. A point's w is its clipmap. False, with @p points partly filled,
    /// when they would pass @p budget.
    static auto make_volume_points(const math::bbox& bounds, const math::vec3& eye, size_t budget, std::vector<math::vec4>& points)
        -> bool;

    /**
     * @brief The probes the volume points of a frame's boxes mark together (make_volume_points). A point marks the
     * eight probes at the corners of its lattice cube, and neighbouring cubes and overlapping boxes share them: the
     * cache's pool is spent in probes, so they are what a budget counts.
     */
    class volume_marks
    {
    public:
        /// Forgets every probe: a new frame.
        void clear();
        /// Appends to @p points the ones of @p candidates (make_volume_points' points of one box) that mark a probe
        /// no earlier box marked, and takes their probes. False, changing nothing, when that is more than
        /// @p max_points points or brings the probes past @p max_probes.
        auto add(const std::vector<math::vec4>& candidates, size_t max_points, size_t max_probes, std::vector<math::vec4>& points) -> bool;
        /// How many probes the boxes added so far mark.
        auto get_probe_count() const -> size_t
        {
            return marked_.size();
        }

    private:
        /// A probe: its clipmap, and its place along each axis in multiples of that clipmap's cell.
        using probe = std::array<int64_t, 4>;

        /// Whether @p point marks a probe marked_ lacks; appends those to fresh_.
        auto collect_new_probes(const math::vec4& point) -> bool;

        /// The probes of the boxes added so far, sorted.
        std::vector<probe> marked_;
        /// add's scratch: the new probes of the box at hand, and the merge of both lists.
        std::vector<probe> fresh_;
        std::vector<probe> merged_;
    };

    /// Whether the probes' SH is kept for surfaces (bind_for_surfaces): from the first frame that marked surface
    /// points.
    auto has_surface_sh() const -> bool
    {
        return has_surfaces_;
    }

    ~gi_radiance_cache();

    auto init(rtti::context& ctx) -> bool;

    /// Updates the cache for this frame. False when it could not run; the gather then traces without it.
    auto update(const frame_inputs& inputs) -> bool;

    /// Binds the cache for the hand-off read (gi_radiance_cache_sample.sh) at the given stages and sets
    /// the clipmap and layout uniforms.
    void bind_for_sampling(uint8_t indirection_stage, uint8_t final_stage) const;

    /// Binds what is lit outside the G-buffer reads of the cache (gi_radiance_cache_surface.sh: mg_forward.sh
    /// FORWARD_GI, particle_lighting.sh PARTICLE_GI): the indirection and the probes' L1 SH at the given stages, and
    /// the clipmap uniforms.
    void bind_for_surfaces(uint8_t indirection_stage, uint8_t sh_stage) const;

    /// Frees the probe atlases and buffers; the next update allocates them again and rebuilds the cache.
    void release_resources();

    /// Binds this frame's update counters (gi_radiance_cache_common.sh GI_RC_COUNTER_*) for reading at @p stage.
    void bind_counters(uint8_t stage) const;
    /// This frame's trace cost budget (u_gi_rc_params.y; a rebuild's is unlimited).
    auto get_trace_cost_budget() const -> float;

private:
    struct uniforms : uniforms_cache
    {
        gfx::program::uniform_ptr u_gi_rc_clipmaps;
        gfx::program::uniform_ptr u_gi_rc_prev_clipmaps;
        gfx::program::uniform_ptr u_gi_rc_params;
        gfx::program::uniform_ptr u_gi_rc_camera;
        gfx::program::uniform_ptr u_gi_rc_layout;
        gfx::program::uniform_ptr u_gi_frame;
        gfx::program::uniform_ptr u_gi_probes;
        gfx::program::uniform_ptr u_gi_view;
        gfx::program::uniform_ptr u_gi_settings;
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr u_gi_hit_lighting;
        gfx::program::uniform_ptr s_gi_probe_records;
        gfx::program::uniform_ptr s_gi_env_sh;
        gfx::program::uniform_ptr s_gi_rc_radiance;
        gfx::program::uniform_ptr s_gi_rc_final;
        gfx::program::uniform_ptr s_sdf_clipmap;
        gfx::program::uniform_ptr s_sdf_clipmap_coverage;
        gfx::program::uniform_ptr s_sdf_clipmap_mip;
        gfx::program::uniform_ptr u_gi_rc_points;
        gfx::program::uniform_ptr u_gi_rc_sh_params;

        void cache_uniforms();
    } uniforms_;

    /// The bookkeeping pass's modes (cs_gi_rc_bookkeeping.eshader).
    enum class bookkeeping : int
    {
        reset = 0,
        frame_start = 1,
        budget = 2,
        trace_list = 3,
        trace_args = 4,
    };

    /// Whether the programs of the cache itself loaded: without them it is off, and the gather traces without it.
    auto has_programs() const -> bool;
    /// Whether the programs that serve see-through surfaces loaded (the marking around their points and the SH):
    /// without them the cache still serves the gather, and those surfaces take their diffuse light from the sky.
    auto has_surface_programs() const -> bool;
    /// Creates the buffers once and the atlases at @p probe_res radiance texels per probe axis; new atlases rebuild
    /// the cache.
    auto ensure_resources(uint32_t probe_res) -> bool;
    /// Snaps the clipmaps around the camera (double precision) and keeps last frame's for the carry-over.
    void place_clipmaps(const math::vec3& camera);
    /// Sets the cache uniforms every pass reads, with @p mode for the bookkeeping pass.
    void set_cache_uniforms(bookkeeping mode) const;
    /// Sets u_gi_rc_layout (gi_radiance_cache_common.sh): x = the probe resolution.
    void set_layout_uniform() const;
    void run_bookkeeping(bookkeeping mode) const;
    /// One dispatch over every indirection entry.
    void run_over_indirection(gpu_program& program, const char* name) const;
    void run_mark(const frame_inputs& inputs) const;
    /// Marks the probes around frame_inputs::surface_points (cs_gi_rc_mark_points.eshader).
    void run_mark_surfaces(const frame_inputs& inputs);
    void run_trace(const frame_inputs& inputs) const;
    void run_filter() const;
    /// Projects the probes traced this frame onto L1 SH (cs_gi_rc_sh.eshader), or every probe of the pool when
    /// @p is_full.
    void run_sh(bool is_full) const;
    /// Whether this frame has surface points to mark.
    static auto has_surface_points(const frame_inputs& inputs) -> bool;

    gpu_program::ptr clear_program_;
    gpu_program::ptr mark_program_;
    gpu_program::ptr update_program_;
    gpu_program::ptr bookkeeping_program_;
    gpu_program::ptr allocate_program_;
    gpu_program::ptr select_program_;
    gpu_program::ptr tiles_program_;
    gpu_program::ptr trace_program_;
    gpu_program::ptr filter_program_;
    gpu_program::ptr mark_points_program_;
    gpu_program::ptr sh_program_;

    /// Ping-pong indirection: this frame's and last frame's.
    std::array<bgfx::DynamicIndexBufferHandle, 2> indirection_{bgfx::DynamicIndexBufferHandle{bgfx::kInvalidHandle},
                                                               bgfx::DynamicIndexBufferHandle{bgfx::kInvalidHandle}};
    bgfx::DynamicIndexBufferHandle probe_state_{bgfx::kInvalidHandle};
    bgfx::DynamicIndexBufferHandle counters_{bgfx::kInvalidHandle};
    bgfx::DynamicIndexBufferHandle traces_{bgfx::kInvalidHandle};
    bgfx::DynamicIndexBufferHandle tiles_{bgfx::kInvalidHandle};
    bgfx::IndirectBufferHandle args_{bgfx::kInvalidHandle};
    gfx::texture::ptr radiance_atlas_;
    gfx::texture::ptr final_atlas_;
    /// Per probe, four vec4: its radiance map's L1 SH, red, green and blue, and the cell it was projected at
    /// (cs_gi_rc_sh.eshader).
    bgfx::DynamicVertexBufferHandle sh_{bgfx::kInvalidHandle};
    /// This frame's surface points (frame_inputs::surface_points), uploaded for the marking.
    bgfx::DynamicVertexBufferHandle surface_points_{bgfx::kInvalidHandle};

    /// Per clipmap: xyz = corner of cell (0, 0, 0), w = cell size; this frame's and last frame's.
    std::array<float, 16> clipmaps_{};
    std::array<float, 16> prev_clipmaps_{};
    /// xyz = this frame's camera position (the trace cost and the angular level depend on the distance).
    std::array<float, 4> camera_{};
    /// The index of this frame's indirection in indirection_.
    uint32_t current_ = 0;
    /// Cache frames since the start (0 = never updated); the probes' last-used / last-traced stamps.
    uint32_t frame_ = 0;
    /// The probes re-traced per frame beyond the new ones (gi_pass::get_radiance_cache_trace_budget).
    uint32_t trace_budget_ = 0;
    /// The atlases' radiance texels per probe axis (gi_pass::get_radiance_cache_probe_resolution).
    uint32_t probe_res_ = 0;
    /// False until a frame has run with these resources: the next frame then starts from scratch.
    bool persistent_ = false;
    /// Surfaces have read the SH since the pool was built: it is current and kept so every frame.
    bool has_surfaces_ = false;
};

} // namespace unravel
