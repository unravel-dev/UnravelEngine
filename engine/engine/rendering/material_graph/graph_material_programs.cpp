#include "graph_material_programs.h"
#include "graph_material.h"

#include <engine/rendering/default_textures.h>
#include <engine/rendering/material.h>

#include <graphics/graphics.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>

namespace unravel
{

namespace
{
/// Material textures filter anisotropically, up to the device maximum (BGFX_RESET_MAXANISOTROPY): trilinear alone
/// picks the mip from the longer footprint axis and smears surfaces seen at grazing angles, such as distant floors.
constexpr std::uint32_t MATERIAL_ANISOTROPIC_SAMPLER_FLAGS = BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;

/// The longest step between two frames graph materials animate by (get_material_graph_times).
constexpr float MAX_MATERIAL_FRAME_SECONDS = 0.1f;

/// The variant of a graph material's vertex shader that draws the velocity of @p vertex_variant's draws.
constexpr const char* VELOCITY_VARIANT = "velocity";

/// The components of a vec4 register: the normal map layouts of four textures (MATERIAL_GRAPH_NORMAL_CHANNELS_UNIFORM).
constexpr size_t REGISTER_COMPONENTS = 4;

/// The shader object of @p shader once it is loaded and not the empty stand-in; never waits for it, as a draw must not.
auto get_loaded_shader(const asset_handle<gfx::shader>& shader) -> std::shared_ptr<gfx::shader>
{
    auto loaded = shader ? shader.try_get() : nullptr;
    return loaded && !asset_handle<gfx::shader>::is_empty(loaded) ? loaded : nullptr;
}

/// The vertex shader of @p mat's draws with @p vertex_variant: its own where it has a vertex stage, it is loaded and has
/// the variant, else none.
auto find_own_vertex_shader(const graph_material& mat, const std::string& vertex_variant) -> std::shared_ptr<gfx::shader>
{
    auto loaded = mat.has_vertex_stage() ? get_loaded_shader(mat.get_vertex_shader()) : nullptr;
    return loaded && loaded->find_variant(vertex_variant) ? loaded : nullptr;
}

/// The times of render frame @p frame (get_material_graph_times), taken from the clock the first time a thread asks
/// for that frame.
auto read_material_graph_times(uint32_t frame) -> math::vec2
{
    static const auto start = std::chrono::steady_clock::now();
    static std::mutex mutex;
    static uint32_t read_frame{};
    static bool has_read{};
    static math::vec2 times{};
    std::lock_guard<std::mutex> lock(mutex);
    if(!has_read || frame != read_frame)
    {
        // Frame numbers can skip (readbacks submit frames of their own): the frame before is the last that read.
        const std::chrono::duration<float> seconds = std::chrono::steady_clock::now() - start;
        const float now = seconds.count();
        times = math::vec2(now, has_read ? std::max(times.x, now - MAX_MATERIAL_FRAME_SECONDS) : now);
        read_frame = frame;
        has_read = true;
    }
    return times;
}
} // namespace

auto get_material_sampler_flags(const gfx::texture::ptr& texture, material_texture_address address, material_texture_filter filter)
    -> std::uint32_t
{
    auto flags = texture ? static_cast<std::uint32_t>(texture->flags & BGFX_SAMPLER_BITS_MASK) : 0u;
    if(address != material_texture_address::texture)
    {
        flags &= ~std::uint32_t(BGFX_SAMPLER_U_MASK | BGFX_SAMPLER_V_MASK | BGFX_SAMPLER_W_MASK);
        flags |= address == material_texture_address::clamp    ? std::uint32_t(BGFX_SAMPLER_UVW_CLAMP)
                 : address == material_texture_address::mirror ? std::uint32_t(BGFX_SAMPLER_UVW_MIRROR)
                                                                : 0u;
    }
    const bool is_point = (flags & (BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT)) != 0u || filter == material_texture_filter::point;
    if(is_point)
    {
        flags &= ~std::uint32_t(BGFX_SAMPLER_MIN_MASK | BGFX_SAMPLER_MAG_MASK | BGFX_SAMPLER_MIP_MASK);
        return flags | std::uint32_t(BGFX_SAMPLER_POINT);
    }
    return filter == material_texture_filter::anisotropic ? flags | MATERIAL_ANISOTROPIC_SAMPLER_FLAGS : flags;
}

namespace
{
/// Whether @p texture is of the kind a sampler declared as @p kind reads: a cube for a cube sampler, a 2D one else.
auto is_material_texture_kind(const gfx::texture& texture, material_value_type kind) -> bool
{
    return texture.info.cubeMap == (kind == material_value_type::texture_cube);
}
} // namespace

auto get_material_graph_times() -> math::vec2
{
    // Each thread keeps the frame it read last: the lock is taken once per frame and thread, not per draw.
    thread_local uint32_t thread_frame{};
    thread_local bool has_thread_read{};
    thread_local math::vec2 thread_times{};
    const uint32_t frame = gfx::get_render_frame();
    if(!has_thread_read || frame != thread_frame)
    {
        thread_times = read_material_graph_times(frame);
        thread_frame = frame;
        has_thread_read = true;
    }
    return thread_times;
}

auto get_material_graph_time() -> float
{
    return get_material_graph_times().x;
}

void graph_material_programs::init(asset_handle<gfx::shader> vertex_shader)
{
    vertex_shader_ = std::move(vertex_shader);
    samplers_.resize(MATERIAL_GRAPH_MAX_TEXTURES);
    for(uint32_t i = 0; i < MATERIAL_GRAPH_MAX_TEXTURES; ++i)
    {
        cache_uniform(nullptr, samplers_[i], get_material_graph_sampler_name(i), bgfx::UniformType::Sampler);
    }
    cache_uniform(nullptr,
                  parameters_,
                  MATERIAL_GRAPH_PARAMETERS_UNIFORM,
                  bgfx::UniformType::Vec4,
                  uint16_t(MATERIAL_GRAPH_MAX_PARAMETER_REGISTERS));
    cache_uniform(nullptr,
                  normal_channels_,
                  MATERIAL_GRAPH_NORMAL_CHANNELS_UNIFORM,
                  bgfx::UniformType::Vec4,
                  uint16_t(MATERIAL_GRAPH_NORMAL_CHANNEL_REGISTERS));
    cache_uniform(nullptr, time_, MATERIAL_GRAPH_TIME_UNIFORM, bgfx::UniformType::Vec4);
    cache_uniform(nullptr, camera_position_, "u_camera_wpos", bgfx::UniformType::Vec4);
}

auto graph_material_programs::begin(const graph_material& mat,
                                    const std::string& vertex_variant,
                                    const std::string& fragment_variant) -> gpu_program*
{
    const auto& shader = mat.get_shader();
    if(!shader || vertex_variant.empty())
    {
        return nullptr;
    }
    const auto shader_object = get_loaded_shader(shader);
    if(!shader_object || !shader_object->find_variant(fragment_variant))
    {
        return nullptr;
    }
    const auto own_shader = find_own_vertex_shader(mat, vertex_variant);
    // Pixels reading what only the graph's vertex shader writes cannot pair with the renderer's.
    if(!own_shader && mat.reads_vertex_outputs())
    {
        return nullptr;
    }
    const auto& vertex_shader = own_shader ? mat.get_vertex_shader() : vertex_shader_;
    const program_key_view key{shader_object.get(), own_shader.get(), shader_object.get(), vertex_variant, fragment_variant};
    return begin_entry(key, vertex_shader, shader);
}

auto graph_material_programs::begin_moved_vertices(const material& mat,
                                                   const std::string& vertex_variant,
                                                   const asset_handle<gfx::shader>& fragment_shader,
                                                   const math::vec3& camera_position) -> gpu_program*
{
    const auto* graph = mat.safe_cast<graph_material>();
    const bool moves_vertices = graph != nullptr && graph->uses_world_position_offset();
    const auto offset_shader = moves_vertices ? find_own_vertex_shader(*graph, vertex_variant) : nullptr;
    const auto shader_object = graph != nullptr ? get_loaded_shader(graph->get_shader()) : nullptr;
    const auto fragment_object = get_loaded_shader(fragment_shader);
    if(!offset_shader || !shader_object || !fragment_object)
    {
        return nullptr;
    }
    const program_key_view key{shader_object.get(), offset_shader.get(), fragment_object.get(), vertex_variant, gfx::shader::DEFAULT_VARIANT};
    auto* program = begin_entry(key, graph->get_vertex_shader(), fragment_shader);
    if(program != nullptr)
    {
        submit_values(*graph);
        submit_camera_position(camera_position);
    }
    return program;
}

auto graph_material_programs::get_position_only_variant(bool is_skinned) -> const char*
{
    return is_skinned ? "position_only_skinned" : "position_only";
}

auto graph_material_programs::get_velocity_variant(const std::string& vertex_variant) -> std::string
{
    const bool is_default = vertex_variant.empty() || vertex_variant == gfx::shader::DEFAULT_VARIANT;
    return is_default ? std::string(VELOCITY_VARIANT) : std::string(VELOCITY_VARIANT) + "_" + vertex_variant;
}

auto graph_material_programs::begin_entry(const program_key_view& key,
                                          const asset_handle<gfx::shader>& vertex_shader,
                                          const asset_handle<gfx::shader>& fragment_shader) -> gpu_program*
{
    auto found = programs_.find(key);
    if(found == programs_.end())
    {
        program_key owned{key.material_shader,
                          key.vertex_shader,
                          key.fragment_shader,
                          std::string(key.vertex_variant),
                          std::string(key.fragment_variant)};
        program_entry entry;
        entry.program = std::make_unique<gpu_program>(vertex_shader, owned.vertex_variant, fragment_shader, owned.fragment_variant);
        found = programs_.emplace(std::move(owned), std::move(entry)).first;
    }
    found->second.last_used_frame = gfx::get_render_frame();
    return found->second.program->begin() ? found->second.program.get() : nullptr;
}

void graph_material_programs::submit_values(const graph_material& mat) const
{
    submit_values(mat.get_parameter_registers(), mat.get_textures());
}

void graph_material_programs::submit_values(const std::vector<math::vec4>& registers,
                                            const std::vector<material_graph_texture>& textures) const
{
    std::array<math::vec4, MATERIAL_GRAPH_NORMAL_CHANNEL_REGISTERS> normal_channels{};
    bool binds_normal_map = false;
    for(size_t i = 0; i < textures.size() && i < samplers_.size(); ++i)
    {
        const auto& texture = textures[i];
        // A texture still loading reads the default, as one the node names none: the draw never waits for it.
        auto loaded = texture.texture ? texture.texture.try_get() : nullptr;
        // A texture of the other kind than the sampler's (a 2D one picked for a cube) reads the default too.
        if(!loaded || asset_handle<gfx::texture>::is_empty(loaded) || !is_material_texture_kind(*loaded, texture.kind))
        {
            if(texture.kind == material_value_type::texture_cube)
            {
                loaded = default_textures::get().black_cube_texture();
            }
            else
            {
                loaded = (texture.is_normal_map ? material::default_normal_map() : material::default_color_map()).get();
            }
        }
        gfx::set_texture(samplers_[i], uint8_t(texture.stage), loaded, get_material_sampler_flags(loaded, texture.address, texture.filter));
        if(texture.is_normal_map && loaded)
        {
            normal_channels[i / REGISTER_COMPONENTS][int(i % REGISTER_COMPONENTS)] = float(loaded->get_normal_map_channels());
            binds_normal_map = true;
        }
    }
    if(binds_normal_map)
    {
        gfx::set_uniform(normal_channels_, normal_channels.data(), uint16_t(normal_channels.size()));
    }
    if(!registers.empty())
    {
        gfx::set_uniform(parameters_, registers.data(), uint16_t(registers.size()));
    }
    const auto times = get_material_graph_times();
    gfx::set_uniform(time_, math::vec4(times.x, times.y, 0.0f, 0.0f));
}

void graph_material_programs::submit_camera_position(const math::vec3& position) const
{
    gfx::set_uniform(camera_position_, math::vec4(position, 0.0f));
}

void graph_material_programs::release_unused()
{
    const uint32_t frame = gfx::get_render_frame();
    std::erase_if(programs_,
                  [frame](const auto& entry)
                  {
                      return frame - entry.second.last_used_frame > PROGRAM_IDLE_FRAMES;
                  });
}

} // namespace unravel
