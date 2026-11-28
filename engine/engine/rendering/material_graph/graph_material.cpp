#include "graph_material.h"
#include "named_values.h"

#include <engine/meta/rendering/material_graph.hpp>

#include <base/hash.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>

namespace unravel
{

namespace
{
/// Whether the Surface Output of @p graph has setting @p name set to @p value.
auto is_output_setting(const material_graph& graph, const char* name, const char* value) -> bool
{
    const int output = find_material_graph_output(graph);
    if(output < 0)
    {
        return false;
    }
    const auto* setting = find_material_graph_setting(graph.nodes[size_t(output)], name);
    return setting != nullptr && *setting == value;
}

/// The faces @p graph hides: none when Two Sided, front faces when Cull Front Faces, else back faces.
auto get_graph_cull_type(const material_graph& graph) -> cull_type
{
    if(is_output_setting(graph, MATERIAL_TWO_SIDED_SETTING, MATERIAL_TWO_SIDED_ON))
    {
        return cull_type::none;
    }
    return is_output_setting(graph, MATERIAL_CULL_FRONT_FACES_SETTING, MATERIAL_CULL_FRONT_FACES_ON) ? cull_type::clockwise
                                                                                                      : cull_type::counter_clockwise;
}

/// Whether two handles give the same shader: the loader's handles all carry the material's key, so the key alone
/// does not tell them apart.
auto is_same_shader(const asset_handle<gfx::shader>& lhs, const asset_handle<gfx::shader>& rhs) -> bool
{
    return lhs == rhs && lhs.version() == rhs.version();
}

/**
 * @brief The aspects the entries of @p current feed whose @p value_of differs from the entry in the same place of
 * @p previous (a compile of the same code); every entry's when the two are laid out otherwise.
 */
template<typename Entry, typename ValueOf>
auto get_changed_value_aspects(const std::vector<Entry>& previous, const std::vector<Entry>& current, ValueOf&& value_of)
    -> render_change::mask
{
    render_change::mask changes = render_change::none;
    for(size_t i = 0; i < current.size(); ++i)
    {
        if(i >= previous.size() || previous[i].name != current[i].name || !(value_of(previous[i]) == value_of(current[i])))
        {
            changes |= current[i].changes;
        }
    }
    return changes;
}

/// The graph materials alive now of each live_count_kind.
std::array<std::atomic<int>, 2> live_material_counts{};
} // namespace

graph_material::live_count_registration::live_count_registration(live_count_kind kind)
    : kind_(kind)
{
}

graph_material::live_count_registration::live_count_registration(const live_count_registration& other)
    : kind_(other.kind_)
{
    set_counted(other.is_counted_);
}

auto graph_material::live_count_registration::operator=(const live_count_registration& other) -> live_count_registration&
{
    set_counted(other.is_counted_);
    return *this;
}

graph_material::live_count_registration::~live_count_registration()
{
    set_counted(false);
}

void graph_material::live_count_registration::set_counted(bool is_counted)
{
    if(is_counted == is_counted_)
    {
        return;
    }
    is_counted_ = is_counted;
    live_material_counts[size_t(kind_)].fetch_add(is_counted ? 1 : -1, std::memory_order_relaxed);
}

auto graph_material::has_offset_bounds() -> bool
{
    return live_material_counts[size_t(live_count_kind::offset_bounds)].load(std::memory_order_relaxed) > 0;
}

auto graph_material::has_animated_shadows() -> bool
{
    return live_material_counts[size_t(live_count_kind::animated_shadow)].load(std::memory_order_relaxed) > 0;
}

graph_material::graph_material()
{
    // A graph of the Surface Output alone binds nothing: there is nothing to compile.
    ensure_material_graph_output(graph_);
    apply_binding({});
}

auto graph_material::clone() const -> std::shared_ptr<material>
{
    return std::make_shared<graph_material>(*this);
}

void graph_material::copy_unreflected_values(const material& source)
{
    const auto* other = source.safe_cast<graph_material>();
    if(other == nullptr)
    {
        return;
    }
    // A new graph takes the source's binding, which matches the source's shader; new values alone are written over
    // the binding's defaults.
    const bool are_functions_changed = functions_ != other->functions_;
    if(are_functions_changed)
    {
        functions_ = other->functions_;
    }
    const bool are_values_changed = take_values(*other);
    const bool is_code_changed = are_functions_changed || shader_hash_ != other->shader_hash_;
    if(is_code_changed || !(graph_ == other->graph_))
    {
        // New copies of the functions it calls, or a shader of other code, are new code.
        bind_graph(other->graph_, other->binding_, !is_code_changed);
    }
    else if(are_values_changed)
    {
        apply_values();
    }
    if(!is_same_shader(shader_, other->shader_))
    {
        set_shader(other->shader_);
    }
    if(!is_same_shader(vertex_shader_, other->vertex_shader_))
    {
        set_vertex_shader(other->vertex_shader_);
    }
    shader_hash_ = other->shader_hash_;
}

void graph_material::copy_values(const graph_material& source)
{
    if(take_values(source))
    {
        apply_values();
    }
}

auto graph_material::take_values(const graph_material& source) -> bool
{
    // Values alone change only what their parameters and textures feed.
    bool is_changed = false;
    if(parameter_values_ != source.parameter_values_)
    {
        const auto changes = get_named_changes(parameter_values_,
                                               source.parameter_values_,
                                               [this](const std::string& name)
                                               {
                                                   const auto* parameter = find_parameter(name);
                                                   return parameter ? parameter->changes : render_change::material_aspects;
                                               });
        parameter_values_ = source.parameter_values_;
        is_changed = true;
        mark_changed(material_property_id::graph_parameter, changes);
    }
    if(texture_values_ != source.texture_values_)
    {
        const auto changes = get_named_changes(texture_values_,
                                               source.texture_values_,
                                               [this](const std::string& name)
                                               {
                                                   const auto* texture = find_texture(name);
                                                   return texture ? texture->changes : render_change::material_aspects;
                                               });
        texture_values_ = source.texture_values_;
        is_changed = true;
        mark_changed(material_property_id::graph_texture, changes);
    }
    return is_changed;
}

auto graph_material::get_graph() const -> const material_graph&
{
    return graph_;
}

void graph_material::set_graph(material_graph graph)
{
    ensure_material_graph_output(graph);
    auto binding = compile_binding(graph);
    bind_graph(std::move(graph), std::move(binding), true);
}

auto graph_material::get_binding() const -> const material_graph_binding&
{
    return binding_;
}

void graph_material::set_compiled_code(std::string shader_hash, material_graph_binding binding)
{
    shader_hash_ = std::move(shader_hash);
    apply_binding(std::move(binding));
    mark_changed(material_property_id::graph);
}

void graph_material::take_compiled(material_graph graph, std::string shader_hash, material_graph_binding binding)
{
    ensure_material_graph_output(graph);
    graph_ = std::move(graph);
    shader_hash_ = std::move(shader_hash);
    apply_binding(std::move(binding));
    mark_changed(material_property_id::graph);
}

void graph_material::bind_graph(material_graph graph, material_graph_binding binding, bool is_code_kept)
{
    const bool is_value_edit = is_code_kept && has_same_material_graph_code(graph_, graph);
    // Of the same code the compile lays the parameters and textures out as before: they compare in place.
    const auto previous_parameters = is_value_edit ? binding_.parameters : std::vector<material_graph_parameter>{};
    const auto previous_textures = is_value_edit ? binding_.textures : std::vector<material_graph_texture>{};
    graph_ = std::move(graph);
    apply_binding(std::move(binding));
    if(!is_value_edit)
    {
        mark_changed(material_property_id::graph);
        return;
    }
    mark_changed(material_property_id::graph_parameter,
                 get_changed_value_aspects(previous_parameters,
                                           binding_.parameters,
                                           [](const material_graph_parameter& parameter)
                                           {
                                               return parameter.default_value;
                                           }));
    mark_changed(material_property_id::graph_texture,
                 get_changed_value_aspects(previous_textures,
                                           binding_.textures,
                                           [](const material_graph_texture& texture)
                                           {
                                               return texture.texture;
                                           }));
}

namespace
{
/// Whether @p handle names an asset that has nothing left to load: loaded, or failed to load.
template<typename T>
auto is_asset_loaded(const asset_handle<T>& handle) -> bool
{
    return handle.is_valid() && handle.try_get() != nullptr;
}

/// Whether @p handle has nothing left to load: it names no asset, or one that is loaded or failed to load.
template<typename T>
auto is_asset_loaded_or_absent(const asset_handle<T>& handle) -> bool
{
    return !handle.is_valid() || handle.try_get() != nullptr;
}
} // namespace

auto graph_material::is_ready_to_draw() const -> bool
{
    if(!is_asset_loaded(get_shader()) || (has_vertex_stage() && !is_asset_loaded(get_vertex_shader())))
    {
        return false;
    }
    const auto& textures = get_textures();
    return std::all_of(textures.begin(),
                       textures.end(),
                       [](const material_graph_texture& texture)
                       {
                           return is_asset_loaded_or_absent(texture.texture);
                       });
}

auto graph_material::get_shader() const -> const asset_handle<gfx::shader>&
{
    return shader_;
}

void graph_material::set_shader(asset_handle<gfx::shader> shader)
{
    // No comparison first: the handles the loader makes all carry the material's key, so two of them compare equal.
    shader_ = std::move(shader);
    mark_changed(material_property_id::graph);
}

auto graph_material::get_vertex_shader() const -> const asset_handle<gfx::shader>&
{
    return vertex_shader_;
}

void graph_material::set_vertex_shader(asset_handle<gfx::shader> shader)
{
    // No comparison first, as set_shader.
    vertex_shader_ = std::move(shader);
    mark_changed(material_property_id::graph);
}

auto graph_material::get_shader_hash() const -> const std::string&
{
    return shader_hash_;
}

void graph_material::set_shader_hash(std::string hash)
{
    shader_hash_ = std::move(hash);
}

auto graph_material::get_functions() const -> const function_copies&
{
    return functions_;
}

void graph_material::set_functions(function_copies functions)
{
    functions_ = std::move(functions);
    apply_binding(compile_binding(graph_));
    mark_changed(material_property_id::graph);
}

auto graph_material::copy_functions() -> bool
{
    function_copies copies;
    bool is_complete = true;
    for(const auto& key : collect_material_function_keys(graph_, load_material_function))
    {
        if(auto function = load_material_function(key))
        {
            copies[key] = std::move(function);
        }
        else
        {
            is_complete = false;
        }
    }
    set_functions(std::move(copies));
    return is_complete;
}

auto graph_material::resolve_function(const std::string& key) const -> std::shared_ptr<const material_graph>
{
    // The copies first: the shader was compiled from them, so the layout of the registers matches it until a recompile
    // brings new copies.
    const auto found = functions_.find(key);
    return found != functions_.end() ? found->second : load_material_function(key);
}

auto graph_material::get_parameters() const -> const std::vector<material_graph_parameter>&
{
    return binding_.parameters;
}

auto graph_material::find_parameter(hpp::string_view name) const -> const material_graph_parameter*
{
    const auto& parameters = binding_.parameters;
    const auto found = std::find_if(parameters.begin(),
                                    parameters.end(),
                                    [&](const material_graph_parameter& parameter)
                                    {
                                        return parameter.name == name;
                                    });
    return found == parameters.end() ? nullptr : &*found;
}

auto graph_material::get_parameter(hpp::string_view name) const -> hpp::optional<math::vec4>
{
    const auto* parameter = find_parameter(name);
    if(parameter == nullptr)
    {
        return {};
    }
    const auto* set = find_named(parameter_values_, name);
    return set ? set->value : parameter->default_value;
}

auto graph_material::set_parameter(hpp::string_view name, const math::vec4& value) -> bool
{
    const auto* parameter = find_parameter(name);
    if(parameter == nullptr)
    {
        return false;
    }
    if(auto* set = find_parameter_value(name))
    {
        if(set->value == value)
        {
            return true;
        }
        set->value = value;
    }
    else
    {
        parameter_values_.push_back({std::string(name), value});
    }
    write_parameter(*parameter, value);
    mark_changed(material_property_id::graph_parameter, parameter->changes);
    return true;
}

auto graph_material::get_parameter_values() const -> const std::vector<graph_parameter_value>&
{
    return parameter_values_;
}

auto graph_material::find_texture(hpp::string_view name) const -> const material_graph_texture*
{
    const auto found = std::find_if(textures_.begin(),
                                    textures_.end(),
                                    [&](const material_graph_texture& texture)
                                    {
                                        return texture.name == name;
                                    });
    return found == textures_.end() ? nullptr : &*found;
}

auto graph_material::get_texture(hpp::string_view name) const -> asset_handle<gfx::texture>
{
    const auto* texture = find_texture(name);
    return texture ? texture->texture : asset_handle<gfx::texture>{};
}

auto graph_material::set_texture(hpp::string_view name, const asset_handle<gfx::texture>& texture) -> bool
{
    if(find_texture(name) == nullptr)
    {
        return false;
    }
    auto* set = find_texture_value(name);
    if(set != nullptr && set->texture == texture && set->texture.version() == texture.version())
    {
        return true;
    }
    if(set != nullptr)
    {
        set->texture = texture;
    }
    else
    {
        texture_values_.push_back({std::string(name), texture});
    }
    mark_changed(material_property_id::graph_texture, write_texture(name, texture));
    return true;
}

auto graph_material::write_texture(hpp::string_view name, const asset_handle<gfx::texture>& texture) -> render_change::mask
{
    // One name may stand for several nodes (each call of a function holding one): they read one texture.
    render_change::mask changes = render_change::none;
    for(auto& entry : textures_)
    {
        if(entry.name == name)
        {
            entry.texture = texture;
            changes |= entry.changes;
        }
    }
    return changes;
}

auto graph_material::get_texture_values() const -> const std::vector<graph_texture_value>&
{
    return texture_values_;
}

auto graph_material::get_parameter_registers() const -> const std::vector<math::vec4>&
{
    return parameter_registers_;
}

auto graph_material::get_textures() const -> const std::vector<material_graph_texture>&
{
    return textures_;
}

auto graph_material::get_static_switches() const -> const std::vector<material_graph_static_switch>&
{
    return binding_.static_switches;
}

auto graph_material::get_emissive_estimate() const -> math::vec3
{
    const auto& estimate = binding_.emissive;
    if(!estimate.is_known)
    {
        return math::vec3(0.0f);
    }
    math::vec3 emissive = estimate.constant;
    for(const auto& name : estimate.parameters)
    {
        const auto* parameter = find_parameter(name);
        const auto value = get_parameter(name);
        if(parameter == nullptr || !value)
        {
            return math::vec3(0.0f);
        }
        emissive *= get_material_value_width(parameter->type) == 1 ? math::vec3(value->x) : math::vec3(*value);
    }
    return emissive;
}

auto graph_material::uses_opacity_mask() const -> bool
{
    return binding_.uses_opacity_mask;
}

auto graph_material::is_opacity_mask_animated() const -> bool
{
    return binding_.is_opacity_mask_animated;
}

auto graph_material::uses_world_position_offset() const -> bool
{
    return binding_.uses_world_position_offset;
}

auto graph_material::reads_vertex_outputs() const -> bool
{
    return binding_.reads_vertex_outputs;
}

auto graph_material::has_vertex_stage() const -> bool
{
    return binding_.uses_world_position_offset || binding_.reads_vertex_outputs;
}

auto graph_material::computes_vertex_values() const -> bool
{
    return binding_.computes_vertex_values;
}

auto graph_material::is_world_position_offset_animated() const -> bool
{
    return binding_.is_world_position_offset_animated;
}

auto graph_material::get_max_world_position_offset() const -> float
{
    return binding_.max_world_position_offset;
}

auto graph_material::draws_own_shadow() const -> bool
{
    return binding_.uses_opacity_mask || binding_.uses_world_position_offset || binding_.casts_forward_shadow;
}

auto graph_material::is_translucent() const -> bool
{
    return binding_.blend_mode != material_blend_mode::opaque;
}

auto graph_material::get_blend_mode() const -> material_blend_mode
{
    return binding_.blend_mode;
}

auto graph_material::is_forward() const -> bool
{
    return binding_.domain == material_domain::surface && is_material_blend_mode_forward(binding_.blend_mode);
}

auto graph_material::get_shading_model() const -> material_shading_model
{
    return binding_.shading_model;
}

auto graph_material::preserves_specular() const -> bool
{
    return binding_.preserve_specular;
}

auto graph_material::get_taa_response() const -> float
{
    return binding_.taa_response;
}

auto graph_material::is_order_independent() const -> bool
{
    return is_forward() && binding_.order_independent;
}

auto graph_material::has_gi_variant() const -> bool
{
    return is_forward() && binding_.has_gi_variant;
}

auto graph_material::has_gi_reflection_variant() const -> bool
{
    return is_forward() && binding_.has_gi_reflection_variant;
}

auto graph_material::has_depth_prepass() const -> bool
{
    return is_forward() && binding_.depth_prepass;
}

auto graph_material::writes_velocity() const -> bool
{
    return is_forward() && binding_.writes_velocity;
}

auto graph_material::is_particle_use() const -> bool
{
    return is_forward() && binding_.particle_use;
}

auto graph_material::get_draw_hash() const -> uint64_t
{
    // The code's hash names the shader: the loader's shader handles carry their material's key and no uid.
    std::size_t hash = std::hash<std::string>{}(get_shader_hash());
    for(const auto& value : get_parameter_registers())
    {
        for(int component = 0; component < 4; ++component)
        {
            uint32_t bits = 0;
            std::memcpy(&bits, &value[component], sizeof(bits));
            utils::hash_combine(hash, bits);
        }
    }
    for(const auto& texture : get_textures())
    {
        utils::hash_combine(hash, texture.texture.uid());
        utils::hash_combine(hash, texture.texture.id());
        utils::hash_combine(hash, (uint32_t(texture.address) << 8) | uint32_t(texture.filter));
    }
    return uint64_t(hash);
}

auto graph_material::binds_same_draw(const graph_material& other) const -> bool
{
    if(this == &other)
    {
        return true;
    }
    const auto& textures = get_textures();
    const auto& other_textures = other.get_textures();
    // Materials of the same code share one loaded shader, which the programs are made per: the code's hash tells, and
    // the loaded shader itself for a material no compile gave a hash.
    const auto& code = get_shader_hash();
    if(code != other.get_shader_hash() || (code.empty() && get_shader().version() != other.get_shader().version()) ||
       get_parameter_registers() != other.get_parameter_registers() || textures.size() != other_textures.size())
    {
        return false;
    }
    for(size_t i = 0; i < textures.size(); ++i)
    {
        const auto& own = textures[i];
        const auto& theirs = other_textures[i];
        // The loaded texture, which every handle of one asset shares and which tells the ones made at run time apart.
        // Two that have no load yet both read the default texture.
        if(own.texture.version() != theirs.texture.version() || own.address != theirs.address || own.filter != theirs.filter)
        {
            return false;
        }
    }
    return true;
}

auto graph_material::get_domain() const -> material_domain
{
    return binding_.domain;
}

auto graph_material::get_decal_writes() const -> uint32_t
{
    return binding_.decal_writes;
}

auto graph_material::decal_reads_surface() const -> bool
{
    return binding_.decal_reads_surface;
}

auto graph_material::get_post_process_location() const -> material_post_process_location
{
    return binding_.post_location;
}

auto graph_material::get_post_process_priority() const -> int
{
    return binding_.post_priority;
}

auto graph_material::get_scene_textures() const -> uint32_t
{
    return binding_.scene_textures;
}

auto graph_material::casts_shadow() const -> bool
{
    const bool is_cast = binding_.blend_mode == material_blend_mode::opaque || (is_forward() && binding_.casts_forward_shadow);
    return is_cast && binding_.domain == material_domain::surface;
}

auto graph_material::is_shadow_animated() const -> bool
{
    const bool is_animated = ((binding_.uses_opacity_mask || binding_.casts_forward_shadow) && binding_.is_opacity_mask_animated) ||
                             binding_.is_world_position_offset_animated;
    return is_animated && casts_shadow();
}

auto graph_material::write_instance_constants(math::vec4* out) const -> uint32_t
{
    const auto count = std::min<size_t>(parameter_registers_.size(), MAX_INSTANCE_CONSTANTS);
    std::copy_n(parameter_registers_.begin(), count, out);
    return uint32_t(count);
}

void graph_material::add_content_versions(render_change_values& keys) const
{
    for(const auto& texture : textures_)
    {
        add_identity_key(keys, texture.changes, texture.texture.version());
    }
    add_identity_key(keys, render_change::material_aspects, shader_.version());
    add_identity_key(keys, render_change::material_aspects, vertex_shader_.version());
}

auto graph_material::compile_binding(const material_graph& graph) const -> material_graph_binding
{
    material_graph_compile_options options;
    options.resolve_function = [this](const std::string& key)
    {
        return resolve_function(key);
    };
    return make_material_graph_binding(compile_material_graph(graph, options));
}

void graph_material::apply_binding(material_graph_binding binding)
{
    binding_ = std::move(binding);
    default_registers_ = make_material_graph_parameter_registers(binding_);
    offset_bounds_registration_.set_counted(binding_.uses_world_position_offset && binding_.max_world_position_offset > 0.0f);
    animated_shadow_registration_.set_counted(is_shadow_animated());
    cull_type_ = get_graph_cull_type(graph_);
    apply_values();
}

void graph_material::apply_values()
{
    parameter_registers_ = default_registers_;
    textures_ = binding_.textures;
    for(const auto& set : parameter_values_)
    {
        if(const auto* parameter = find_parameter(set.name))
        {
            write_parameter(*parameter, set.value);
        }
    }
    for(const auto& set : texture_values_)
    {
        write_texture(set.name, set.texture);
    }
}

void graph_material::write_parameter(const material_graph_parameter& parameter, const math::vec4& value)
{
    if(parameter.register_index >= parameter_registers_.size())
    {
        return;
    }
    auto& target = parameter_registers_[parameter.register_index];
    if(get_material_value_width(parameter.type) == 1)
    {
        target[int(parameter.component)] = value.x;
        return;
    }
    target = value;
}

auto graph_material::find_parameter_value(hpp::string_view name) -> graph_parameter_value*
{
    return find_named(parameter_values_, name);
}

auto graph_material::find_texture_value(hpp::string_view name) -> graph_texture_value*
{
    return find_named(texture_values_, name);
}

auto get_material_domain(const material& mat) -> material_domain
{
    const auto* graph = mat.safe_cast<graph_material>();
    return graph != nullptr ? graph->get_domain() : material_domain::surface;
}

auto find_loaded_material_domain(const asset_handle<material>& mat) -> hpp::optional<material_domain>
{
    const auto loaded = mat ? mat.try_get() : nullptr;
    // Null while the material still loads (or for a handle without an asset); the empty asset after a failed load.
    if(!loaded || asset_handle<material>::is_empty(loaded))
    {
        return hpp::nullopt;
    }
    return get_material_domain(*loaded);
}

} // namespace unravel
