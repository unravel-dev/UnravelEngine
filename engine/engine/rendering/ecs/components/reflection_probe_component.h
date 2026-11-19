#pragma once
#include <base/basetypes.hpp>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/reflection_probe.h>

#include <graphics/render_pass.h>
#include <graphics/render_view.h>

#include <array>

namespace unravel
{

/**
 * @class reflection_probe_component
 * @brief Class that contains core reflection probe data, used for rendering and other purposes.
 */
class reflection_probe_component : public component_crtp<reflection_probe_component>
{
public:
    /**
     * @brief Gets the reflection probe object.
     * @return A constant reference to the reflection probe object.
     */
    auto get_probe() const -> const reflection_probe&;

    /**
     * @brief Sets the reflection probe object.
     * @param[in] probe The reflection probe object to set.
     */
    void set_probe(const reflection_probe& probe);

    /**
     * @brief Gets the bounding box of the probe object.
     */
    auto get_bounds() const -> math::bbox;

    /**
     * @brief Computes the projected sphere rectangle.
     * @param[out] rect Reference to the rectangle to be computed.
     * @param[in] position The position of the reflection probe.
     * @param[in] view_origin The origin of the view.
     * @param[in] view The view transform.
     * @param[in] proj The projection transform.
     * @return An integer indicating the result of the computation.
     */
    auto compute_projected_sphere_rect(irect32_t& rect,
                                       const math::vec3& position,
                                       const math::vec3& scale,
                                       const math::vec3& view_origin,
                                       const math::transform& view,
                                       const math::transform& proj) const -> int;

    /**
     * @brief Gets the render view.
     * @param[in] idx The index of the render view to get.
     * @return A reference to the render view.
     */
    auto get_render_view(size_t idx) -> gfx::render_view&;

    /**
     * @brief Gets the cubemap texture.
     * @return A shared pointer to the cubemap texture.
     */
    auto get_cubemap_prefiltered() -> const gfx::texture::ptr&;
    auto get_cubemap() -> const gfx::texture::ptr&;

    /**
     * @brief Gets the cubemap frame buffer object (FBO).
     * @return A shared pointer to the cubemap frame buffer object.
     */
    auto get_cubemap_fbo(size_t face) -> const gfx::frame_buffer::ptr&;

    /**
     * @brief Advances the probe's update bookkeeping. Called once per frame by reflection_probe_system.
     *
     * First settles whether the probe is set aside, from what the views reported in the frame before
     * (note_view_use): a probe some view left out and none used is treated as disabled - its cubemaps are
     * released and no bake waits on it - and one a view uses again bakes whole.
     * @param[in] dt Frame delta time in seconds. Used to drive the realtime update interval.
     */
    void update(float dt);
    void release_resources();

    /**
     * @brief Check if the cubemap was generated this frame.
     * @return A bool indicating whether the faces was already generated
     */
    auto already_generated() const -> bool;

    /**
     * @brief Check if the cubemap's face was generated this frame.
     * @return A bool indicating whether the face was already generated.
     */
    auto already_generated(size_t face) const -> bool;

    /**
     * @brief marks the genrerated face this frame
     */
    void set_generation_frame(size_t face, uint64_t frame);

    /**
     * @brief Requests the probe to rebuild its cubemap.
     *
     * The next reflection-generation pass will begin emitting new faces.
     * When force_full_first_frame is true, the faces bake as fast as the frame's face budget allows
     * (graphics.reflection_probe_faces_per_frame, shared by every probe), as a first bake does, instead
     * of being time-sliced by faces_per_frame. Use this for manual "Bake now" actions where the user
     * wants the result as soon as possible.
     */
    void mark_dirty(bool force_full_first_frame = false);

    /**
     * @brief Returns true when the pending bake is not time-sliced by faces_per_frame: the probe's first
     * bake, or one requested with mark_dirty(true). The frame's face budget still applies.
     */
    auto is_whole_bake() const -> bool;

    /**
     * @brief Returns true if the probe has pending or in-flight cubemap generation work.
     */
    auto is_dirty() const -> bool;

    /**
     * @brief Returns true when a bake has been requested but no cubemap face has been emitted yet.
     */
    auto is_bake_cycle_unstarted() const -> bool;

    /**
     * @brief Returns true when all six cubemap faces have been rendered in the current bake cycle.
     */
    auto is_bake_complete() const -> bool;

    /**
     * @brief Returns true when the product cubemaps hold a finished bake.
     *
     * The capture lives only on the GPU: it is never serialized and is lost whenever the probe's
     * resources are released (inactive entity, resolution change). Until a bake completes the
     * product cubemaps are uninitialized memory, so the pipeline must not sample them.
     */
    auto has_capture() const -> bool;

    /**
     * @brief Records that a bake has written the product cubemaps. Called by the pipeline after the prefilter.
     */
    void mark_capture_ready();

    /**
     * @brief Records whether a view that drew the probe's scene used the probe (@p is_used) or left it out,
     * because the global illumination's reflections own that view (reflection_probe_system::is_used_in_view).
     * update() settles the frame's reports. Not saved.
     */
    void note_view_use(bool is_used);

    /**
     * @brief Returns true while the probe is set aside: in the last frame a view reported on, some view left it
     * out and none used it. A set-aside probe is treated as disabled.
     */
    auto is_set_aside() const -> bool;

    /**
     * @brief Gets the number of faces generated per frame.
     * @return The number of faces generated per frame.
     */
    auto get_faces_per_frame() const -> size_t { return faces_per_frame_; }

    /**
     * @brief Sets the number of faces generated per frame.
     * @param faces The number of faces to generate per frame (1-6). Only used while a bake is in progress.
     */
    void set_faces_per_frame(size_t faces) { faces_per_frame_ = faces; }

    /**
     * @brief Gets whether prefiltering is applied.
     * @return True if prefiltering is applied, false otherwise.
     */
    auto get_apply_prefilter() const -> bool { return apply_prefilter_; }

    /**
     * @brief Sets whether to apply prefiltering.
     * @param apply True to apply prefiltering, false otherwise.
     */
    void set_apply_prefilter(bool apply) { apply_prefilter_ = apply; }

    /**
     * @brief Gets the probe's update policy.
     */
    auto get_update_mode() const -> probe_update_mode { return update_mode_; }

    /**
     * @brief Sets the probe's update policy.
     * Switching INTO on_demand leaves the current bake state as-is; switching OUT of on_demand
     * schedules a rebuild so the probe reflects its new behavior immediately.
     */
    void set_update_mode(probe_update_mode mode);

    /**
     * @brief Gets the realtime refresh interval in seconds. Only consulted for realtime probes.
     */
    auto get_update_interval() const -> float { return update_interval_; }

    /**
     * @brief Sets the realtime refresh interval in seconds.
     * @param seconds 0 means "every available frame" (still time-sliced by faces_per_frame).
     */
    void set_update_interval(float seconds) { update_interval_ = seconds; }

    /**
     * @brief Gets the cubemap face resolution.
     */
    auto get_resolution() const -> probe_resolution { return resolution_; }

    /**
     * @brief Sets the cubemap face resolution. Triggers a rebuild when changed.
     */
    void set_resolution(probe_resolution resolution);

    /**
     * @brief Returns true when the atmospheric sky pass is included during cubemap capture.
     */
    auto get_capture_sky() const -> bool { return capture_sky_; }

    /**
     * @brief Sets whether the atmospheric sky pass runs during cubemap capture.
     * Disable for interior/local probes that should only reflect nearby geometry.
     */
    void set_capture_sky(bool capture);

    /**
     * @brief Returns true when shadow maps are rendered during cubemap capture.
     */
    auto get_capture_shadows() const -> bool { return capture_shadows_; }

    /**
     * @brief Sets whether shadow maps are built and sampled during cubemap capture.
     */
    void set_capture_shadows(bool capture);

private:
    /**
     * @brief The reflection probe object this component represents.
     */
    reflection_probe probe_;

    /**
     * @brief The render views for this component.
     */
    std::array<gfx::render_view, 6> face_rviews_;
    gfx::render_view rview_;

    std::array<uint64_t, 6> generated_frame_{uint64_t(-1),
                                             uint64_t(-1),
                                             uint64_t(-1),
                                             uint64_t(-1),
                                             uint64_t(-1),
                                             uint64_t(-1)};

    bool apply_prefilter_{true};
    /// Number of faces to emit per frame while a bake is in progress (1-6).
    /// Higher = faster bake but more work per frame; lower = smoother amortization.
    size_t faces_per_frame_ = 1;
    /// Number of faces generated in the current cycle.
    size_t generated_faces_count_ = 0;
    /// When true, the faces bake as fast as the frame's face budget allows instead of being time-sliced.
    /// Stays true until the first bake completes, or while mark_dirty(true) is honored for manual "bake now" actions.
    bool first_generation_{true};
    /// Seconds accumulated since the last realtime refresh started.
    float time_since_last_refresh_ = 0.0f;
    /// Has a bake been requested but not yet fully completed.
    /// Default is true: the capture is never serialized, so every new or loaded probe needs one bake
    /// whatever its update mode. The mode only governs the bakes after that.
    bool has_pending_bake_{true};
    /// True once a bake has written the product cubemaps; cleared when they are released or recreated.
    bool has_capture_{false};
    /// What the views reported this frame (note_view_use): some view left the probe out, some view used it.
    bool is_left_out_by_view_{false};
    bool is_used_by_view_{false};
    /// Settled by update() from the reports (is_set_aside).
    bool is_set_aside_{false};

    /// Settles is_set_aside_ from the frame's reports and treats the probe as disabled while it holds.
    void settle_view_use();
    /// Forgets the pending bake and the faces it rendered: nothing waits on the probe.
    void drop_pending_bake();

    /// When the probe should refresh its cubemap. "Once" is the default because it matches the
    /// typical use case (probes covering static environment): bake on load, then stop. Scripts that
    /// want full manual control should switch to "on_demand"; day/night probes should use "realtime".
    probe_update_mode update_mode_{probe_update_mode::once};
    /// Seconds between refreshes when update_mode_ == realtime. 0 means "every available frame"
    /// (still time-sliced by faces_per_frame_).
    float update_interval_{0.0f};
    /// Cubemap face resolution in pixels.
    probe_resolution resolution_{probe_resolution::res_256};
    /// When false, the atmospheric pass is skipped while baking cubemap faces.
    bool capture_sky_{true};
    /// When true, shadow maps are built and used in direct lighting during cubemap capture.
    bool capture_shadows_{true};
};

} // namespace unravel
