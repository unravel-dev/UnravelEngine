#include "gpu_program.h"
#include <algorithm>
#include <logging/logging.h>

namespace unravel
{
namespace
{
/// The names @p shader answers to, sorted, for an error message.
auto list_variant_names(const gfx::shader& shader) -> std::string
{
    std::vector<std::string> names = {gfx::shader::DEFAULT_VARIANT};
    for(const auto& variant : shader.variants)
    {
        names.push_back(variant.first);
    }
    std::sort(names.begin(), names.end());
    return fmt::format("{}", fmt::join(names, ", "));
}
} // namespace

auto describe_varying_mismatches(const gfx::shader& vertex, const gfx::shader& fragment) -> std::string
{
    std::vector<std::string> mismatches;
    for(const auto& [name, declaration] : fragment.varyings)
    {
        const auto written = vertex.varyings.find(name);
        if(written != vertex.varyings.end() && written->second != declaration)
        {
            mismatches.push_back(fmt::format("{} is {} in the vertex shader and {} in the fragment shader", name, written->second, declaration));
        }
    }
    return fmt::format("{}", fmt::join(mismatches, ", "));
}

gpu_program::gpu_program(asset_handle<gfx::shader> compute_shader, std::string variant)
{
    attach_shader(std::move(compute_shader), std::move(variant));
    populate();
}

gpu_program::gpu_program(asset_handle<gfx::shader> vertex_shader, asset_handle<gfx::shader> fragment_shader)
{
    attach_shader(std::move(vertex_shader));
    attach_shader(std::move(fragment_shader));
    populate();
}

gpu_program::gpu_program(asset_handle<gfx::shader> vertex_shader,
                         std::string vertex_variant,
                         asset_handle<gfx::shader> fragment_shader,
                         std::string fragment_variant)
{
    attach_shader(std::move(vertex_shader), std::move(vertex_variant));
    attach_shader(std::move(fragment_shader), std::move(fragment_variant));
    populate();
}

void gpu_program::attach_shader(asset_handle<gfx::shader> shader, std::string variant)
{
    variants_.emplace_back(std::move(variant));
    shaders_.emplace_back(std::move(shader));
}

auto gpu_program::find_refusal(const shader_assets& assets, const selected_shaders& selected) const -> std::string
{
    for(std::size_t i = 0; i < shaders_.size(); ++i)
    {
        if(selected[i] == nullptr && !asset_handle<gfx::shader>::is_empty(assets[i]))
        {
            return fmt::format("Shader {0} has no variant named {1}. It has: {2}.", shaders_[i].id(), variants_[i], list_variant_names(*assets[i]));
        }
    }
    if(shaders_.size() != 2 || selected[0] == nullptr || selected[1] == nullptr)
    {
        return {};
    }
    // bgfx checks only the names, and D3D12 does not survive one name with two types: the pipeline state fails to create.
    const auto mismatches = describe_varying_mismatches(*selected[0], *selected[1]);
    if(mismatches.empty())
    {
        return {};
    }
    return fmt::format("Shaders {0} and {1} cannot make a program, since both stages must declare a varying the same way: {2}.",
                       describe_shader(0),
                       describe_shader(1),
                       mismatches);
}

auto gpu_program::describe_shader(std::size_t index) const -> std::string
{
    return variants_[index] == gfx::shader::DEFAULT_VARIANT ? shaders_[index].id() : shaders_[index].id() + "#" + variants_[index];
}

void gpu_program::create_program(const selected_shaders& selected)
{
    // An empty program first: the old one goes before bgfx is asked for its replacement.
    program_ = std::make_shared<gfx::program>();
    ++revision_;
    if(shaders_.size() == 1)
    {
        program_ = std::make_shared<gfx::program>(*selected[0]);
    }
    else if(shaders_.size() == 2)
    {
        program_ = std::make_shared<gfx::program>(*selected[0], *selected[1]);
        if(!program_->is_valid())
        {
            // bgfx only traces this in debug builds, and a pass whose program is invalid silently draws nothing.
            APPLOG_ERROR("Shaders {0} and {1} do not make a valid program. The vertex shader's outputs and the fragment "
                         "shader's inputs must list the same names, and a sampler both declare must have one dimension.",
                         describe_shader(0),
                         describe_shader(1));
        }
    }
}

void gpu_program::populate()
{
    // The assets are held for the whole rebuild: a reload on another thread can replace them, and the variants live in them.
    shader_assets assets{nullptr, nullptr};
    selected_shaders selected{nullptr, nullptr};
    for(std::size_t i = 0; i < shaders_.size(); ++i)
    {
        if(!shaders_[i])
        {
            return;
        }
        assets[i] = shaders_[i].get();
        selected[i] = assets[i]->find_variant(variants_[i]);
    }
    const auto refusal = find_refusal(assets, selected);
    if(!refusal.empty())
    {
        APPLOG_ERROR("{0}", refusal);
        // The cached assets keep begin() from trying again, and logging again, until a reload changes one of them.
        program_ = std::make_shared<gfx::program>();
        ++revision_;
        shaders_cached_ = assets;
        return;
    }
    // A shader that failed to load: begin() tries again every frame, until a reload brings it.
    const auto is_drawable = [](const gfx::shader* shader)
    {
        return shader != nullptr && shader->is_valid();
    };
    if(!std::all_of(selected.begin(), selected.begin() + shaders_.size(), is_drawable))
    {
        return;
    }
    create_program(selected);
    shaders_cached_ = assets;
}

void gpu_program::set_texture(uint8_t stage,
                              const hpp::string_view& sampler,
                              const gfx::frame_buffer* fbo,
                              uint8_t attachment,
                              uint32_t flags)
{
    program_->set_texture(stage, sampler, fbo, attachment, flags);
}

void gpu_program::set_texture(uint8_t stage,
                              const hpp::string_view& sampler,
                              const gfx::texture* texture,
                              uint32_t flags)
{
    program_->set_texture(stage, sampler, texture, flags);
}

void gpu_program::set_uniform(const hpp::string_view& name, const void* value, uint16_t num)
{
    program_->set_uniform(name, value, num);
}

void gpu_program::set_uniform(const hpp::string_view& name, const math::vec4& value, uint16_t num)
{
    set_uniform(name, math::value_ptr(value), num);
}

void gpu_program::set_uniform(const hpp::string_view& name, const math::vec3& value, uint16_t num)
{
    set_uniform(name, math::vec4(value, 0.0f), num);
}

void gpu_program::set_uniform(const hpp::string_view& name, const math::vec2& value, uint16_t num)
{
    set_uniform(name, math::vec4(value, 0.0f, 0.0f), num);
}

auto gpu_program::get_uniform(const hpp::string_view& name) -> gfx::program::uniform_ptr
{
    return program_->get_uniform(name);
}

auto gpu_program::native_handle() const -> gfx::program::handle_type_t
{
    return program_->native_handle();
}

auto gpu_program::get_shaders() const -> const std::vector<asset_handle<gfx::shader>>&
{
    return shaders_;
}

auto gpu_program::is_valid() const -> bool
{
    return program_->is_valid();
}

auto gpu_program::begin() -> bool
{
    for(std::size_t i = 0; i < shaders_.size(); ++i)
    {
        if(shaders_[i] && shaders_cached_[i] != shaders_[i].get())
        {
            populate();
            break;
        }
    }
    return is_valid();
}

auto gpu_program::get_revision() const -> std::uint32_t
{
    return revision_;
}

void gpu_program::end()
{
}


} // namespace unravel

namespace gfx
{

void set_world_transform(const std::vector<math::transform::mat4_t>& matrices)
{
    if(matrices.empty())
    {
        return;
    }

    gfx::set_world_transform(matrices.data(), static_cast<std::uint16_t>(matrices.size()));
}

void set_world_transform(const std::vector<math::transform>& matrices)
{
    if(matrices.empty())
    {
        return;
    }

    using mat_type = math::transform::mat4_t;
    std::vector<mat_type> mats;
    mats.reserve(matrices.size());
    for(const auto& m : matrices)
    {
        mats.emplace_back(m.get_matrix());
    }

    set_world_transform(mats);
}

void set_world_transform(const math::transform::mat4_t& matrix)
{
    auto mat4 = (const void*)math::value_ptr(matrix);
    gfx::set_world_transform(mat4);
}

void set_world_transform(const math::transform& matrix)
{
    set_world_transform(matrix.get_matrix());
}

void set_prev_world_transform(const std::vector<math::transform::mat4_t>& matrices)
{
    if(matrices.empty())
    {
        return;
    }

    gfx::set_prev_world_transform(matrices.data(), static_cast<std::uint16_t>(matrices.size()));
}

void set_prev_world_transform(const math::transform::mat4_t& matrix)
{
    auto mat4 = (const void*)math::value_ptr(matrix);
    gfx::set_prev_world_transform(mat4);
}

void set_transform(const std::vector<math::transform::mat4_t>& matrices)
{
    if(matrices.empty())
    {
        return;
    }

    bgfx::setTransform(matrices.data(), static_cast<std::uint16_t>(matrices.size()));
}

void set_transform(const std::vector<math::transform>& matrices)
{
    if(matrices.empty())
    {
        return;
    }

    using mat_type = math::transform::mat4_t;
    std::vector<mat_type> mats;
    mats.reserve(matrices.size());
    for(const auto& m : matrices)
    {
        mats.emplace_back(m.get_matrix());
    }

    set_transform(mats);
}

void set_transform(const math::transform::mat4_t& matrix)
{
    auto mat4 = (const void*)math::value_ptr(matrix);
    bgfx::setTransform(mat4);
}

void set_transform(const math::transform& matrix)
{
    set_transform(matrix.get_matrix());
}

void set_texture(const gfx::program::uniform_ptr& uniform,
                 std::uint8_t stage,
                 const gfx::frame_buffer::ptr& handle,
                 uint8_t attachment,
                 std::uint32_t flags)
{
    set_texture(uniform, stage, handle.get(), attachment, flags);
}

void set_texture(const gfx::program::uniform_ptr& uniform,
                 std::uint8_t stage,
                 const gfx::texture::ptr& texture,
                 std::uint32_t flags)
{
    set_texture(uniform, stage, texture.get(), flags);
}

void set_texture(const gfx::program::uniform_ptr& uniform,
                 std::uint8_t stage,
                 const gfx::frame_buffer* handle,
                 uint8_t attachment,
                 std::uint32_t flags)
{
    if(!uniform)
    {
        return;
    }

    uniform->set_texture(stage, handle, attachment, flags);
}

void set_texture(const gfx::program::uniform_ptr& uniform,
                 std::uint8_t stage,
                 const gfx::texture* texture,
                 std::uint32_t flags)
{
    if(!uniform)
    {
        return;
    }

    uniform->set_texture(stage, texture, flags);
}

void set_texture(const gfx::program::uniform_ptr& uniform,
                 std::uint8_t stage,
                 const asset_handle<gfx::texture>& texture,
                 std::uint32_t flags)
{
    set_texture(uniform, stage, texture.get(), flags);
}

void set_uniform(const gfx::program::uniform_ptr& uniform, const void* value, std::uint16_t num)
{
    if(!uniform)
    {
        return;
    }

    uniform->set_uniform(value, num);
}
void set_uniform(const gfx::program::uniform_ptr& uniform, const math::mat4& value, std::uint16_t num)
{
    set_uniform(uniform, math::value_ptr(value), num);
}
void set_uniform(const gfx::program::uniform_ptr& uniform, const math::vec4& value, std::uint16_t num)
{
    set_uniform(uniform, math::value_ptr(value), num);
}
void set_uniform(const gfx::program::uniform_ptr& uniform, const math::vec3& value, std::uint16_t num)
{
    set_uniform(uniform, math::vec4(value, 0.0f), num);
}
void set_uniform(const gfx::program::uniform_ptr& uniform, const math::vec2& value, std::uint16_t num)
{
    set_uniform(uniform, math::vec4(value, 0.0f, 0.0f), num);
}
} // namespace gfx
