#pragma once

#include <bgfx/bgfx.h>

/**
 * @file gpu_resource_utils.h
 * @brief Small bgfx resource helpers shared by the passes and systems that own raw handles.
 */

namespace unravel::gpu_resource
{

/// The layout of a buffer the shaders read as a flat array of vec4 (BUFFER_RO(_, vec4, _)).
inline auto get_vec4_layout() -> const bgfx::VertexLayout&
{
    static const bgfx::VertexLayout layout = []()
    {
        bgfx::VertexLayout decl;
        decl.begin().add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float).end();
        return decl;
    }();
    return layout;
}

/// Destroys a bgfx handle when it is valid and invalidates it.
template<typename Handle>
void destroy_handle(Handle& handle)
{
    if(bgfx::isValid(handle))
    {
        bgfx::destroy(handle);
        handle = {bgfx::kInvalidHandle};
    }
}

} // namespace unravel::gpu_resource
