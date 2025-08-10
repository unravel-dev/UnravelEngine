#include "gpu_program.h"
#include <algorithm>

namespace unravel
{
gpu_program::gpu_program(asset_handle<gfx::shader> compute_shader)
{
    attach_shader(std::move(compute_shader));
    populate();
}

gpu_program::gpu_program(asset_handle<gfx::shader> vertex_shader, asset_handle<gfx::shader> fragment_shader)
{
    attach_shader(std::move(vertex_shader));
    attach_shader(std::move(fragment_shader));
    populate();
}

void gpu_program::attach_shader(asset_handle<gfx::shader> shader)
{
    if(!shader)
    {
        shaders_cached_.push_back(nullptr);
        shaders_.push_back(shader);
        return;
    }

    shaders_cached_.push_back(nullptr);
    shaders_.emplace_back(std::move(shader));
}

void gpu_program::populate()
{
    bool all_valid = std::all_of(std::begin(shaders_),
                                 std::end(shaders_),
                                 [](auto& shader)
                                 {
                                     return shader && shader.get()->is_valid();
                                 });

    if(all_valid)
    {
        if(shaders_.size() == 1)
        {
            const auto& compute_shader = shaders_[0];
            program_ = std::make_shared<gfx::program>(*compute_shader.get());
        }
        else if(shaders_.size() == 2)
        {
            const auto& vertex_shader = shaders_[0];
            const auto& fragment_shader = shaders_[1];
            program_ = std::make_shared<gfx::program>(*vertex_shader.get(), *fragment_shader.get());
        }

        shaders_cached_.clear();
        for(const auto& shader : shaders_)
        {
            shaders_cached_.push_back(shader.get());
        }
    }
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
    return program_ && program_->is_valid();
}

auto gpu_program::begin() -> bool
{
    bool repopulate = false;
    for(std::size_t i = 0; i < shaders_cached_.size(); ++i)
    {
        auto shader_ptr = shaders_[i];
        if(!shader_ptr)
        {
            continue;
        }

        if(shaders_cached_[i] != shader_ptr.get())
        {
            repopulate = true;
            break;
        }
    }

    if(repopulate)
    {
        populate();
    }

    return is_valid();
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

void set_transform(const std::vector<math::transform::mat4_t>& matrices)
{
    if(matrices.empty())
    {
        return;
    }

    gfx::set_transform(matrices.data(), static_cast<std::uint16_t>(matrices.size()));
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
    gfx::set_transform(mat4);
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
