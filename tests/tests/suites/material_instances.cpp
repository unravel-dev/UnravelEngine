/*
 * Validation suite for material instances: a model slot's overrides of its shared material.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "material instances"
 *
 * What it pins:
 *   - a value the instance does not override follows the shared material, also after the shared material changes;
 *   - an override takes only a parameter or a texture the shared graph has, and reverting it follows the shared
 *     material again;
 *   - shares_draw is true while every override sits in the registers an instanced draw reads per instance, a masked
 *     Standard material included; a slot whose overrides feed nothing a shadow reads casts with the shared material;
 *   - the resolved material applies the overrides, keeps its identity when the shared material changes, and counts
 *     per-instance edits apart from the others;
 *   - an instance survives a JSON and a binary round trip;
 *   - an older save, a whole material per slot (a PBR material), turns into the parameters and textures where it
 *     differs from the shared material, through the model's own load;
 *   - an instance saved over a PBR material loads its overrides as the Standard graph's parameters and textures;
 *   - a model without overrides saves an empty instance list, as before;
 *   - the default materials load by the keys they had while they were made at startup ("engine:/embedded/standard");
 *   - a reload that skips one slot's instance (a prefab instance's override of it) keeps that slot's live instance;
 *   - a load that lists some of a slot's overrides (editing several entities at once) sets or reverts those and keeps
 *     the instance's other overrides, also for a slot the document has no instance for; one listing the whole slot
 *     replaces it.
 */

#include "../tests.h"

#include <engine/assets/asset_manager.h>
#include <engine/defaults/engine_asset_keys.h>
#include <engine/engine.h>
#include <engine/meta/assets/asset_database.hpp>
#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/core/math/vector.hpp>
#include <engine/meta/rendering/material_instance.hpp>
#include <engine/meta/rendering/model.hpp>
#include <engine/meta/rendering/standard_material.hpp>
#include <engine/rendering/material_graph/standard_material.h>
#include <engine/rendering/material_instance.h>
#include <engine/rendering/model.h>

#include <hpp/string_view.hpp>
#include <uuid/uuid.h>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/document_paths.h>
#include <serialization/serialization.h>
#include <serialization/types/vector.hpp>

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>

using namespace unravel::test;

using namespace unravel;

namespace
{

const math::color SHARED_BASE_COLOR{0.2f, 0.4f, 0.6f, 1.0f};
const math::color INSTANCE_EMISSIVE_COLOR{1.0f, 0.1f, 0.1f, 1.0f};
constexpr const char* BASE_COLOR = "Base Color";
constexpr const char* EMISSIVE_COLOR = "Emissive Color";
constexpr const char* EMISSIVE_INTENSITY = "Emissive Intensity";
constexpr const char* ROUGHNESS = "Roughness";
constexpr const char* METALLIC = "Metallic";
constexpr const char* TILING = "Tiling";
constexpr const char* COLOR_MAP = "Color Map";
constexpr float MATCH_TOLERANCE = 1e-5f;

auto is_near(const math::vec4& a, const math::vec4& b) -> bool
{
    return math::all(math::lessThanEqual(math::abs(a - b), math::vec4(MATCH_TOLERANCE)));
}

/// A Standard shared material registered under @p name, replacing an earlier one of that name.
auto make_shared_material(const std::string& name, alpha_mode alpha = alpha_mode::opaque) -> asset_handle<material>
{
    auto& am = engine::context().get_cached<asset_manager>();
    const std::string key = "engine:/embedded/material_instance_test/" + name;
    am.unload_asset<material>(key);
    standard_surface surface;
    surface.base_color = SHARED_BASE_COLOR;
    surface.alpha = alpha;
    return am.get_asset_from_instance<material>(key, make_standard_material(surface));
}

auto get_graph(const asset_handle<material>& handle) -> std::shared_ptr<graph_material>
{
    return std::static_pointer_cast<graph_material>(handle.get());
}

/// What the instance gives parameter @p name; zero when it gives none.
auto get_parameter(const material_instance& instance, const char* name) -> math::vec4
{
    return instance.get_graph_parameter(name).value_or(math::vec4(0.0f));
}

/// The value the Standard graph holds for a picker color.
auto to_parameter(const math::color& color) -> math::vec4
{
    return color.to_linear().value;
}

auto make_instance(const asset_handle<material>& shared) -> material_instance
{
    material_instance instance;
    instance.set_shared_material(shared);
    return instance;
}

/// An empty texture registered under @p name.
auto make_texture(const std::string& name) -> asset_handle<gfx::texture>
{
    auto& am = engine::context().get_cached<asset_manager>();
    const std::string key = "engine:/embedded/material_instance_test/texture_" + name;
    auto texture = am.get_asset_from_instance<gfx::texture>(key, std::make_shared<gfx::texture>());
    texture.get();
    return texture;
}

void test_values_follow_the_shared_material()
{
    std::printf("values the instance does not override follow the shared material\n");
    const auto shared = make_shared_material("follow");
    auto instance = make_instance(shared);
    check(!instance.has_overrides(), "a new instance has no overrides");
    check(is_near(get_parameter(instance, BASE_COLOR), to_parameter(SHARED_BASE_COLOR)), "it reads the shared base color");
    check(instance.set_graph_parameter(EMISSIVE_COLOR, to_parameter(INSTANCE_EMISSIVE_COLOR)), "an emissive color override applies");
    const math::vec4 new_base{0.9f, 0.8f, 0.7f, 1.0f};
    get_graph(shared)->set_parameter(BASE_COLOR, new_base);
    check(get_parameter(instance, BASE_COLOR) == new_base, "the base color follows the shared material");
    check(is_near(get_parameter(instance, EMISSIVE_COLOR), to_parameter(INSTANCE_EMISSIVE_COLOR)),
          "the overridden emissive color keeps the instance's value");
}

void test_override_rules()
{
    std::printf("an override takes only what the shared graph has\n");
    const auto shared = make_shared_material("rules");
    auto instance = make_instance(shared);
    check(!instance.set_graph_parameter("No Such Parameter", math::vec4(1.0f)), "an unknown parameter is refused");
    check(!instance.set_graph_texture("No Such Map", {}), "an unknown texture is refused");
    check(!instance.has_overrides(), "refused overrides leave no trace");
    check(instance.set_graph_parameter(ROUGHNESS, math::vec4(0.75f)), "a value applies to roughness");
    check(instance.is_graph_parameter_overridden(ROUGHNESS), "roughness is overridden");
    instance.revert_graph_parameter(ROUGHNESS);
    check(!instance.is_graph_parameter_overridden(ROUGHNESS), "a reverted parameter is no longer overridden");
    check(get_parameter(instance, ROUGHNESS) == *get_graph(shared)->get_parameter(ROUGHNESS),
          "a reverted parameter reads the shared value");
    instance.set_graph_parameter(METALLIC, math::vec4(1.0f));
    instance.set_graph_texture(COLOR_MAP, {});
    instance.revert_all();
    check(!instance.has_overrides(), "revert_all drops every override");
}

void test_standard_register_layout()
{
    std::printf("the Standard graph keeps the per-instance values in the first registers\n");
    const auto shared = make_shared_material("layout");
    const auto graph = get_graph(shared);
    const auto register_of = [&graph](const char* name)
    {
        const auto* parameter = graph->find_parameter(name);
        return parameter != nullptr ? parameter->register_index : MATERIAL_GRAPH_MAX_PARAMETER_REGISTERS;
    };
    for(const char* name : {BASE_COLOR, "Subsurface Color", EMISSIVE_COLOR, EMISSIVE_INTENSITY, ROUGHNESS, METALLIC, "Bumpiness"})
    {
        check(register_of(name) < material::MAX_INSTANCE_CONSTANTS, std::string(name) + " is per instance");
    }
    check(register_of(TILING) >= material::MAX_INSTANCE_CONSTANTS, "the tiling is the material's");
    check(register_of("Dither Threshold") >= material::MAX_INSTANCE_CONSTANTS, "the dither threshold is the material's");
    const auto& parameters = graph->get_parameters();
    check(!parameters.empty() && parameters.front().name == BASE_COLOR, "the parameters list in the graph's order");
}

void test_shares_draw()
{
    std::printf("shares_draw follows the per-instance registers\n");
    const auto shared = make_shared_material("shares_draw");
    auto instance = make_instance(shared);
    check(instance.shares_draw(), "an instance without overrides shares the draw");
    instance.set_graph_parameter(BASE_COLOR, math::vec4(1.0f, 0.0f, 0.0f, 0.5f));
    instance.set_graph_parameter(EMISSIVE_INTENSITY, math::vec4(4.0f));
    check(instance.shares_draw(), "colors and numbers keep sharing the draw");
    instance.set_graph_parameter(TILING, math::vec4(2.0f, 2.0f, 0.0f, 0.0f));
    check(!instance.shares_draw(), "a tiling override gives the slot a draw of its own");
    instance.revert_graph_parameter(TILING);
    check(instance.shares_draw(), "reverting it shares the draw again");
    instance.set_graph_texture(COLOR_MAP, {});
    check(!instance.shares_draw(), "a texture override gives the slot a draw of its own");
    const auto masked = make_shared_material("shares_draw_masked", alpha_mode::mask);
    auto masked_instance = make_instance(masked);
    check(get_graph(masked)->draws_own_shadow(), "a masked Standard material draws its own shadow");
    masked_instance.set_graph_parameter(BASE_COLOR, math::vec4(1.0f, 0.0f, 0.0f, 0.5f));
    check(masked_instance.shares_draw(), "a masked material's color override still shares the draw");
}

void test_shadow_sharing()
{
    std::printf("overrides that feed nothing a shadow reads cast with the shared material\n");
    const auto shared = make_shared_material("shadow_sharing", alpha_mode::mask);
    model mdl;
    mdl.set_shared_material(shared, 0);
    auto& instance = mdl.get_material_instance(0);
    instance.set_graph_parameter(ROUGHNESS, math::vec4(0.9f));
    mdl.update_material_instances();
    check(instance.get_resolved_shares_shadow() && mdl.get_shadow_material(0) == shared.get(),
          "a roughness override of a masked material casts with the shared material");
    check(mdl.get_render_material(0) != shared.get(), "while the slot draws with its resolved material");
    instance.set_graph_parameter(BASE_COLOR, to_parameter(math::color{1.0f, 1.0f, 1.0f, 0.5f}));
    mdl.update_material_instances();
    check(!instance.get_resolved_shares_shadow() && mdl.get_shadow_material(0) == mdl.get_render_material(0),
          "a base color override, which the mask reads, casts with the resolved material");
}

void test_resolved_material()
{
    std::printf("the resolved material applies the overrides in place\n");
    const auto shared = make_shared_material("resolved");
    auto instance = make_instance(shared);
    instance.update_resolved();
    check(instance.get_resolved() == nullptr, "an instance without overrides resolves to nothing");
    instance.set_graph_parameter(EMISSIVE_COLOR, to_parameter(INSTANCE_EMISSIVE_COLOR));
    instance.update_resolved();
    const auto resolved = std::static_pointer_cast<graph_material>(instance.get_resolved());
    check(resolved != nullptr, "an override resolves to a material");
    if(!resolved)
    {
        return;
    }
    check(is_near(*resolved->get_parameter(EMISSIVE_COLOR), to_parameter(INSTANCE_EMISSIVE_COLOR)),
          "the resolved material has the override");
    check(is_near(*resolved->get_parameter(BASE_COLOR), to_parameter(SHARED_BASE_COLOR)), "and the shared base color");
    const auto state_revision = resolved->get_state_revision();
    const auto values_revision = resolved->get_instance_values_revision();
    instance.set_graph_parameter(EMISSIVE_INTENSITY, math::vec4(3.0f));
    instance.update_resolved();
    check(instance.get_resolved() == resolved, "a new override keeps the resolved material's identity");
    check(resolved->get_parameter(EMISSIVE_INTENSITY)->x == 3.0f, "and reaches it");
    check(resolved->get_state_revision() == state_revision, "a per-instance override leaves the state revision");
    check(resolved->get_instance_values_revision() > values_revision, "and counts as a per-instance edit");
    const auto texture = make_texture("resolved");
    instance.set_graph_texture(COLOR_MAP, texture);
    instance.update_resolved();
    check(resolved->get_texture(COLOR_MAP) == texture, "a texture override reaches the resolved material");
    instance.revert_graph_texture(COLOR_MAP);
    instance.update_resolved();
    check(resolved->get_texture(COLOR_MAP) == get_graph(shared)->get_texture(COLOR_MAP),
          "a reverted texture override takes the shared texture again");
    const math::vec4 new_base{0.1f, 0.9f, 0.1f, 1.0f};
    get_graph(shared)->set_parameter(BASE_COLOR, new_base);
    instance.update_resolved();
    check(instance.get_resolved() == resolved, "a shared material edit keeps the resolved material's identity");
    check(*resolved->get_parameter(BASE_COLOR) == new_base, "and reaches it");
    check(is_near(*resolved->get_parameter(EMISSIVE_COLOR), to_parameter(INSTANCE_EMISSIVE_COLOR)),
          "the overrides stay on top");
    instance.revert_graph_parameter(EMISSIVE_INTENSITY);
    instance.update_resolved();
    check(*resolved->get_parameter(EMISSIVE_INTENSITY) == *get_graph(shared)->get_parameter(EMISSIVE_INTENSITY),
          "a reverted override takes the shared value again");
    instance.revert_all();
    instance.update_resolved();
    check(instance.get_resolved() == nullptr, "no overrides, no resolved material");
}

template<typename OutputArchive, typename InputArchive>
auto round_trip(const material_instance& saved, const asset_handle<material>& shared, std::ios::openmode mode)
    -> material_instance
{
    std::stringstream stream(std::ios::in | std::ios::out | mode);
    {
        OutputArchive ar(stream);
        try_save(ar, ser20::make_nvp("instance", saved));
    }
    material_instance loaded;
    {
        InputArchive ar(stream);
        try_load(ar, ser20::make_nvp("instance", loaded));
    }
    loaded.set_shared_material(shared);
    return loaded;
}

void test_round_trips()
{
    std::printf("an instance round-trips through JSON and binary\n");
    const auto shared = make_shared_material("round_trip");
    auto instance = make_instance(shared);
    instance.set_graph_parameter(EMISSIVE_COLOR, to_parameter(INSTANCE_EMISSIVE_COLOR));
    instance.set_graph_parameter(ROUGHNESS, math::vec4(0.65f));
    instance.set_graph_parameter(TILING, math::vec4(3.0f, 1.5f, 0.0f, 0.0f));
    instance.set_graph_texture(COLOR_MAP, {});
    check(!instance.set_override("cull_type"_hs, cull_type::none), "the cull type is the graph's: no override takes it");
    const auto json = round_trip<ser20::oarchive_associative_t, ser20::iarchive_associative_t>(instance, shared, {});
    check(json == instance, "the JSON round trip keeps every override");
    const auto binary =
        round_trip<ser20::oarchive_binary_t, ser20::iarchive_binary_t>(instance, shared, std::ios::binary);
    check(binary == instance, "the binary round trip keeps every override");
    const auto empty = make_instance(shared);
    const auto empty_json = round_trip<ser20::oarchive_associative_t, ser20::iarchive_associative_t>(empty, shared, {});
    check(!empty_json.has_overrides(), "an instance without overrides loads without overrides");
}

/// A whole PBR material as an older save kept it per slot: the shared material's surface with @p change applied.
auto make_legacy_copy(const std::function<void(standard_surface&)>& change) -> std::shared_ptr<legacy_pbr_material>
{
    auto copy = std::make_shared<legacy_pbr_material>();
    copy->surface.base_color = SHARED_BASE_COLOR;
    change(copy->surface);
    return copy;
}

void test_legacy_copy()
{
    std::printf("a whole PBR material from an older save becomes its differences\n");
    const auto shared = make_shared_material("legacy_copy");
    auto instance = make_instance(shared);
    instance.set_legacy_copy(make_legacy_copy(
        [](standard_surface& surface)
        {
            surface.emissive_color = INSTANCE_EMISSIVE_COLOR;
            surface.roughness = 0.9f;
            surface.cull = cull_type::none;
        }));
    check(instance.has_overrides() && !instance.shares_draw(), "a waiting copy counts as overrides");
    std::vector<graph_parameter_value> parameters;
    std::vector<graph_texture_value> textures;
    instance.collect_graph_overrides(parameters, textures);
    check(parameters.size() == 2 && textures.empty(), "saving it writes only the two differences");
    check(instance.convert_legacy_copy(true), "it converts once the shared material is loaded");
    check(!instance.has_legacy_copy(), "the copy is gone");
    check(instance.get_graph_parameter_overrides().size() == 2 && instance.is_graph_parameter_overridden(EMISSIVE_COLOR) &&
              instance.is_graph_parameter_overridden(ROUGHNESS),
          "the overrides are exactly the differing parameters");
    check(get_parameter(instance, ROUGHNESS).x == 0.9f, "with the copy's values");
    check(instance.get_overrides().empty(), "the copy's cull type, the graph's, is no override");
    check(instance.shares_draw(), "so the instance keeps the shared draw");
    get_graph(shared)->set_parameter(METALLIC, math::vec4(0.5f));
    check(get_parameter(instance, METALLIC).x == 0.5f, "a value the copy did not change follows the shared material from then on");
}

/// The shape of a model saved before material instances held overrides: a whole material per slot.
struct legacy_model_document
{
    std::vector<asset_handle<material>> materials;
    std::vector<material::sptr> material_instances;

    template<typename Archive>
    void save(Archive& ar) const
    {
        std::vector<asset_handle<mesh>> lods;
        try_save(ar, ser20::make_nvp("lods", lods));
        try_save(ar, ser20::make_nvp("materials", materials));
        try_save(ar, ser20::make_nvp("material_instances", material_instances));
    }
};

void test_legacy_model_document()
{
    std::printf("a model saved with whole PBR materials per slot loads them as overrides\n");
    const auto shared = make_shared_material("legacy_model");
    const math::color yellow{1.0f, 1.0f, 0.0f, 1.0f};
    material::sptr copy = make_legacy_copy(
        [&yellow](standard_surface& surface)
        {
            surface.base_color = yellow;
        });
    legacy_model_document document{{shared}, {copy}};
    std::stringstream stream;
    {
        ser20::oarchive_associative_t ar(stream);
        try_save(ar, ser20::make_nvp("model", document));
    }
    check(stream.str().find("\"pbr_material\"") != std::string::npos, "the copy is saved as a PBR material was");
    model loaded;
    {
        ser20::iarchive_associative_t ar(stream);
        check(try_load(ar, ser20::make_nvp("model", loaded)), "the old document loads as a model");
    }
    auto& instance = loaded.get_material_instance(0);
    check(instance.has_legacy_copy(), "the slot's whole material waits to be compared");
    check(instance.get_shared_material() == shared, "the instance knows its shared material");
    instance.convert_legacy_copy(true);
    check(instance.get_graph_parameter_overrides().size() == 1 && instance.is_graph_parameter_overridden(BASE_COLOR),
          "only the base color differed, so only it is overridden");
    check(is_near(get_parameter(instance, BASE_COLOR), to_parameter(yellow)), "in the graph's linear space");
}

/// An instance an older version saved over a PBR material: its overrides by the PBR property names.
constexpr const char* LEGACY_INSTANCE_JSON = R"({"instance": {
    "material_type": "pbr_material",
    "cull_type": 0,
    "base_color": {"r": 1.0, "g": 0.5, "b": 0.25, "a": 1.0},
    "roughness": 0.35,
    "metalness": 0.6,
    "tiling": {"x": 2.0, "y": 3.0},
    "dither_threshold": {"x": 0.5, "y": 4.0},
    "alpha_mode": 1,
    "color_map": {"uid": "00000000-0000-0000-0000-000000000000"}
}})";

void test_legacy_instance_overrides()
{
    std::printf("an instance saved over a PBR material loads its overrides as the Standard graph's\n");
    const auto shared = make_shared_material("legacy_overrides");
    material_instance loaded;
    {
        std::stringstream stream(LEGACY_INSTANCE_JSON);
        ser20::iarchive_associative_t ar(stream);
        check(try_load(ar, ser20::make_nvp("instance", loaded)), "the old instance loads");
    }
    loaded.set_shared_material(shared);
    check(loaded.get_material_type() == entt::resolve<graph_material>(), "its type is the graph material");
    check(loaded.is_graph_parameter_overridden(BASE_COLOR) &&
              is_near(get_parameter(loaded, BASE_COLOR), to_parameter(math::color{1.0f, 0.5f, 0.25f, 1.0f})),
          "the base color becomes the Base Color parameter, in linear space");
    check(get_parameter(loaded, ROUGHNESS).x == 0.35f, "a number becomes its parameter");
    check(loaded.is_graph_parameter_overridden(METALLIC) && get_parameter(loaded, METALLIC).x == 0.6f,
          "the PBR material's metalness becomes the Metallic parameter");
    check(get_parameter(loaded, TILING) == math::vec4(2.0f, 3.0f, 0.0f, 0.0f), "the tiling keeps both numbers");
    check(get_parameter(loaded, "Dither Threshold").x == 4.0f, "the dither threshold keeps its distance");
    check(loaded.is_graph_texture_overridden(COLOR_MAP), "a map becomes its texture node's override");
    check(!loaded.has_overrides() || loaded.get_overrides().empty(), "the cull type and the alpha mode, the graph's, are dropped");
    check(loaded.get_graph_parameter_overrides().size() == 5, "every other value is a parameter override");
}

/// What follows a saved instance in a binary stream: it must read back intact.
constexpr uint32_t BINARY_SENTINEL = 0xC0FFEEu;

/// An instance an older version saved in binary over a PBR material, followed by @p sentinel: its type, then its
/// overrides as (name, value) pairs.
auto make_legacy_binary_instance(const math::color& base_color, uint32_t sentinel) -> std::string
{
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    {
        ser20::oarchive_binary_t ar(stream);
        std::string type_name = "pbr_material";
        uint32_t count = 1;
        std::string property = "base_color";
        math::color value = base_color;
        try_save(ar, ser20::make_nvp("material_type", type_name));
        try_save(ar, ser20::make_nvp("count", count));
        try_save(ar, ser20::make_nvp("property", property));
        try_save(ar, ser20::make_nvp("base_color", value));
        try_save(ar, ser20::make_nvp("sentinel", sentinel));
    }
    return stream.str();
}

void test_legacy_binary_instance()
{
    std::printf("an instance saved in binary over a PBR material reads all it saved\n");
    const auto shared = make_shared_material("legacy_binary");
    const math::color blue{0.2f, 0.4f, 0.9f, 1.0f};
    std::stringstream stream(make_legacy_binary_instance(blue, BINARY_SENTINEL), std::ios::in | std::ios::binary);
    material_instance loaded;
    uint32_t sentinel = 0;
    {
        ser20::iarchive_binary_t ar(stream);
        try_load(ar, ser20::make_nvp("instance", loaded));
        try_load(ar, ser20::make_nvp("sentinel", sentinel));
    }
    loaded.set_shared_material(shared);
    check(is_near(get_parameter(loaded, BASE_COLOR), to_parameter(blue)), "a binary PBR override loads as the Standard parameter");
    check(sentinel == BINARY_SENTINEL, "and the instance reads all it saved: what follows it reads intact");
}

void test_model_saves_an_empty_list_without_overrides()
{
    std::printf("a model without overrides saves an empty instance list\n");
    const auto shared = make_shared_material("empty_list");
    model mdl;
    mdl.set_shared_material(shared, 0);
    mdl.set_shared_material(shared, 1);
    mdl.set_shared_material(shared, 2);
    std::stringstream stream;
    {
        ser20::oarchive_associative_t ar(stream);
        try_save(ar, ser20::make_nvp("model", mdl));
    }
    const auto text = stream.str();
    check(text.find("\"material_instances\": []") != std::string::npos, "the list is empty");
    mdl.get_material_instance(1).set_graph_parameter(ROUGHNESS, math::vec4(0.2f));
    std::stringstream overridden;
    {
        ser20::oarchive_associative_t ar(overridden);
        try_save(ar, ser20::make_nvp("model", mdl));
    }
    model loaded;
    {
        ser20::iarchive_associative_t ar(overridden);
        try_load(ar, ser20::make_nvp("model", loaded));
    }
    check(loaded.get_material_instances().size() == 3, "every slot has an instance after loading");
    check(loaded.get_material_instance(1).is_graph_parameter_overridden(ROUGHNESS), "the second slot keeps its override");
    check(!loaded.get_material_instance(2).has_overrides(), "the slot after it has none");
}

/// Loads @p document over @p live, skipping the paths @p is_skipped names, as a prefab instance's reload skips
/// its overrides.
auto load_model_over(const model& live, const std::string& document, std::function<bool(hpp::string_view)> is_skipped)
    -> model
{
    model loaded = live;
    serialization::path_context path_ctx;
    path_ctx.enable_recording();
    path_ctx.should_serialize_property_callback = [&is_skipped](const std::string& path)
    {
        return !is_skipped(path);
    };
    auto* previous = serialization::get_path_context();
    serialization::set_path_context(&path_ctx);
    {
        std::stringstream stream(document);
        ser20::iarchive_associative_t ar(stream);
        try_load(ar, ser20::make_nvp("model", loaded));
    }
    serialization::set_path_context(previous);
    return loaded;
}

void test_reload_keeps_skipped_slots()
{
    std::printf("a reload that skips a slot's instance keeps it, and restates the others\n");
    const auto shared = make_shared_material("reload");
    model prefab_model;
    prefab_model.set_shared_material(shared, 0);
    prefab_model.set_shared_material(shared, 1);
    std::stringstream stream;
    {
        ser20::oarchive_associative_t ar(stream);
        try_save(ar, ser20::make_nvp("model", prefab_model));
    }
    const auto document = stream.str();
    model live = prefab_model;
    live.get_material_instance(0).set_graph_parameter(EMISSIVE_COLOR, to_parameter(INSTANCE_EMISSIVE_COLOR));
    live.get_material_instance(1).set_graph_parameter(ROUGHNESS, math::vec4(0.2f));
    const auto slot_reloaded = load_model_over(live,
                                               document,
                                               [](hpp::string_view path)
                                               {
                                                   return path.ends_with("material_instances[0]");
                                               });
    check(slot_reloaded.get_material_instance(0).is_graph_parameter_overridden(EMISSIVE_COLOR),
          "the skipped slot keeps its live override");
    check(!slot_reloaded.get_material_instance(1).has_overrides(), "the other slot is restated by the document");
    const auto list_reloaded = load_model_over(live,
                                               document,
                                               [](hpp::string_view path)
                                               {
                                                   return path.ends_with("material_instances");
                                               });
    check(list_reloaded.get_material_instance(0).has_overrides() &&
              list_reloaded.get_material_instance(1).has_overrides(),
          "skipping the whole list keeps every live slot");
    const auto plain = load_model_over(live,
                                       document,
                                       [](hpp::string_view)
                                       {
                                           return false;
                                       });
    check(!plain.get_material_instance(0).has_overrides() && !plain.get_material_instance(1).has_overrides(),
          "a load that skips nothing restates every slot");
    check(plain.get_material_instances().size() == 2, "and keeps one instance per slot");

    model overridden_prefab = prefab_model;
    overridden_prefab.get_material_instance(0).set_graph_parameter(EMISSIVE_COLOR, to_parameter(INSTANCE_EMISSIVE_COLOR));
    overridden_prefab.get_material_instance(1).set_graph_parameter(ROUGHNESS, math::vec4(0.2f));
    std::stringstream overridden_stream;
    {
        ser20::oarchive_associative_t ar(overridden_stream);
        try_save(ar, ser20::make_nvp("model", overridden_prefab));
    }
    model metallic_live = prefab_model;
    metallic_live.get_material_instance(0).set_graph_parameter(METALLIC, math::vec4(0.6f));
    const auto second_slot_reloaded = load_model_over(metallic_live,
                                                      overridden_stream.str(),
                                                      [](hpp::string_view path)
                                                      {
                                                          return path.ends_with("material_instances[0]");
                                                      });
    check(second_slot_reloaded.get_material_instance(0).is_graph_parameter_overridden(METALLIC) &&
              !second_slot_reloaded.get_material_instance(0).is_graph_parameter_overridden(EMISSIVE_COLOR),
          "a skipped first slot keeps its live instance");
    check(second_slot_reloaded.get_material_instance(1).is_graph_parameter_overridden(ROUGHNESS) &&
              !second_slot_reloaded.get_material_instance(1).is_graph_parameter_overridden(EMISSIVE_COLOR),
          "the slot after it reads its own instance from the document, not the skipped slot's");
}

/// @p model saved as a document, under "model".
auto save_model_document(const model& mdl) -> std::string
{
    std::stringstream stream;
    {
        ser20::oarchive_associative_t ar(stream);
        try_save(ar, ser20::make_nvp("model", mdl));
    }
    return stream.str();
}

/// Loads @p document over @p live, limited to @p paths, as editing several entities at once loads an edited value.
auto load_model_listing(const model& live, const std::string& document, const serialization::path_set& paths) -> model
{
    model loaded = live;
    const serialization::scoped_path_filter filter("model", paths);
    std::stringstream stream(document);
    ser20::iarchive_associative_t ar(stream);
    try_load(ar, ser20::make_nvp("model", loaded));
    return loaded;
}

/// Two slots of @p shared, with an Emissive Color override in the first and a Metallic one in the second.
auto make_two_slot_model(const asset_handle<material>& shared) -> model
{
    model mdl;
    mdl.set_shared_material(shared, 0);
    mdl.set_shared_material(shared, 1);
    mdl.get_material_instance(0).set_graph_parameter(EMISSIVE_COLOR, to_parameter(INSTANCE_EMISSIVE_COLOR));
    mdl.get_material_instance(1).set_graph_parameter(METALLIC, math::vec4(0.6f));
    return mdl;
}

void test_load_listing_some_overrides()
{
    std::printf("a load that lists some of a slot's overrides sets or reverts them and keeps the others\n");
    const auto shared = make_shared_material("listing");
    model source;
    source.set_shared_material(shared, 0);
    source.set_shared_material(shared, 1);
    source.get_material_instance(0).set_graph_parameter(ROUGHNESS, math::vec4(0.2f));
    const auto rough_document = save_model_document(source);
    // What an edit of one row lists: the value, and the instance's type when the instance first appears in the list.
    const serialization::path_set roughness_paths{"material_instances[0]/material_type",
                                                  "material_instances[0]/graph_parameters/Roughness"};

    const auto live = make_two_slot_model(shared);
    const auto merged = load_model_listing(live, rough_document, roughness_paths);
    check(merged.get_material_instance(0).is_graph_parameter_overridden(ROUGHNESS) &&
              get_parameter(merged.get_material_instance(0), ROUGHNESS).x == 0.2f,
          "the listed override is set");
    check(merged.get_material_instance(0).is_graph_parameter_overridden(EMISSIVE_COLOR),
          "the slot keeps the override the load does not list");
    check(merged.get_material_instance(1).is_graph_parameter_overridden(METALLIC), "the other slot keeps its override");

    model plain;
    plain.set_shared_material(shared, 0);
    plain.set_shared_material(shared, 1);
    const auto plain_document = save_model_document(plain);
    const auto reverted = load_model_listing(merged, plain_document, roughness_paths);
    check(!reverted.get_material_instance(0).is_graph_parameter_overridden(ROUGHNESS),
          "a listed override the document has no instance for is reverted");
    check(reverted.get_material_instance(0).is_graph_parameter_overridden(EMISSIVE_COLOR) &&
              reverted.get_material_instance(1).is_graph_parameter_overridden(METALLIC),
          "and the overrides the load does not list stay");

    model metallic_source;
    metallic_source.set_shared_material(shared, 0);
    metallic_source.set_shared_material(shared, 1);
    metallic_source.get_material_instance(0).set_graph_parameter(METALLIC, math::vec4(0.9f));
    const auto unlisted_kept = load_model_listing(merged,
                                                  save_model_document(metallic_source),
                                                  {"material_instances[0]/graph_parameters/Roughness"});
    check(!unlisted_kept.get_material_instance(0).is_graph_parameter_overridden(ROUGHNESS),
          "a listed override the document's instance lacks is reverted");
    check(unlisted_kept.get_material_instance(0).is_graph_parameter_overridden(EMISSIVE_COLOR) &&
              !unlisted_kept.get_material_instance(0).is_graph_parameter_overridden(METALLIC),
          "an override the load does not list stays, and one only the document has is not taken");

    const auto whole = load_model_listing(live, rough_document, {"material_instances[0]"});
    check(whole.get_material_instance(0).is_graph_parameter_overridden(ROUGHNESS) &&
              !whole.get_material_instance(0).is_graph_parameter_overridden(EMISSIVE_COLOR),
          "a load listing the whole slot replaces its instance");
    check(whole.get_material_instance(1).is_graph_parameter_overridden(METALLIC), "and keeps the slots it does not list");
}

void test_default_material_keys()
{
    std::printf("the default materials load by the keys they had while they were made at startup\n");
    check(get_engine_key_of_embedded("engine:/embedded/standard") == STANDARD_MATERIAL_KEY &&
              get_engine_key_of_embedded("engine:/embedded/fallback") == FALLBACK_MATERIAL_KEY &&
              get_default_material_key_of_embedded("engine:/embedded/cube").empty() &&
              get_default_material_key_of_embedded("engine:/embedded/standard_x").empty(),
          "the earlier keys name the default materials' files, and only those");
    auto& am = engine::context().get_cached<asset_manager>();
    const auto path = fs::resolve_protocol(STANDARD_MATERIAL_KEY);
    asset_meta meta;
    load_from_file(path.string() + ".meta", meta);
    am.add_asset_info_for_path(path, meta, true);
    const auto entry = am.get_metadata_for_key(EMBEDDED_STANDARD_MATERIAL_KEY);
    check(entry.location == STANDARD_MATERIAL_KEY && entry.meta.uid == generate_uuid(EMBEDDED_STANDARD_MATERIAL_KEY),
          "the earlier key finds the Standard material's entry, under the id scenes hold");
    const auto handle = am.get_asset<material>(EMBEDDED_STANDARD_MATERIAL_KEY, load_flags::standard, load_mode::deferred);
    check(handle.id() == STANDARD_MATERIAL_KEY, "and a request by it loads the file");
}

void test_material_value_helpers()
{
    std::printf("material values read and write through reflection, base properties included\n");
    auto source = make_standard_material({});
    source->set_cull_type(cull_type::none);
    const auto cull = get_material_value(*source, "cull_type"_hs);
    check(cull.allow_cast<cull_type>() && cull.cast<cull_type>() == cull_type::none, "a base material property reads");
    check(!get_material_value(*source, "no_such_property"_hs), "an unknown property reads empty");
    standard_surface rough;
    rough.roughness = 0.7f;
    auto target = make_standard_material({});
    check(!set_material_value(*target, "cull_type"_hs, math::color{}), "a color is refused for the cull type");
    const auto* address = target.get();
    const auto state_revision = target->get_state_revision();
    const auto values_revision = target->get_instance_values_revision();
    copy_material_values(*make_standard_material(rough), *target);
    check(target.get() == address && target->get_parameter(ROUGHNESS)->x == 0.7f,
          "copying gives the target the source's graph in place");
    check(target->get_instance_values_revision() > values_revision && target->get_state_revision() == state_revision,
          "a copied graph of other values alone counts as a per-instance edit of them");
    standard_surface two_sided;
    two_sided.cull = cull_type::none;
    copy_material_values(*make_standard_material(two_sided), *target);
    check(target->get_state_revision() > state_revision && target->get_cull_type() == cull_type::none,
          "a copied graph of other code counts as a state edit");
}

void run_material_instances_suite(rtti::context& ctx)
{
    RUN_CASE(test_material_value_helpers());
    RUN_CASE(test_default_material_keys());
    RUN_CASE(test_values_follow_the_shared_material());
    RUN_CASE(test_override_rules());
    RUN_CASE(test_standard_register_layout());
    RUN_CASE(test_shares_draw());
    RUN_CASE(test_resolved_material());
    RUN_CASE(test_shadow_sharing());
    RUN_CASE(test_round_trips());
    RUN_CASE(test_legacy_copy());
    RUN_CASE(test_legacy_model_document());
    RUN_CASE(test_legacy_instance_overrides());
    RUN_CASE(test_legacy_binary_instance());
    RUN_CASE(test_model_saves_an_empty_list_without_overrides());
    RUN_CASE(test_reload_keeps_skipped_slots());
    RUN_CASE(test_load_listing_some_overrides());
}

} // namespace

REGISTER_TEST_SUITE("material instances", run_material_instances_suite)
