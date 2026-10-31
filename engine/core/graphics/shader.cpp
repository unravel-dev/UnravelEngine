#include "shader.h"

namespace gfx
{
shader::shader(const bgfx::Memory* mem) : shader(bgfx::createShader(mem))
{
}

shader::shader(const bgfx::EmbeddedShader* es, const char* name)
    : shader(bgfx::createEmbeddedShader(es, bgfx::getRendererType(), name))
{
}

shader::shader(handle_type_t hndl)
{
    handle_ = hndl;

    populate_uniforms();
}

auto shader::find_variant(const std::string& name) const -> const shader*
{
    if(name == DEFAULT_VARIANT)
    {
        return this;
    }
    const auto it = variants.find(name);
    return it != variants.end() ? it->second.get() : nullptr;
}

void shader::populate_uniforms()
{
    auto uniform_count = bgfx::getShaderUniforms(handle_);
    if(uniform_count > 0)
    {
        std::vector<uniform::handle_type_t> uniforms_handles(uniform_count);
        bgfx::getShaderUniforms(handle_, &uniforms_handles[0], uniform_count);
        uniforms.reserve(uniform_count);
        for(auto& uni : uniforms_handles)
        {
            auto uniform_var = std::make_shared<uniform>(uni);
            uniforms.emplace_back(uniform_var);
        }
    }
}

} // namespace gfx
