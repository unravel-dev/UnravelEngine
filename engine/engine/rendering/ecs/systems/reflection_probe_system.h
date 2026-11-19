#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/ecs/scene.h>
#include <engine/frame_times.h>
#include <engine/rendering/probe_face_budget.h>
#include <hpp/span.hpp>

#include <cstdint>
#include <functional>

namespace unravel
{
class reflection_probe_component;
struct reflection_probe;

/// The order in which probes take a frame's face budget: a probe without a capture shows no reflection until
/// its bake ends, so it goes first; a bake asked for whole (Bake Now, play start) comes next, refreshes last.
enum class probe_bake_rank : std::uint8_t
{
    first_capture,
    whole_bake,
    refresh
};

/// A probe a view may bake: its entity and its component.
struct probe_bake_candidate
{
    entt::entity entity{entt::null};
    reflection_probe_component* probe{};
};

/// What a view does for the faces bake_due_probes gives it.
struct probe_bake_steps
{
    /// Whether the view sees the probe; a probe the view does not see takes no faces.
    std::function<bool(const probe_bake_candidate&)> sees;
    /// Whether the frame has render passes left for one more face.
    std::function<bool()> has_room_for_face;
    /// Before the first face of the probe in this view: a bake cycle that has not started clears its faces.
    std::function<void(const probe_bake_candidate&)> begin_probe;
    /// Renders face @p face (0-5) of the probe's cubemap.
    std::function<void(const probe_bake_candidate&, std::uint32_t face)> render_face;
    /// After the probe's last face in this view; @p is_complete when its bake has every face, to prefilter.
    std::function<void(const probe_bake_candidate&, bool is_complete)> end_probe;
};

class reflection_probe_system
{
public:
    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    void on_frame_update(scene& scn, const frame_times& times);
    void on_play_begin(hpp::span<const entt::handle> entities, delta_t dt);

    /**
     * @brief Flags every reflection probe in the scene for rebuild.
     *
     * Used by editor tooling and scripted bakes. When force_full_first_frame is true, each probe
     * bakes its faces as fast as the frame's face budget allows instead of its own Faces Per Frame
     * at a time - pick true for explicit "Bake now" actions and false for background rebuilds
     * where smooth amortization is preferred.
     */
    static auto mark_all_dirty(scene& scn, bool force_full_first_frame = false) -> size_t;

    /// The face budget every camera of the frame spends (see probe_face_budget).
    static auto get_face_budget() -> probe_face_budget&;

    /// Where @p probe stands in the order probes take a frame's faces.
    static auto get_bake_rank(const reflection_probe_component& probe) -> probe_bake_rank;

    /**
     * @brief Whether a view bakes and draws @p probe.
     *
     * Every probe, except where the global illumination's traced reflections own the view
     * (@p gi_reflections_own_view): they reflect the scene themselves, so a probe of the static scene is
     * left out as if disabled, and only environment probes are baked and drawn, for the rays that miss.
     */
    static auto is_used_in_view(const reflection_probe& probe, bool gi_reflections_own_view) -> bool;

    /**
     * @brief Bakes the faces one view gives the probes of @p candidates on render frame @p frame.
     *
     * The probes the view uses (is_used_in_view) whose bakes need faces, in bake order (probe_bake_rank), each
     * as many faces as its own Faces Per Frame (waived for a whole bake), @p budget and the frame's render
     * passes allow. @p steps renders them.
     */
    static void bake_due_probes(hpp::span<const probe_bake_candidate> candidates,
                                probe_face_budget& budget,
                                bool gi_reflections_own_view,
                                std::uint64_t frame,
                                const probe_bake_steps& steps);

    /**
     * @brief Registry hook fired when an entity gains active_component (i.e. becomes active).
     *
     * Connected permanently in the scene constructor (not play-gated: the editor's active
     * toggle runs the same transform-flags path). Refreshes the probe's capture, because
     * on_frame_update released its product cubemaps while the entity was inactive.
     * Exception: on_demand probes are skipped while play mode is active - their owner
     * scripts schedule rebakes explicitly (MarkDirty).
     */
    static void on_create_active_component(entt::registry& r, entt::entity e);

private:
    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
};
} // namespace unravel
