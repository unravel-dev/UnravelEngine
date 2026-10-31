#pragma once

#include "uniform.h"
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace gfx
{
struct shader : public handle_impl<shader, bgfx::ShaderHandle>
{
    /// The variant a shader is when nothing names one.
    static constexpr const char* DEFAULT_VARIANT = "default";

    shader() = default;
    shader(const bgfx::Memory* _mem);
    shader(const bgfx::EmbeddedShader* _es, const char* name);
    shader(handle_type_t hndl);

    /**
     * @brief The shader to draw with for variant @p name: DEFAULT_VARIANT is this shader itself. Null when the source
     *        declares no such variant.
     */
    auto find_variant(const std::string& name) const -> const shader*;

    /// Uniforms for this shader
    std::vector<std::shared_ptr<uniform>> uniforms;

    /// The named variants of the source, without the default, which is this shader.
    std::unordered_map<std::string, std::shared_ptr<shader>> variants;

    /// The varyings the source declares, each name with its type, after its interpolation if it has one: "flat vec4".
    std::map<std::string, std::string> varyings;

private:
    void populate_uniforms();
};
} // namespace gfx
