/*
 * Validation suite for the translucency pass: which surfaces it draws, the order it draws them in, and what a model
 * keeps of it.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "translucency"
 *
 * What it pins:
 *   - the passes: the G-buffer draws Opaque and Dithered surfaces, the translucency pass the other Blend Modes, and a
 *     G-buffer submit reports a model whose surfaces it left out; a pass of every material draws them all;
 *   - the order: a lower Sort Priority draws first, then the farther, as particle batches sort;
 *   - a scene round trip keeps a model's Sort Priority, and a scene without it reads 0;
 *   - the GI: the radiance cache points a model marks, and the region the front layer's reflections run over.
 */

#include "../tests.h"

#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/scene.h>
#include <engine/meta/ecs/entity.hpp>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/rendering/material_graph/graph_material.h>
#include <engine/rendering/material_graph/material_graph.h>
#include <engine/rendering/model.h>
#include <engine/rendering/gi/gi_pass_constants.h>
#include <engine/rendering/pipeline/pipeline.h>
#include <engine/rendering/pipeline/passes/gi_radiance_cache.h>
#include <engine/rendering/pipeline/passes/gi_reflection_pass.h>

#include <algorithm>
#include <array>
#include <set>
#include <cstdio>
#include <iterator>
#include <fstream>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

using namespace unravel;
using namespace unravel::test;

namespace
{

/// A graph material of @p mode.
auto make_translucency_material(material_blend_mode mode) -> graph_material
{
    material_graph graph;
    ensure_material_graph_output(graph);
    set_material_graph_blend_mode(graph, mode);
    graph_material mat;
    mat.set_graph(graph);
    return mat;
}

/// Why an emitter's sprites, trails and ribbons cannot draw a material, as the Inspector's Material row and its
/// picker say it: each state of the material's output is told apart.
void test_particle_material_problem()
{
    std::printf("test_particle_material_problem\n");
    const auto find_problem = [](const material_graph& graph)
    {
        graph_material mat;
        mat.set_graph(graph);
        return particle_emitter_component::find_material_problem(mat);
    };
    material_graph opaque;
    ensure_material_graph_output(opaque);
    check(find_problem(opaque) == particle_material_problem::not_forward, "an opaque surface draws in the G-buffer, where particles do not");

    material_graph translucent = opaque;
    set_material_graph_blend_mode(translucent, material_blend_mode::translucent);
    check(find_problem(translucent) == particle_material_problem::particle_use_off,
          "a see-through surface without Particle Use needs it turned on");

    // The suite compiles no shader: a material that would draw particles has none to draw them with.
    material_graph particle = translucent;
    check(set_material_graph_particle_use(particle, true) && find_problem(particle) == particle_material_problem::no_shader,
          "a surface with Particle Use and no compiled shader says its shader is missing");

    material_graph decal = opaque;
    check(set_material_graph_domain(decal, material_domain::decal) && find_problem(decal) == particle_material_problem::not_surface,
          "a decal material is no Surface material");
}

void test_material_passes()
{
    std::printf("test_material_passes\n");
    for(uint32_t i = 0; i < MATERIAL_BLEND_MODE_COUNT; ++i)
    {
        const auto mode = material_blend_mode(i);
        const auto mat = make_translucency_material(mode);
        const bool is_forward = is_material_blend_mode_forward(mode);
        bool found_forward = false;
        check(is_material_drawn_in(mat, model_material_pass::deferred, &found_forward) == !is_forward &&
                  found_forward == is_forward,
              std::string("the G-buffer draws ") + get_material_blend_mode_label(mode) +
                  " surfaces exactly when the translucency pass does not, and reports those it leaves out");
        check(is_material_drawn_in(mat, model_material_pass::forward, nullptr) == is_forward,
              std::string("the translucency pass draws ") + get_material_blend_mode_label(mode) + " surfaces as its Blend Mode says");
        check(is_material_drawn_in(mat, model_material_pass::all, nullptr), "a pass of every material draws it");
        check(mat.is_forward() == is_forward && mat.casts_shadow() == (mode == material_blend_mode::opaque),
              "only an Opaque surface casts a shadow");
    }
}

void test_draw_order()
{
    std::printf("test_draw_order\n");
    using key = rendering::translucent_mesh_list::key;
    check(rendering::is_drawn_before(key{0, 9.0f}, key{0, 4.0f}) && !rendering::is_drawn_before(key{0, 4.0f}, key{0, 9.0f}),
          "of one Sort Priority, the farther draws first");
    check(rendering::is_drawn_before(key{-1, 1.0f}, key{0, 100.0f}) && !rendering::is_drawn_before(key{1, 100.0f}, key{0, 1.0f}),
          "a lower Sort Priority draws first, however near");
    check(!rendering::is_drawn_before(key{0, 4.0f}, key{0, 4.0f}), "equal draws keep their order");
    std::vector<key> keys = {{0, 1.0f}, {2, 50.0f}, {0, 30.0f}, {-3, 2.0f}, {0, 10.0f}};
    std::stable_sort(keys.begin(), keys.end(), rendering::is_drawn_before);
    const bool is_sorted = keys[0].sort_priority == -3 && keys[1].distance == 30.0f && keys[2].distance == 10.0f &&
                           keys[3].distance == 1.0f && keys[4].sort_priority == 2;
    check(is_sorted, "a list sorts by Sort Priority, then far to near");

    rendering::translucent_mesh_list list;
    check(list.is_empty(), "a list without draws is empty");
    list.has_order_independent = true;
    check(!list.is_empty(), "Order Independent surfaces alone are something to draw");
}

void test_gi_surface_points()
{
    std::printf("test_gi_surface_points\n");
    const math::vec3 eye(0.0f);
    const float cell = gi_radiance_cache::get_cell_size(0);
    std::vector<math::vec4> points;
    // A small model near the camera: one point in the middle.
    check(gi_radiance_cache::make_surface_points(math::bbox(math::vec3(4.0f, -0.4f, -0.4f), math::vec3(4.8f, 0.4f, 0.4f)), eye, 64, 1.0f, points) &&
              points.size() == 1 && math::distance(math::vec3(points[0]), math::vec3(4.4f, 0.0f, 0.0f)) < 1e-4f,
          "a model under a cell wide marks around its middle");
    // A pane a cell apart along its length: neighbours mark every probe between them.
    points.clear();
    check(gi_radiance_cache::make_surface_points(math::bbox(math::vec3(2.0f, 0.0f, -5.0f), math::vec3(2.1f, 2.0f, 5.0f)), eye, 1024, 1.0f, points),
          "a pane fits its budget");
    // A point marks the two lattice planes around it on each axis: neighbours under two cells apart leave none out.
    std::vector<float> along_pane;
    for(const auto& point : points)
    {
        if(point.y == points.front().y)
        {
            along_pane.push_back(point.z);
        }
    }
    std::sort(along_pane.begin(), along_pane.end());
    float widest_gap = 0.0f;
    for(size_t i = 1; i < along_pane.size(); ++i)
    {
        widest_gap = std::max(widest_gap, along_pane[i] - along_pane[i - 1]);
    }
    check(along_pane.size() > 2 && widest_gap > 0.0f && widest_gap < 2.0f * cell, "its points along the pane are under two cells apart");
    // A large box keeps its points near its faces.
    points.clear();
    const math::bbox room(math::vec3(-6.0f), math::vec3(6.0f));
    check(gi_radiance_cache::make_surface_points(room, eye, 100000, 1.0f, points), "a room-sized box fits a large budget");
    const bool all_near_faces = std::all_of(points.begin(), points.end(), [&](const math::vec4& point)
    {
        const math::vec3 to_faces = math::min(math::vec3(point) - room.min, room.max - math::vec3(point));
        return std::min(to_faces.x, std::min(to_faces.y, to_faces.z)) <= cell + 1e-4f;
    });
    check(all_near_faces, "a box several cells wide marks near its faces, not its empty inside");
    // A model reaching past the finest clipmap: its far part is a coarser clipmap's cell apart, and no point there sits
    // in the finer clipmap's reach.
    points.clear();
    const float reach0 = gi_radiance_cache::get_reach(0);
    check(gi_radiance_cache::make_surface_points(math::bbox(math::vec3(0.0f, 0.0f, 1.0f), math::vec3(0.1f, 0.1f, reach0 * 3.0f)), eye, 100000, 1.0f, points),
          "a long model fits a large budget");
    size_t near_points = 0;
    size_t far_points = 0;
    for(const auto& point : points)
    {
        (point.z <= reach0 ? near_points : far_points)++;
    }
    check(near_points > 0 && far_points > 0 && far_points < near_points * 2 + 2,
          "past the finest clipmap the points spread to the coarser cells");
    // Twice the spacing for a model too large for its share: fewer points over the same box.
    std::vector<math::vec4> coarse;
    const math::bbox pool(math::vec3(-8.0f, 0.0f, -8.0f), math::vec3(8.0f, 0.2f, 8.0f));
    points.clear();
    check(gi_radiance_cache::make_surface_points(pool, eye, 100000, 1.0f, points) &&
              gi_radiance_cache::make_surface_points(pool, eye, 100000, 2.0f, coarse) && coarse.size() * 3 < points.size() && !coarse.empty(),
          "twice the spacing lays a quarter of the points over a flat model");
    // Past its budget a model marks nothing: it is left out whole.
    points.clear();
    check(!gi_radiance_cache::make_surface_points(room, eye, 8, 1.0f, points), "a model past its budget is refused");
}

/// The radiance cache's lattice as its shaders see it with the camera at an eye position, in the floats they
/// compute with (gi_radiance_cache::place_clipmaps, gi_radiance_cache_common.sh): the clipmap a position reads, and
/// the probes a lookup there interpolates.
struct cache_lattice
{
    static constexpr int clipmap_count = int(gi::GI_RADIANCE_CACHE_CLIPMAPS);
    static constexpr int grid = int(gi::GI_RADIANCE_CACHE_GRID);
    /// A probe: its clipmap and its cell.
    using probe = std::array<int, 4>;

    /// u_gi_rc_clipmaps. Per clipmap: the corner of cell (0, 0, 0), and the cell size.
    std::array<std::array<float, 4>, size_t(clipmap_count)> clipmaps{};

    explicit cache_lattice(const math::vec3& eye)
    {
        for(int clipmap = 0; clipmap < clipmap_count; ++clipmap)
        {
            const double cell = 2.0 * double(gi::GI_RADIANCE_CACHE_EXTENT) / double(grid) * double(1 << clipmap);
            for(int axis = 0; axis < 3; ++axis)
            {
                const double snapped = std::floor(double(eye[axis]) / cell) * cell;
                clipmaps[size_t(clipmap)][size_t(axis)] = float(snapped - 0.5 * cell - 0.5 * double(grid) * cell);
            }
            clipmaps[size_t(clipmap)][3] = float(cell);
        }
    }

    /// GiRcClipmapOf: the finest clipmap whose lattice covers @p position, or clipmap_count.
    auto get_clipmap_of(const math::vec3& position) const -> int
    {
        for(int clipmap = 0; clipmap < clipmap_count; ++clipmap)
        {
            const auto& clip = clipmaps[size_t(clipmap)];
            bool is_inside = true;
            for(int axis = 0; axis < 3; ++axis)
            {
                const float cells = (position[axis] - clip[size_t(axis)]) / clip[3];
                is_inside = is_inside && cells > 0.5f && cells < float(grid) - 0.5f;
            }
            if(is_inside)
            {
                return clipmap;
            }
        }
        return clipmap_count;
    }

    /// GiRcInterpolationProbe: calls @p visit with each of the eight probes around @p position in @p clipmap and its
    /// trilinear weight.
    template<typename Visit>
    void for_each_probe(const math::vec3& position, int clipmap, const Visit& visit) const
    {
        const auto& clip = clipmaps[size_t(clipmap)];
        std::array<int, 3> base{};
        std::array<float, 3> fraction{};
        for(int axis = 0; axis < 3; ++axis)
        {
            const float lattice = (position[axis] - clip[size_t(axis)]) / clip[3] - 0.5f;
            base[size_t(axis)] = int(std::floor(lattice));
            fraction[size_t(axis)] = lattice - std::floor(lattice);
        }
        for(int corner = 0; corner < 8; ++corner)
        {
            const std::array<int, 3> step = {corner & 1, (corner >> 1) & 1, corner >> 2};
            float weight = 1.0f;
            for(size_t axis = 0; axis < 3; ++axis)
            {
                weight *= step[axis] != 0 ? fraction[axis] : 1.0f - fraction[axis];
            }
            visit(probe{clipmap, base[0] + step[0], base[1] + step[1], base[2] + step[2]}, weight);
        }
    }

    /// The probes cs_gi_rc_mark_points.eshader marks for @p points.
    auto mark(const std::vector<math::vec4>& points) const -> std::set<probe>
    {
        std::set<probe> marked;
        for(const auto& point : points)
        {
            const int clipmap = get_clipmap_of(math::vec3(point));
            if(clipmap >= clipmap_count)
            {
                continue;
            }
            for_each_probe(math::vec3(point),
                           clipmap,
                           [&](const probe& cell, float /*weight*/)
                           {
                               if(std::min({cell[1], cell[2], cell[3]}) >= 0 && std::max({cell[1], cell[2], cell[3]}) < grid)
                               {
                                   marked.insert(cell);
                               }
                           });
        }
        return marked;
    }

    /// How much of a lookup at @p position the probes of @p marked answer: the trilinear weights of the ones it
    /// interpolates that are among them. -1 where no clipmap covers the position.
    auto get_coverage(const math::vec3& position, const std::set<probe>& marked) const -> float
    {
        const int clipmap = get_clipmap_of(position);
        if(clipmap >= clipmap_count)
        {
            return -1.0f;
        }
        float coverage = 0.0f;
        for_each_probe(position,
                       clipmap,
                       [&](const probe& cell, float weight)
                       {
                           coverage += marked.count(cell) != 0 ? weight : 0.0f;
                       });
        return coverage;
    }
};

/// The lookups a volume case makes through @p bounds: a grid @p step apart, the box's far corner, and around the box's
/// middle the faces of every clipmap's lattice cubes with the floats on either side of them, where a lookup changes
/// cube and, at a clipmap's edge, clipmap.
auto make_volume_lookups(const math::bbox& bounds, float step) -> std::vector<math::vec3>
{
    std::vector<math::vec3> lookups;
    for(float z = bounds.min.z; z <= bounds.max.z; z += step)
    {
        for(float y = bounds.min.y; y <= bounds.max.y; y += step)
        {
            for(float x = bounds.min.x; x <= bounds.max.x; x += step)
            {
                lookups.emplace_back(x, y, z);
            }
        }
    }
    lookups.push_back(bounds.max);
    const math::vec3 middle = bounds.get_center();
    for(int clipmap = 0; clipmap < cache_lattice::clipmap_count; ++clipmap)
    {
        const float cell = gi_radiance_cache::get_cell_size(uint32_t(clipmap));
        for(int axis = 0; axis < 3; ++axis)
        {
            // Every face of this clipmap's cubes that crosses the box along the axis.
            for(float face = std::ceil(bounds.min[axis] / cell) * cell; face <= bounds.max[axis]; face += cell)
            {
                for(const float along : {std::nextafter(face, bounds.min[axis]), face, std::nextafter(face, bounds.max[axis])})
                {
                    if(along >= bounds.min[axis] && along <= bounds.max[axis])
                    {
                        math::vec3 lookup = middle;
                        lookup[axis] = along;
                        lookups.push_back(lookup);
                    }
                }
            }
        }
    }
    return lookups;
}

/// The points of what fills a box, as particles fill their emitter's: every probe a lookup inside interpolates is
/// marked, in whichever clipmap the lookup reads, and boxes that overlap mark the same probes, counted once.
void test_gi_volume_points()
{
    std::printf("test_gi_volume_points\n");
    // The rules cache_lattice copies, where the shaders state them.
    const auto read_shader = [](const std::string& key)
    {
        std::ifstream file(fs::resolve_protocol(key), std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::erase(text, '\r');
        return text;
    };
    const std::string common = read_shader("engine:/data/shaders/gi/gi_radiance_cache_common.sh");
    const std::string lattice_header = read_shader("engine:/data/shaders/gi/gi_radiance_cache_lattice.sh");
    const std::string mark_points = read_shader("engine:/data/shaders/gi/cs_gi_rc_mark_points.eshader");
    const auto has_text = [](const std::string& text, const char* part)
    {
        return text.find(part) != std::string::npos;
    };
    check(has_text(common, "vec3 f = (position - clip.xyz) / clip.w;") &&
              has_text(common, "all(greaterThan(f, vec3_splat(0.5))) && all(lessThan(f, vec3_splat(float(GI_RADIANCE_CACHE_GRID) - 0.5)))") &&
              has_text(common, "return ivec3(floor((position - clip.xyz) / clip.w - 0.5));") &&
              has_text(lattice_header, "vec3 lattice = (origin - clip.xyz) / clip.w - 0.5;") &&
              has_text(mark_points, "int clipmap = GiRcClipmapOf(position);") &&
              has_text(mark_points, "ivec3 base = GiRcInterpolationBase(position, clipmap);"),
          "the shaders pick a clipmap and a lookup's probes by the rules this test copies");

    // The grid snaps to the camera's lattice cube, so where the camera sits in its cube moves every clipmap's edges;
    // far from the origin the shaders' floats are coarse.
    const std::array<math::vec3, 6> eyes = {math::vec3(0.0f),
                                            math::vec3(0.3f, 7.9f, -3.2f),
                                            math::vec3(100.9f, 0.05f, 51.99f),
                                            math::vec3(-17.25f, -3.4f, 8.6f),
                                            math::vec3(8000.37f, -7999.6f, 123.45f),
                                            math::vec3(-8200.9f, 40.1f, 8100.2f)};
    struct box_case
    {
        math::vec3 low;
        math::vec3 high;
        /// How far apart the lookups are that the case makes through the box.
        float step;
        const char* name;
    };
    const std::array<box_case, 6> boxes = {{
        {{-3.0f, -1.0f, -2.5f}, {2.5f, 3.0f, 3.2f}, 0.29f, "a box beside the camera"},
        {{12.0f, -1.5f, -2.0f}, {64.0f, 1.5f, 2.0f}, 0.83f, "a box across the edges of the two finest clipmaps"},
        {{-58.0f, 0.0f, -30.0f}, {-18.0f, 2.5f, -19.0f}, 0.83f, "a box across those edges on the other side"},
        {{-2.0f, 12.0f, -40.0f}, {2.0f, 40.0f, -12.0f}, 0.83f, "a box across the finest clipmap's edges upward and behind"},
        {{60.0f, 10.0f, 60.0f}, {110.0f, 14.0f, 64.0f}, 1.7f, "a box across the edge of the third clipmap"},
        {{150.0f, 0.0f, 0.0f}, {260.0f, 3.0f, 3.0f}, 0.83f, "a box across the last clipmap's edge"},
    }};
    for(const auto& eye : eyes)
    {
        const cache_lattice lattice(eye);
        // The reach is the most a clipmap covers along an axis and the inner reach the least, on either side.
        bool is_reach_outside = true;
        bool is_inner_reach_inside = true;
        for(int axis = 0; axis < 3; ++axis)
        {
            for(const float side : {-1.0f, 1.0f})
            {
                math::vec3 direction(0.0f);
                direction[axis] = side;
                is_reach_outside = is_reach_outside && lattice.get_clipmap_of(eye + direction * (gi_radiance_cache::get_reach(0) + 0.01f)) != 0;
                is_inner_reach_inside = is_inner_reach_inside &&
                                        lattice.get_clipmap_of(eye + direction * (gi_radiance_cache::get_inner_reach(0) - 0.01f)) == 0;
            }
        }
        check(is_reach_outside && is_inner_reach_inside,
              "past the reach nothing reads the finest clipmap and within the inner reach everything does");
        for(const auto& box : boxes)
        {
            const math::bbox bounds(eye + box.low, eye + box.high);
            std::vector<math::vec4> points;
            const bool fits = gi_radiance_cache::make_volume_points(bounds, eye, 100000, points);
            const auto marked = lattice.mark(points);
            float least_coverage = 1.0f;
            size_t lookups = 0;
            for(const auto& position : make_volume_lookups(bounds, box.step))
            {
                const float coverage = lattice.get_coverage(position, marked);
                if(coverage >= 0.0f)
                {
                    least_coverage = std::min(least_coverage, coverage);
                    ++lookups;
                }
            }
            // A lookup within a float's step of a cube's face may read the cube next door, whose far probes carry
            // that step's share of the weight.
            check(fits && lookups > 100 && least_coverage > 0.99f,
                  std::string(box.name) + ": every probe a lookup in the box interpolates is marked (least coverage " +
                      std::to_string(least_coverage) + " over " + std::to_string(lookups) + " lookups)");
            std::vector<math::vec4> refused;
            check(!points.empty() && !gi_radiance_cache::make_volume_points(bounds, eye, points.size() - 1, refused),
                  std::string(box.name) + ": a budget one point short refuses it");
        }
    }

    // One point a lattice cube: a box beside the camera takes as many points as it reaches cubes of the finest clipmap.
    const math::vec3 eye(0.3f, 7.9f, -3.2f);
    const float cell = gi_radiance_cache::get_cell_size(0);
    const auto count_cubes = [&](const math::bbox& box)
    {
        return math::ivec3(math::floor(box.max / cell)) - math::ivec3(math::floor(box.min / cell)) + 1;
    };
    const auto count_corners = [&](const math::bbox& box)
    {
        const math::ivec3 cubes = count_cubes(box);
        return size_t(cubes.x + 1) * size_t(cubes.y + 1) * size_t(cubes.z + 1);
    };
    const math::bbox smoke(eye + math::vec3(-3.0f, -1.0f, -2.5f), eye + math::vec3(2.5f, 3.0f, 3.2f));
    std::vector<math::vec4> smoke_points;
    check(gi_radiance_cache::make_volume_points(smoke, eye, 100000, smoke_points), "a box of smoke fits a large budget");
    const math::ivec3 cubes = count_cubes(smoke);
    check(smoke_points.size() == size_t(cubes.x) * size_t(cubes.y) * size_t(cubes.z), "it takes one point for each lattice cube it reaches");

    // Boxes that overlap name the same points where they do.
    const auto has_point = [](const std::vector<math::vec4>& points, const math::vec4& wanted)
    {
        return std::any_of(points.begin(), points.end(), [&](const math::vec4& point)
        {
            return point.x == wanted.x && point.y == wanted.y && point.z == wanted.z;
        });
    };
    const math::vec3 shift(1.3f, 0.4f, -0.7f);
    const math::bbox embers(smoke.min + shift, smoke.max + shift);
    const math::bbox shared(math::max(smoke.min, embers.min), math::min(smoke.max, embers.max));
    std::vector<math::vec4> ember_points;
    std::vector<math::vec4> shared_points;
    check(gi_radiance_cache::make_volume_points(embers, eye, 100000, ember_points) &&
              gi_radiance_cache::make_volume_points(shared, eye, 100000, shared_points) && !shared_points.empty(),
          "a second box over the first, and the box they share, fit too");
    const bool names_same_points = std::all_of(shared_points.begin(), shared_points.end(), [&](const math::vec4& point)
    {
        return has_point(smoke_points, point) && has_point(ember_points, point);
    });
    check(names_same_points, "two boxes name the same points where they overlap");

    // The probes are what is counted: a box marks the corners of its cubes, and a second box only what the first left.
    constexpr size_t plenty = 100000;
    gi_radiance_cache::volume_marks marks;
    std::vector<math::vec4> added;
    check(marks.add(smoke_points, plenty, plenty, added) && added.size() == smoke_points.size() &&
              marks.get_probe_count() == count_corners(smoke),
          "the first box adds every point and marks the corners of its cubes");
    const size_t after_smoke = added.size();
    check(marks.add(ember_points, plenty, plenty, added) && added.size() > after_smoke &&
              added.size() - after_smoke < ember_points.size() &&
              marks.get_probe_count() == count_corners(smoke) + count_corners(embers) - count_corners(shared),
          "a box over it adds only the points that mark something new, and the probes both mark count once");
    const size_t after_embers = added.size();
    const size_t probes_of_both = marks.get_probe_count();
    check(marks.add(smoke_points, 0, probes_of_both, added) && added.size() == after_embers && marks.get_probe_count() == probes_of_both,
          "a box whose probes are all marked costs nothing");

    // Past its points or its probes a box changes nothing.
    gi_radiance_cache::volume_marks tight;
    std::vector<math::vec4> kept = {math::vec4(1.0f)};
    check(!tight.add(smoke_points, smoke_points.size() - 1, plenty, kept) && kept.size() == 1 && tight.get_probe_count() == 0,
          "a box of more points than are left is refused whole");
    check(!tight.add(smoke_points, plenty, count_corners(smoke) - 1, kept) && kept.size() == 1 && tight.get_probe_count() == 0,
          "a box of more probes than are left is refused whole");
    check(tight.add(smoke_points, smoke_points.size(), count_corners(smoke), kept) && kept.size() == 1 + smoke_points.size(),
          "a box that takes exactly what is left fits");
    gi_radiance_cache::volume_marks one;
    std::vector<math::vec4> one_cube;
    std::vector<math::vec4> one_added;
    check(gi_radiance_cache::make_volume_points(math::bbox(eye + math::vec3(0.1f), eye + math::vec3(0.2f)), eye, 16, one_cube) &&
              one_cube.size() == 1 && one.add(one_cube, 1, 8, one_added) && one.get_probe_count() == 8,
          "a box inside one lattice cube takes one point, which marks the cube's eight probes");

    // Past its budget a box is refused, a box far too large without walking it, and one no clipmap reaches takes no point.
    std::vector<math::vec4> refused;
    check(!gi_radiance_cache::make_volume_points(math::bbox(eye - math::vec3(500.0f), eye + math::vec3(500.0f)), eye, 1024, refused) &&
              refused.empty(),
          "a box far too large is refused before any point is made");
    check(!gi_radiance_cache::make_volume_points(math::bbox(math::vec3(-1.0e30f), math::vec3(1.0e30f)), eye, 1024, refused) && refused.empty(),
          "a box past any world is refused the same way");
    std::vector<math::vec4> beyond;
    check(gi_radiance_cache::make_volume_points(math::bbox(eye + math::vec3(500.0f), eye + math::vec3(510.0f)), eye, 16, beyond) && beyond.empty(),
          "a box past every clipmap takes no point");
}

void test_front_reflection_region()
{
    std::printf("test_front_reflection_region\n");
    using region = gi_reflection_pass::dispatch_region;
    const usize32_t view{1920, 1080};
    const int32_t margin = gi_reflection_pass::front_region_margin;
    const auto covers = [&](const region& r, const math::ivec2& low, const math::ivec2& high, int32_t factor)
    {
        const bool aligned = r.trace_origin.x % 8 == 0 && r.trace_origin.y % 8 == 0 && r.trace_size.x % 8 == 0 && r.trace_size.y % 8 == 0;
        const bool pixels_follow = r.pixel_origin == r.trace_origin * factor;
        const math::ivec2 want_low = math::max(low - margin, math::ivec2(0));
        const math::ivec2 want_high = math::min(high + margin, math::ivec2(1920, 1080));
        const bool holds = r.pixel_origin.x <= want_low.x && r.pixel_origin.y <= want_low.y &&
                           r.pixel_origin.x + r.pixel_size.x >= want_high.x && r.pixel_origin.y + r.pixel_size.y >= want_high.y;
        const bool inside = r.pixel_origin.x + r.pixel_size.x <= 1920 && r.pixel_origin.y + r.pixel_size.y <= 1080;
        return aligned && pixels_follow && holds && inside;
    };
    for(const uint32_t factor : {1u, 2u})
    {
        const math::ivec2 low(700, 300);
        const math::ivec2 high(901, 517);
        const auto r = gi_reflection_pass::get_front_dispatch_region(low, high, view, factor);
        check(covers(r, low, high, int32_t(factor)),
              "the region holds the surfaces and their margin, on whole trace groups, inside the view (downsample " +
                  std::to_string(factor) + ")");
        check(r.pixel_size.x < 400 && r.pixel_size.y < 400, "and stays near them, not the whole view");
        const auto corner = gi_reflection_pass::get_front_dispatch_region(math::ivec2(1900, 1070), math::ivec2(1920, 1080), view, factor);
        check(covers(corner, math::ivec2(1900, 1070), math::ivec2(1920, 1080), int32_t(factor)), "a corner region ends at the view's edge");
        const auto whole = gi_reflection_pass::get_front_dispatch_region(math::ivec2(0), math::ivec2(1920, 1080), view, factor);
        check(whole.pixel_origin == math::ivec2(0) && whole.pixel_size == math::ivec2(1920, 1080), "the whole view is the whole view");
    }
    const auto empty = gi_reflection_pass::get_front_dispatch_region(math::ivec2(500), math::ivec2(500), view, 1);
    check(empty.pixel_size.x > 0, "an empty box still runs its margin");
}

void test_front_reflection_target_size()
{
    std::printf("test_front_reflection_target_size\n");
    using pass = gi_reflection_pass;
    const usize32_t view{1920, 1080};
    pass::front_target_size state;
    const auto size_for = [&](const math::ivec2& region)
    {
        const auto size = pass::update_front_target_size(state, region, view);
        return math::ivec2(int32_t(size.width), int32_t(size.height));
    };
    check(size_for({300, 200}) == math::ivec2(512, 256), "the targets start at the region rounded up to whole steps");
    check(size_for({500, 250}) == math::ivec2(512, 256), "a region that still fits keeps them");
    check(size_for({520, 250}) == math::ivec2(768, 256), "a larger region grows them at once");
    check(size_for({1900, 1070}) == math::ivec2(1920, 1080), "they never pass the view's size");
    bool kept = true;
    for(uint32_t frame = 1; frame < pass::front_target_shrink_frames; ++frame)
    {
        kept = kept && size_for({300, 200}) == math::ivec2(1920, 1080);
    }
    check(kept, "a smaller region keeps them while it has not lasted");
    check(size_for({300, 200}) == math::ivec2(512, 256), "and shrinks them once it has");
    for(uint32_t frame = 1; frame < pass::front_target_shrink_frames; ++frame)
    {
        size_for(frame % 2 == 0 ? math::ivec2(200, 100) : math::ivec2(300, 200));
    }
    check(size_for({600, 200}) == math::ivec2(768, 256), "growing restarts the wait");
    check(size_for({200, 100}) == math::ivec2(768, 256), "so one smaller frame after it does not shrink them");
}

void test_sort_priority_round_trip()
{
    std::printf("test_sort_priority_round_trip\n");
    scene src("src");
    auto entity = src.create_entity("glass");
    entity.emplace<model_component>().set_sort_priority(-5);
    std::stringstream stream;
    save_to_stream(stream, src);
    scene dst("dst");
    load_from_stream(stream, dst);
    int loaded = 1;
    dst.registry->view<model_component>().each(
        [&](auto, model_component& comp)
        {
            loaded = comp.get_sort_priority();
        });
    check(loaded == -5, "a model keeps its Sort Priority");
    check(model_component().get_sort_priority() == 0, "a model starts at Sort Priority 0");
}

void run_translucency_suite(rtti::context& /*ctx*/)
{
    RUN_CASE(test_particle_material_problem());
    RUN_CASE(test_material_passes());
    RUN_CASE(test_draw_order());
    RUN_CASE(test_sort_priority_round_trip());
    RUN_CASE(test_gi_surface_points());
    RUN_CASE(test_gi_volume_points());
    RUN_CASE(test_front_reflection_region());
    RUN_CASE(test_front_reflection_target_size());
}

} // namespace

REGISTER_TEST_SUITE("translucency", run_translucency_suite)
