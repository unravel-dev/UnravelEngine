/**
 * Validation suite: the morph deform pass's streams over frames (morph_deformer), on bgfx's Noop renderer.
 *
 * Run:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "morph deform"
 *
 * The Noop device runs no compute, so the CPU path deforms; the streams' handles and the binding a model's draws read
 * are what the checks follow: a new shape takes a stream of its own, a still model writes nothing, a weight change
 * writes the other stream and keeps the drawn one as the previous shape, a model no view used and no camera sees
 * keeps its shape, and one coming into a camera's view takes its shape that frame with no motion of its own.
 */

#include "../tests.h"

#include <engine/assets/asset_manager.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/scene.h>
#include <engine/engine.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/morph_deformer.h>
#include <engine/rendering/morph_targets.h>
#include <graphics/graphics.h>
#include <graphics/vertex_decl.h>

#include <array>
#include <memory>
#include <string>

using namespace unravel::test;
using namespace unravel;

namespace
{

constexpr const char* DEFORM_MESH_KEY = "engine:/embedded/morph_deform_test/triangle";

/// A triangle in the unit box at the origin whose vertex 0 rises under Open and vertex 1 under Lift.
auto make_deform_triangle() -> std::shared_ptr<mesh>
{
    mesh::load_data data;
    data.vertex_format = gfx::mesh_vertex::get_layout();
    data.vertex_count = 3;
    data.vertex_data.resize(size_t(data.vertex_count) * data.vertex_format.getStride());
    const std::array<math::vec3, 3> positions{math::vec3(0.0f, 0.0f, 0.0f), math::vec3(1.0f, 0.0f, 0.0f), math::vec3(0.0f, 1.0f, 0.0f)};
    for(uint32_t v = 0; v < 3; ++v)
    {
        const float position[4] = {positions[v].x, positions[v].y, positions[v].z, 1.0f};
        bgfx::vertexPack(position, false, bgfx::Attrib::Position, data.vertex_format, data.vertex_data.data(), v);
    }
    data.triangle_data = {mesh::triangle{0, {0, 1, 2}}};
    data.triangle_count = 1;
    mesh::submesh only;
    only.vertex_start = 0;
    only.vertex_count = 3;
    only.face_start = 0;
    only.face_count = 1;
    data.submeshes = {only};
    data.material_count = 1;
    data.bbox = math::bbox(math::vec3(0.0f), math::vec3(1.0f, 1.0f, 0.0f));
    morph_target_builder builder;
    builder.add("Open", 0.0f, {{0, math::vec3(0.0f, 0.25f, 0.0f), math::vec3(0.0f)}});
    builder.add("Lift", 0.0f, {{1, math::vec3(0.0f, 0.5f, 0.0f), math::vec3(0.0f)}});
    data.morph_targets = builder.build(0.0f, 0.0f);
    auto result = std::make_shared<mesh>();
    check(result->load_mesh(std::move(data), true), "the morph triangle loads with GPU buffers");
    return result;
}

/// The streams a model's draws read now, by handle index: what the checks compare across frames.
struct drawn_streams
{
    bool bound{};
    uint16_t current{bgfx::kInvalidHandle};
    uint16_t previous{bgfx::kInvalidHandle};
    bool readable{};
};

auto read_streams(const model_component& model, const void* deformed_mesh) -> drawn_streams
{
    const auto* binding = model.get_morph_deform_binding();
    if(binding == nullptr || !binding->deforms(deformed_mesh))
    {
        return {};
    }
    return {true, binding->current.idx, binding->previous.idx, binding->is_shader_readable};
}

/// The next render frame, in which the model counts as drawn last frame when @p drawn.
void next_frame(model_component& model, bool drawn)
{
    gfx::frame();
    if(drawn)
    {
        model.set_last_render_frame(gfx::get_render_frame() - 1);
    }
}

/// A camera at @p position looking along +z, or -z when @p looks_away.
void place_camera(entt::handle camera_entity, const math::vec3& position, bool looks_away)
{
    auto& transform = camera_entity.get<transform_component>();
    transform.set_position_global(position);
    transform.set_rotation_euler_global(math::vec3(0.0f, looks_away ? 180.0f : 0.0f, 0.0f));
    camera_entity.get<camera_component>().update(transform.get_transform_global());
}

void test_streams_over_frames(rtti::context& ctx)
{
    auto& am = ctx.get_cached<asset_manager>();
    am.unload_asset<mesh>(DEFORM_MESH_KEY);
    const auto handle = am.get_asset_from_instance<mesh>(DEFORM_MESH_KEY, make_deform_triangle());
    const void* deformed_mesh = handle.get().get();

    morph_deformer deformer;
    check(deformer.init(ctx), "the deform pass starts");
    // The Noop device reports compute, but the runner compiles no shader: the programs stay empty and the CPU deforms.
    check(!deformer.uses_compute(), "without its compute programs the pass deforms on the CPU");

    scene scn("morph deform");
    auto face = scene::create_entity(*scn.registry, "Face");
    auto& model = face.emplace<model_component>();
    unravel::model lods;
    lods.set_lods({handle});
    model.set_model(lods);
    model.update_world_bounds(face.get<transform_component>().get_transform_global());

    // A model drawn last frame takes its first shape into one stream, which stands for the previous shape too.
    next_frame(model, true);
    model.set_morph_weight("Open", 1.0f);
    deformer.update(scn);
    const auto first = read_streams(model, deformed_mesh);
    check(first.bound && first.current == first.previous, "a new shape draws from one stream, with no motion of its own");
    check(!first.readable, "the CPU path's streams have no shader view");

    // Still since last frame: nothing is written.
    next_frame(model, true);
    deformer.update(scn);
    const auto still = read_streams(model, deformed_mesh);
    check(still.bound && still.current == first.current && still.previous == first.current,
          "a model whose weights hold still keeps drawing the same stream");

    // A change writes the other stream; the one drawn last frame becomes the previous shape.
    next_frame(model, true);
    model.set_morph_weight("Open", 0.5f);
    deformer.update(scn);
    const auto changed = read_streams(model, deformed_mesh);
    check(changed.bound && changed.current != first.current && changed.previous == first.current,
          "a weight change draws from the other stream, the last one being the previous shape");

    // No view used it for two frames and no camera sees it: it keeps the shape it left with.
    next_frame(model, false);
    next_frame(model, false);
    model.set_morph_weight("Lift", 1.0f);
    deformer.update(scn);
    const auto gated = read_streams(model, deformed_mesh);
    check(gated.bound && gated.current == changed.current && gated.previous == changed.previous,
          "a model no view used and no camera sees is not deformed when its weights change");

    auto camera_entity = scene::create_entity(*scn.registry, "Camera");
    camera_entity.emplace_or_replace<active_component>();
    camera_entity.emplace<camera_component>().set_viewport_size({64, 64});
    place_camera(camera_entity, math::vec3(0.5f, 0.5f, -3.0f), true);
    next_frame(model, false);
    model.set_morph_weight("Lift", 0.75f);
    model.update_world_bounds(face.get<transform_component>().get_transform_global());
    deformer.update(scn);
    const auto looked_away = read_streams(model, deformed_mesh);
    check(looked_away.bound && looked_away.current == changed.current && looked_away.previous == changed.previous,
          "nor while the only camera looks away from it");

    // A camera turns to it: it takes its shape this frame, with no motion of its own.
    place_camera(camera_entity, math::vec3(0.5f, 0.5f, -3.0f), false);
    next_frame(model, false);
    deformer.update(scn);
    const auto entered = read_streams(model, deformed_mesh);
    check(entered.bound && entered.current == entered.previous,
          "a model coming into a camera's view is deformed that frame, its previous shape the same stream");

    // Every weight at 0: the mesh's own buffer draws it.
    next_frame(model, true);
    model.reset_morph_weights();
    deformer.update(scn);
    check(!read_streams(model, deformed_mesh).bound, "a model at rest draws its mesh and binds no stream");

    deformer.deinit();
    check(model.get_morph_deform_binding() == nullptr, "the pass's end leaves no model bound to a released stream");
    am.unload_asset<mesh>(DEFORM_MESH_KEY);
}

void run_morph_deform(rtti::context& ctx)
{
    bgfx::Init init;
    init.type = bgfx::RendererType::Noop;
    if(!gfx::init(init))
    {
        check(false, "bgfx initialises on the Noop renderer");
        gfx::shutdown();
        return;
    }
    RUN_CASE(test_streams_over_frames(ctx));
    // A camera's pipeline loads textures on this device: they go before it does.
    ctx.get_cached<asset_manager>().unload_all();
    gfx::shutdown();
}

} // namespace

REGISTER_TEST_SUITE("morph deform", run_morph_deform)
