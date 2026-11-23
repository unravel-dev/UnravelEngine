/*
 * Validation suite for rebuilding world positions from depth far from the world origin.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "camera relative"
 *
 * A fused absolute inverse view-projection carries the camera position in every element, so a position rebuilt
 * through it loses more the farther the camera is from the origin. The shaders rebuild through the inverse projection
 * and the inverse view taken apart (clipToWorldThroughView, shaderlib.sh), or through the camera-relative inverse plus
 * the camera position (the lights, decals, post materials and the GI's previous frame, gi_motion.sh). These run that
 * math in float, as the GPU does, on the matrices the passes upload, and check that it keeps the precision of the
 * coordinates themselves; the same for the view direction the reflection probe passes take. They also check that the
 * camera reports the eye it was given and last frame's eye.
 */

#include "../tests.h"

#include <engine/rendering/camera.h>
#include <graphics/graphics.h>
#include <math/math.h>

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

using namespace unravel::test;
using namespace unravel;

namespace
{

/// The spacing of floats around @p value.
auto float_step(double value) -> double
{
    const float f = float(std::abs(value));
    return double(std::nextafter(f, std::numeric_limits<float>::infinity()) - f);
}

/// A camera at @p eye looking along @p forward, with the editor's Scene camera clip range (1 cm to 1 km).
auto make_camera(const math::vec3& eye, const math::vec3& forward) -> camera
{
    camera cam;
    cam.set_fov(60.0f);
    cam.set_aspect_ratio(16.0f / 9.0f, true);
    cam.set_near_clip(0.01f);
    cam.set_far_clip(1000.0f);
    cam.look_at(eye, eye + forward);
    return cam;
}

/// The view @p view of a camera at @p eye with its translation made in double: the reference the float math is
/// measured against (the rotation is the camera's own).
auto make_exact_view(const math::mat4& view, const glm::dvec3& eye) -> glm::dmat4
{
    glm::dmat4 exact(view);
    exact[3] = glm::dvec4(-(glm::dmat3(exact) * eye), 1.0);
    return exact;
}

/// The clip position a depth buffer holds for @p point under @p view_proj: x, y and depth rounded to float.
auto to_stored_clip(const glm::dmat4& view_proj, const glm::dvec3& point) -> math::vec3
{
    const glm::dvec4 clip = view_proj * glm::dvec4(point, 1.0);
    return math::vec3(float(clip.x / clip.w), float(clip.y / clip.w), float(clip.z / clip.w));
}

/// clipToWorld(_invViewProj, _clipPos), shaderlib.sh.
auto clip_to_world(const math::mat4& inv_view_proj, const math::vec3& clip) -> math::vec3
{
    const math::vec4 world = inv_view_proj * math::vec4(clip, 1.0f);
    return math::vec3(world) / world.w;
}

/// clipToWorldThroughView(_clipPos), shaderlib.sh, with the inverses bgfx computes in float from the pass's view.
auto clip_to_world_through_view(const math::mat4& inv_proj, const math::mat4& inv_view, const math::vec3& clip)
    -> math::vec3
{
    const math::vec4 view = inv_proj * math::vec4(clip, 1.0f);
    return math::vec3(inv_view * math::vec4(math::vec3(view) / view.w, 1.0f));
}

struct reconstruction_errors
{
    /// The largest error of each form over the points, in metres.
    double fused = 0.0;
    double through_view = 0.0;
    double relative = 0.0;
    /// The largest share of the bound each kept form used.
    double through_view_share = 0.0;
    double relative_share = 0.0;
};

/// Rebuilds points 0.3 m to 500 m in front of a camera at @p eye from their stored clip positions, through the fused
/// absolute inverse, through the view and through the camera-relative inverse.
auto measure_reconstruction(const math::vec3& eye) -> reconstruction_errors
{
    const math::vec3 forward = math::normalize(math::vec3(0.6f, -0.15f, 0.8f));
    const camera cam = make_camera(eye, forward);
    const math::mat4 proj = cam.get_projection().get_matrix();
    const math::mat4 view = cam.get_view().get_matrix();
    // What bgfx derives from the uploaded pair: the fused product and the inverses, all in float.
    const math::mat4 inv_view_proj = glm::inverse(proj * view);
    const math::mat4 inv_proj = glm::inverse(proj);
    const math::mat4 inv_view = glm::inverse(view);
    const math::mat4 inv_view_proj_relative = cam.get_inverse_view_projection_relative();

    const glm::dvec3 exact_eye(eye);
    const glm::dmat4 exact_view = make_exact_view(view, exact_eye);
    const glm::dmat4 exact_view_proj = glm::dmat4(proj) * exact_view;
    // The camera's own axes (its rotation rows), so every point lies inside its frustum.
    const glm::dmat3 rotation = glm::transpose(glm::dmat3(exact_view));
    const glm::dvec3 right = rotation[0];
    const glm::dvec3 up = rotation[1];
    const glm::dvec3 ahead = rotation[2];

    reconstruction_errors errors;
    const double distances[] = {0.3, 1.0, 4.0, 20.0, 80.0, 250.0, 500.0};
    const double offsets[] = {-0.4, 0.0, 0.35};
    for(double distance : distances)
    {
        for(double x : offsets)
        {
            for(double y : offsets)
            {
                const glm::dvec3 point = exact_eye + distance * (ahead + x * right + 0.5 * y * up);
                const math::vec3 clip = to_stored_clip(exact_view_proj, point);
                const double fused = glm::length(glm::dvec3(clip_to_world(inv_view_proj, clip)) - point);
                const double through_view =
                    glm::length(glm::dvec3(clip_to_world_through_view(inv_proj, inv_view, clip)) - point);
                const double relative =
                    glm::length(glm::dvec3(eye + clip_to_world(inv_view_proj_relative, clip)) - point);
                // The coordinates' own spacing, a few steps for the float view and its inverse, plus the depth
                // buffer's: one float step of reversed depth is a relative step of the distance.
                const double coordinate_step = float_step(glm::length(point));
                const double bound = 4.0 * coordinate_step + 4.0e-6 * distance * glm::length(glm::dvec3(1.0, x, y));
                errors.fused = std::max(errors.fused, fused);
                errors.through_view = std::max(errors.through_view, through_view);
                errors.relative = std::max(errors.relative, relative);
                errors.through_view_share = std::max(errors.through_view_share, through_view / bound);
                errors.relative_share = std::max(errors.relative_share, relative / bound);
            }
        }
    }
    return errors;
}

void test_reconstruction_far_from_origin()
{
    const math::vec3 eyes[] = {{500.25f, 3.5f, -500.75f}, {10000.25f, 3.5f, -10000.75f}};
    for(const auto& eye : eyes)
    {
        const reconstruction_errors errors = measure_reconstruction(eye);
        char label[96];
        std::snprintf(label, sizeof(label), "camera %.0f m from the origin", double(math::length(eye)));
        std::printf("  %s: fused %.3g m, through the view %.3g m, relative %.3g m\n",
                    label,
                    errors.fused,
                    errors.through_view,
                    errors.relative);
        check(errors.through_view_share <= 1.0,
              std::string(label) + ": a position rebuilt through the view keeps the coordinates' precision");
        check(errors.relative_share <= 1.0,
              std::string(label) + ": a position rebuilt through the camera-relative inverse keeps the coordinates' "
                                   "precision");
    }
}

/// The view direction the reflection probe passes take for a pixel (fs_box_reflection_probe.eshader): from the
/// pixel's view-space position through the inverse view, a direction only. A far-plane point of the absolute inverse
/// view-projection taken as a direction leans toward the world origin by the camera's distance from it over the far
/// plane's.
void test_probe_view_direction()
{
    const math::vec3 eye(10000.25f, 3.5f, -10000.75f);
    const camera cam = make_camera(eye, math::normalize(math::vec3(0.6f, -0.15f, 0.8f)));
    const math::mat4 proj = cam.get_projection().get_matrix();
    const math::mat4 view = cam.get_view().get_matrix();
    const math::mat4 inv_proj = glm::inverse(proj);
    const math::mat4 inv_view = glm::inverse(view);
    const glm::dmat4 exact_view = make_exact_view(view, glm::dvec3(eye));
    const glm::dmat3 rotation = glm::transpose(glm::dmat3(exact_view));

    double worst = 0.0;
    const double offsets[] = {-0.5, 0.0, 0.5};
    for(double x : offsets)
    {
        for(double y : offsets)
        {
            const glm::dvec3 direction = glm::normalize(rotation[2] + x * rotation[0] + 0.5 * y * rotation[1]);
            const math::vec3 clip = to_stored_clip(glm::dmat4(proj) * exact_view, glm::dvec3(eye) + 30.0 * direction);
            const math::vec4 view_position = inv_proj * math::vec4(clip, 1.0f);
            const math::vec3 pixel_direction =
                math::normalize(math::vec3(inv_view * math::vec4(math::vec3(view_position) / view_position.w, 0.0f)));
            // The angle from the cross and dot products: acos of a dot near 1 would read the float normalization's
            // own rounding as an angle.
            const glm::dvec3 measured(pixel_direction);
            const double angle =
                std::atan2(glm::length(glm::cross(measured, direction)), glm::dot(measured, direction));
            worst = std::max(worst, angle);
        }
    }
    std::printf("  a probe pixel's view direction 14 km from the origin is off by %.3g rad\n", worst);
    check(worst < 1.0e-4, "a probe pixel's view direction does not depend on the camera's distance from the origin");
}

/// GiPrevWorldFromViewDepth (gi_motion.sh): the point at a view depth on last frame's ray through a pixel.
auto prev_world_from_view_depth(const camera& cam, const math::vec2& clip_xy, float view_depth) -> math::vec3
{
    const math::mat4 inverse = cam.get_prev_inverse_view_projection_relative_unjittered();
    const auto offset_from_clip = [&](float depth, float& clip_w)
    {
        const math::vec4 offset = inverse * math::vec4(clip_xy, depth, 1.0f);
        clip_w = 1.0f / offset.w;
        return math::vec3(offset) / offset.w;
    };
    float near_depth = 0.0f;
    float far_depth = 0.0f;
    const math::vec3 near_point = offset_from_clip(gfx::camera_near_depth, near_depth);
    const math::vec3 far_point = offset_from_clip(0.5f, far_depth);
    return cam.get_prev_position() + math::mix(near_point, far_point, (view_depth - near_depth) / (far_depth - near_depth));
}

void test_camera_positions_and_previous_frame()
{
    const math::vec3 forward = math::normalize(math::vec3(0.0f, -0.1f, 1.0f));
    const math::vec3 first(10000.25f, 2.0f, -10000.75f);
    const math::vec3 second(10000.5f, 2.0f, -10000.5f);
    camera cam = make_camera(first, forward);
    check(cam.get_position() == first, "a camera reports the eye it looks from, exactly");
    check(cam.get_prev_position() == first, "before any frame is recorded, last frame's eye is the current one");

    cam.record_current_matrices();
    gfx::frame();
    cam.look_at(second, second + forward);
    cam.record_current_matrices();
    check(cam.get_position() == second, "the camera reports this frame's eye");
    check(cam.get_prev_position() == first, "last frame's eye is the eye the previous frame rendered from");

    // A point 6 m in front of last frame's camera, found again from its pixel and view depth.
    const glm::dvec3 exact_first(first);
    const glm::dmat4 prev_view = make_exact_view(cam.get_prev_view().get_matrix(), exact_first);
    const glm::dmat3 prev_rotation = glm::transpose(glm::dmat3(prev_view));
    const glm::dvec3 point = exact_first + 6.0 * prev_rotation[2] + 0.7 * prev_rotation[0] + 0.4 * prev_rotation[1];
    const glm::dmat4 prev_view_proj = glm::dmat4(cam.get_prev_projection_unjittered().get_matrix()) * prev_view;
    const glm::dvec4 clip = prev_view_proj * glm::dvec4(point, 1.0);
    const math::vec2 clip_xy(float(clip.x / clip.w), float(clip.y / clip.w));
    const math::vec3 found = prev_world_from_view_depth(cam, clip_xy, float(clip.w));
    const double error = glm::length(glm::dvec3(found) - point);
    std::printf("  last frame's point 10 km from the origin found again within %.3g m\n", error);
    check(error <= 4.0 * float_step(glm::length(point)),
          "last frame's point is found again at the precision of its coordinates");
}

void run_camera_relative_suite(rtti::context& /*ctx*/)
{
    RUN_CASE(test_reconstruction_far_from_origin());
    RUN_CASE(test_probe_view_direction());

    // Recording a frame's matrices is stamped by the render frame, which only a running device advances.
    bgfx::Init init;
    init.type = bgfx::RendererType::Noop;
    if(!gfx::init(init))
    {
        check(false, "bgfx initialises on the Noop renderer");
        gfx::shutdown();
        return;
    }
    RUN_CASE(test_camera_positions_and_previous_frame());
    gfx::shutdown();
}

} // namespace

REGISTER_TEST_SUITE("camera relative", run_camera_relative_suite)
