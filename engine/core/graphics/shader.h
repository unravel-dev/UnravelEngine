#pragma once

#include "uniform.h"
#include <memory>
#include <vector>

namespace gfx
{
struct shader : public handle_impl<shader, bgfx::ShaderHandle>
{
    shader() = default;
    shader(const std::string& path);
    shader(const bgfx::Memory* _mem);
    shader(const bgfx::EmbeddedShader* _es, const char* name);
    shader(handle_type_t hndl);

    /// Uniforms for this shader
    std::vector<std::shared_ptr<uniform>> uniforms;

private:
    void populate_uniforms();
};
} // namespace gfx
