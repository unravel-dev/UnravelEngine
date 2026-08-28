#pragma once
#include <engine/engine_export.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <math/math.h>
#include <reflection/registration.h>
#include <serialization/serialization.h>

namespace unravel
{
/**
 * @brief Enum representing the projection mode of a camera.
 */
enum class projection_mode : std::uint32_t
{
    perspective = 0,
    orthographic = 1
};

/**
 * @brief Subpixel jitter sequence for temporal AA (see camera::set_aa_data).
 */
enum class taa_jitter_mode : std::uint8_t
{
    /// Kronecker / golden-ratio; smooth incommensurable steps (default).
    progressive_golden = 0,
    /// Halton(base 2,3) in [-0.5,0.5]; strong low-discrepancy, larger frame-to-frame steps.
    halton_2_3 = 1,
    /// R2 / recurrence lattice pair; alternative progressive 2D coverage.
    r2_low_discrepancy = 2,
    /// Two-tap antipodal MSAA-style pattern, period 2. Per-axis range ±0.25 px;
    /// the 2-frame average lands exactly on the pixel center, so the cycle is
    /// well below the history-blend time constant → visually the most stable.
    msaa_2_rotating = 3,
    /// Three-tap equilateral-triangle pattern, period 3, inscribed in a
    /// radius-0.4 px circle (per-axis range [-0.346, +0.4]). Wider footprint
    /// than msaa_2 → slightly softer AA, slightly more visible cycling.
    msaa_3_rotating = 4,
    /// Four-tap rotated grid (D3D 4× MSAA standard, in 1/16-px units), period 4.
    /// Per-axis range ±0.375 px. With history_blend ≈ 0.82 the 4-frame cycle
    /// approaches the temporal filter's time constant, so the pattern can be
    /// perceptible as a slow wobble — lower jitter_amplitude or raise
    /// history_blend if it bothers you.
    msaa_4_rotating = 5,
};

/**
 * @brief Structure for storing camera related context.
 */
struct camera_storage
{
    rtti::context ctx; ///< RTTI context for the camera.
};

/**
 * @brief Class representing a camera. Contains functionality for manipulating and
 * updating a camera. It should not be used as a standalone class - see
 * camera_component and the entity system.
 */
class camera : public crtp_meta_type<camera>
{
public:
    SERIALIZABLE(camera)

    /**
     * @brief Sets the current projection mode for this camera (i.e. orthographic or perspective).
     *
     * @param mode The projection mode to set.
     */
    void set_projection_mode(projection_mode mode);

    /**
     * @brief Sets the field of view angle of this camera (perspective only).
     *
     * @param degrees The field of view in degrees.
     */
    void set_fov(float degrees);

    /**
     * @brief Sets the near plane distance.
     *
     * @param distance The distance to the near clipping plane.
     */
    void set_near_clip(float distance);

    /**
     * @brief Sets the far plane distance.
     *
     * @param distance The distance to the far clipping plane.
     */
    void set_far_clip(float distance);

    /**
     * @brief Sets the half of the vertical size of the viewing volume in world units.
     *
     * @param size The size to set.
     */
    void set_orthographic_size(float size);

    /**
     * @brief Retrieves the current projection mode for this camera.
     *
     * @return The current projection mode.
     */
    auto get_projection_mode() const -> projection_mode;

    /**
     * @brief Retrieves the current field of view angle in degrees.
     *
     * @return The field of view angle.
     */
    auto get_fov() const -> float;

    /**
     * @brief Retrieves the distance from the camera to the near clip plane.
     *
     * @return The near clip distance.
     */
    auto get_near_clip() const -> float;

    /**
     * @brief Retrieves the distance from the camera to the far clip plane.
     *
     * @return The far clip distance.
     */
    auto get_far_clip() const -> float;

    /**
     * @brief Retrieves the orthographic size.
     *
     * @return The orthographic size.
     */
    auto get_ortho_size() const -> float;

    /**
     * @brief Retrieves the zoom factor.
     *
     * @return The zoom factor.
     */
    auto get_zoom_factor() const -> float;

    /**
     * @brief Retrieves the pixels per unit (PPU).
     *
     * @return The PPU value.
     */
    auto get_ppu() const -> float;

    /**
     * @brief Sets the size of the viewport.
     *
     * @param viewportSize The size of the viewport.
     */
    void set_viewport_size(const usize32_t& viewport_size);

    /**
     * @brief Sets the position of the viewport.
     *
     * @param viewportPos The position of the viewport.
     */
    void set_viewport_pos(const ipoint32_t& viewport_pos);

    /**
     * @brief Retrieves the size of the viewport.
     *
     * @return The size of the viewport.
     */
    auto get_viewport_size() const -> const usize32_t&;

    /**
     * @brief Retrieves the position of the viewport.
     *
     * @return The position of the viewport.
     */
    auto get_viewport_pos() const -> const ipoint32_t&;

    /**
     * @brief Sets the aspect ratio to be used for generating the horizontal FOV angle (perspective only).
     *
     * @param aspect The aspect ratio to set.
     * @param locked Whether the aspect ratio should be locked.
     */
    void set_aspect_ratio(float aspect, bool locked = false);

    /**
     * @brief Retrieves the aspect ratio used to generate the horizontal FOV angle.
     *
     * @return The aspect ratio.
     */
    auto get_aspect_ratio() const -> float;

    /**
     * @brief Determines if the aspect ratio is currently being updated by the render driver.
     *
     * @return true if the aspect ratio is locked, false otherwise.
     */
    auto is_aspect_locked() const -> bool;

    /**
     * @brief Checks if the frustum is currently locked.
     *
     * @return true if the frustum is locked, false otherwise.
     */
    auto is_frustum_locked() const -> bool;

    /**
     * @brief Locks or unlocks the frustum.
     *
     * @param locked Whether the frustum should be locked.
     */
    void lock_frustum(bool locked);

    /**
     * @brief Retrieves the current camera object frustum.
     *
     * @return The current frustum.
     */
    auto get_frustum() const -> const math::frustum&;

    /**
     * @brief Retrieves the frustum representing the space between the camera position and its near plane.
     *
     * @return The clipping volume frustum.
     */
    auto get_clipping_volume() const -> const math::frustum&;

    /**
     * @brief Retrieves the current projection matrix.
     *
     * @return The current projection matrix.
     */
    auto get_projection() const -> const math::transform&;

    /**
     * @brief The current projection with the TAA subpixel jitter subtracted back out.
     *
     * get_projection() carries the jitter set_aa_data() injected, which is correct for
     * rasterization but poison for any pass that reconstructs world positions from the
     * inverse view-projection: the sub-pixel wobble times the ray's lever arm becomes a
     * world-space sweep marching to the jitter sequence (measured as mirror shimmer in the
     * GI reflection chain). Such passes take this matrix instead; with TAA off the two are
     * identical.
     */
    auto get_projection_unjittered() const -> math::transform;

    /**
     * @brief Retrieves the current view matrix.
     *
     * @return The current view matrix.
     */
    auto get_view() const -> const math::transform&;
    auto get_view_inverse() const -> const math::transform&;
    auto get_view_relative() const -> const math::transform&;
    auto get_view_inverse_relative() const -> const math::transform&;

    /**
     * @brief The matrices the PREVIOUS frame rendered with - the complete set, mirroring
     * the current-side convention (unsuffixed = jittered, _unjittered explicit):
     *
     *   current                          | previous
     *   get_view                         | get_prev_view
     *   get_projection        (jittered) | get_prev_projection        (jittered)
     *   get_projection_unjittered        | get_prev_projection_unjittered
     *   get_view_projection   (jittered) | get_prev_view_projection   (jittered)
     *   get_view_projection_unjittered   | get_prev_view_projection_unjittered
     *
     * Recorded by record_current_matrices(), which the pipeline calls once per rendered
     * frame AFTER applying the frame's jitter (frame-stamped: a camera rendered more
     * than once per frame cannot collapse the previous pair onto the current one).
     *
     * Temporal reprojection consumers take get_prev_view_projection_unjittered: the
     * history is the resolved, pixel-center-aligned image, so unprojecting the current
     * pixel with the jittered current matrices and reprojecting with the UNJITTERED
     * previous pair yields the correct history UV under camera motion (standard TAA
     * formulation). The jittered variants exist for completeness and rasterization-
     * aligned uses.
     *
     * All getters are TOTAL: on a camera never recorded they report the CURRENT
     * matrices ("previous == current", zero motion) instead of default-constructed
     * identity, so a consumer at worst treats the frame as fresh.
     */
    auto get_prev_view() const -> const math::transform&;
    auto get_prev_projection() const -> const math::transform&;
    auto get_prev_projection_unjittered() const -> math::transform;
    auto get_prev_view_projection() const -> math::transform;
    auto get_prev_view_projection_unjittered() const -> math::transform;

    /**
     * @brief Camera-RELATIVE previous view-projection (previous rotation-only view x the
     * UNJITTERED previous projection), for camera-relative passes (clouds). Derived from
     * the absolute pair by dropping the view translation - valid because the view is
     * rigid (lookAt), so its rotation-only form IS the relative view. Total.
     */
    auto get_prev_view_projection_relative_unjittered() const -> math::transform;

    /**
     * @brief Records the matrices THIS frame renders with; last frame's recording becomes
     * the get_prev_* set. Called by the pipeline once per rendered frame, immediately
     * after the frame's jitter is applied (so both the jittered and unjittered current
     * pairs are final) - NOT from before-render code, where the frame's jitter does not
     * exist yet and pipeline-only cameras (probe faces, tools) would be missed.
     * Frame-stamped internally: repeated calls within one render frame are no-ops.
     */
    void record_current_matrices();

    /**
     * @brief Retrieves the current view-projection matrix.
     *
     * @return The current view-projection matrix.
     */
    auto get_view_projection() const -> math::transform;
    auto get_view_projection_unjittered() const -> math::transform;
    auto get_view_projection_relative() const -> math::transform;

    /**
     * @brief Sets the current jitter value for temporal anti-aliasing.
     *
     * @param viewport_size Viewport size (pixels) for scaling jitter into clip space.
     * @param temporal_frame_index Monotonic frame counter (e.g. render frame). Drives progressive
     *        subpixel jitter (no short-period MSAA-style cycling); avoid large discontinuities.
     * @param temporal_aa_samples Values > 1 enable jitter; count is stored for UI / future tuning.
     * @param jitter_mode Which subpixel sequence to use (see @c taa_jitter_mode).
     * @param jitter_amplitude Scales raw subpixel offsets before clip scaling; 1 = full ~±½ pixel.
     *        Lower values (e.g. 0.5–0.7) reduce visible whole-frame shake at some AA cost.
     * @param jitter_temporal_phase_scale Multiplies progression speed for golden / Halton / R2 (1 = legacy).
     *        MSAA rotating modes ignore this and advance one subsample per frame.
     */
    void set_aa_data(const usize32_t& viewport_size,
                     std::uint32_t temporal_frame_index,
                     std::uint32_t temporal_aa_samples,
                     taa_jitter_mode jitter_mode = taa_jitter_mode::progressive_golden,
                     float jitter_amplitude = 1.0f,
                     float jitter_temporal_phase_scale = 1.0f);

    /**
     * @brief Retrieves the anti-aliasing data.
     *
     * @return The anti-aliasing data.
     */
    auto get_aa_data() const -> const math::vec4&;

    /**
     * @brief Determines if the specified AABB falls within the frustum.
     *
     * @param bounds The AABB to test.
     * @return The result of the volume query.
     */
    auto classify_aabb(const math::bbox& bounds) const -> math::volume_query;

    /**
     * @brief Tests if the specified AABB is within the frustum.
     *
     * @param bounds The AABB to test.
     * @return true if the AABB is within the frustum, false otherwise.
     */
    auto test_aabb(const math::bbox& bounds) const -> bool;

    /**
     * @brief Determines if the specified OBB is within the frustum.
     *
     * @param bounds The OBB to test.
     * @param t The transformation to apply to the OBB.
     * @return The result of the volume query.
     */
    auto classify_obb(const math::bbox& bounds, const math::transform& t) const -> math::volume_query;

    /**
     * @brief Tests if the specified OBB is within the frustum.
     *
     * @param bounds The OBB to test.
     * @param t The transformation to apply to the OBB.
     * @return true if the OBB is within the frustum, false otherwise.
     */
    auto test_obb(const math::bbox& bounds, const math::transform& t) const -> bool;

    auto test_billboard(float size, const math::transform& t) const -> bool;


    /**
     * @brief Converts the specified screen position into a ray origin and direction vector.
     *
     * @param point The screen position.
     * @param rayOriginOut The output ray origin.
     * @param rayDirectionOut The output ray direction.
     * @return true if the conversion is successful, false otherwise.
     */
    auto viewport_to_ray(const math::vec2& point, math::vec3& vec_ray_start, math::vec3& vec_ray_dir) const -> bool;

    /**
     * @brief Converts a screen position into a world space position on the specified plane.
     *
     * @param point The screen position.
     * @param plane The plane to intersect.
     * @param position_out The output world space position.
     * @param clip Whether to clip the result.
     * @return true if the conversion is successful, false otherwise.
     */
    auto viewport_to_world(const math::vec2& point, const math::plane& plane, math::vec3& position_out, bool clip) const
        -> bool;

    /**
     * @brief Raycasts from viewport point onto a quad plane and returns pixel coordinates.
     *
     * Projects the viewport point through the camera onto the quad's plane, then converts
     * the world intersection to local quad space and finally to document pixel coordinates.
     * Assumes the quad is a unit quad (-0.5 to 0.5) scaled by the transform.
     *
     * @param viewport_point The viewport/screen position.
     * @param quad_transform The quad's model transform (world * scale for world size).
     * @param quad_width The quad width in pixels.
     * @param quad_height The quad height in pixels.
     * @param pixel_out The output pixel coordinates (0..width, 0..height), or (-1,-1) if no hit.
     * @return true if the ray hit the quad plane, false otherwise.
     */
    auto project_to_quad(const math::vec2& viewport_point,
                     const math::transform& quad_transform,
                     uint32_t quad_width,
                     uint32_t quad_height,
                     math::vec2& pixel_out) const -> bool;

    /**
     * @brief Converts a screen position into a world space intersection point on a major axis plane.
     *
     * @param point The screen position.
     * @param axis_origin The origin of the axis.
     * @param position_out The output world space position.
     * @param major_axis_out The output major axis.
     * @return true if the conversion is successful, false otherwise.
     */
    auto viewport_to_major_axis(const math::vec2& point,
                                const math::vec3& axis_origin,
                                math::vec3& position_out,
                                math::vec3& major_axis_out) const -> bool;

    /**
     * @brief Converts a screen position into a world space intersection point on a major axis plane.
     *
     * @param point The screen position.
     * @param axis_origin The origin of the axis.
     * @param align_normal The alignment normal.
     * @param position_out The output world space position.
     * @param major_axis_out The output major axis.
     * @return true if the conversion is successful, false otherwise.
     */
    auto viewport_to_major_axis(const math::vec2& point,
                                const math::vec3& axis_origin,
                                const math::vec3& align_normal,
                                math::vec3& position_out,
                                math::vec3& major_axis_out) const -> bool;

    /**
     * @brief Converts a screen position into a camera space position at the near plane.
     *
     * @param point The screen position.
     * @param position_out The output camera space position.
     * @return true if the conversion is successful, false otherwise.
     */
    auto viewport_to_camera(const math::vec3& point, math::vec3& position_out) const -> bool;

    /**
     * @brief Transforms a point from world space into screen space.
     *
     * @param pos The world space position.
     * @return The screen space position.
     */
    auto world_to_viewport(const math::vec3& pos) const -> math::vec3;

    /**
     * @brief Estimates the zoom factor based on the specified plane.
     *
     * @param plane The reference plane.
     * @return The estimated zoom factor.
     */
    auto estimate_zoom_factor(const math::plane& plane) const -> float;

    /**
     * @brief Estimates the zoom factor based on the specified position.
     *
     * @param position The reference position.
     * @return The estimated zoom factor.
     */
    auto estimate_zoom_factor(const math::vec3& position) const -> float;

    /**
     * @brief Estimates the zoom factor based on the specified plane, constrained by a maximum value.
     *
     * @param plane The reference plane.
     * @param maximum_value The maximum zoom factor value.
     * @return The estimated zoom factor.
     */
    auto estimate_zoom_factor(const math::plane& plane, float maximum_value) const -> float;

    /**
     * @brief Estimates the zoom factor based on the specified position, constrained by a maximum value.
     *
     * @param position The reference position.
     * @param maximum_value The maximum zoom factor value.
     * @return The estimated zoom factor.
     */
    auto estimate_zoom_factor(const math::vec3& position, float maximum_value) const -> float;

    /**
     * @brief Estimates the pick tolerance based on the pixel tolerance and reference position.
     *
     * @param pixel_tolerance The pixel tolerance.
     * @param reference_position The reference position.
     * @param object_transform The transformation to apply to the object.
     * @return The estimated pick tolerance.
     */
    auto estimate_pick_tolerance(float pixel_tolerance,
                                 const math::vec3& reference_position,
                                 const math::transform& object_transform) const -> math::vec3;

    /**
     * @brief Sets the camera to look at a specified target.
     *
     * @param eye The eye position.
     * @param at The target position.
     */
    void look_at(const math::vec3& eye, const math::vec3& at);

    /**
     * @brief Sets the camera to look at a specified target with an up vector.
     *
     * @param eye The eye position.
     * @param at The target position.
     * @param vUp The up vector.
     */
    void look_at(const math::vec3& eye, const math::vec3& at, const math::vec3& up);

    /**
     * @brief Retrieves the current position of the camera.
     *
     * @return The current camera position.
     */
    auto get_position() const -> const math::vec3&;

    /**
     * @brief Retrieves the x-axis unit vector of the camera's local coordinate system.
     *
     * @return The x-axis unit vector.
     */
    auto x_unit_axis() const -> math::vec3;

    /**
     * @brief Retrieves the y-axis unit vector of the camera's local coordinate system.
     *
     * @return The y-axis unit vector.
     */
    auto y_unit_axis() const -> math::vec3;

    /**
     * @brief Retrieves the z-axis unit vector of the camera's local coordinate system.
     *
     * @return The z-axis unit vector.
     */
    auto z_unit_axis() const -> math::vec3;

    /**
     * @brief Retrieves the bounding box of this object.
     *
     * @return The local bounding box.
     */
    auto get_local_bounding_box() -> math::bbox;

    /**
     * @brief Marks the camera as modified.
     */
    void touch();

    /**
     * @brief Retrieves a camera for one of six cube faces.
     *
     * @param face The index of the cube face.
     * @param transform The transformation to apply.
     * @return The corresponding face camera.
     */
    static auto get_face_camera(std::uint32_t face, const math::transform& transform) -> camera;

protected:
    /// Anti-aliasing data.
    math::vec4 aa_data_ = {0.0f, 0.0f, 0.0f, 0.0f};
    /// Cached view matrix
    math::transform view_;
    math::transform view_inverse_;

    math::transform view_relative_;
    math::transform view_inverse_relative_;

    /// Cached projection matrix.
    mutable math::transform projection_;

    /// Previous-frame matrix state (see record_current_matrices): the full set the
    /// previous frame rendered with (view + jittered and unjittered projections), plus
    /// the recording of this frame's set that becomes "previous" on the next call. The
    /// promotion is guarded by a render-frame stamp so a camera rendered more than once
    /// per frame (preview insets, multi-view) cannot collapse the previous set onto the
    /// current one.
    math::transform prev_view_;
    math::transform prev_projection_;
    math::transform prev_projection_unjittered_;
    math::transform frame_view_;
    math::transform frame_projection_;
    math::transform frame_projection_unjittered_;
    bool prev_matrices_valid_ = false;
    std::uint32_t prev_record_frame_ = 0xFFFFFFFFu;
    /// Details regarding the camera frustum.
    mutable math::frustum frustum_;
    /// The near clipping volume (area of space between the camera position and the near plane).
    mutable math::frustum clipping_volume_;
    /// The type of projection currently selected for this camera.
    projection_mode projection_mode_ = projection_mode::perspective;
    /// Vertical degrees angle (perspective only).
    float fov_ = 60.0f;
    /// Near clip plane Distance
    float near_clip_ = 0.1f;
    /// Far clip plane Distance
    float far_clip_ = 1000.0f;
    /// camera's half-size when in orthographic mode.
    float ortho_size_ = 5;
    /// The aspect ratio used to generate the correct horizontal degrees (perspective only)
    float aspect_ratio_ = 1.0f;
    /// Viewport position
    ipoint32_t viewport_pos_ = {0, 0};
    /// Viewport size
    usize32_t viewport_size_ = {0, 0};
    /// View matrix dirty ?
    bool view_dirty_ = true;
    /// Projection matrix dirty ?
    mutable bool projection_dirty_ = true;
    /// Has the aspect ratio changed?
    mutable bool aspect_dirty_ = true;
    /// Are the frustum planes dirty ?
    mutable bool frustum_dirty_ = true;
    /// Should the aspect ratio be automatically updated by the render driver?
    bool aspect_locked_ = false;
    /// Is the frustum locked?
    bool frustum_locked_ = false;
};
} // namespace unravel
