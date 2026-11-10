#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

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

/// How much a GPU resource grows by when its content outgrows it.
constexpr uint32_t CAPACITY_GROWTH_FACTOR = 2;

/**
 * @brief The capacity a GPU resource that holds @p current elements takes to hold @p required: at least that many,
 * at least CAPACITY_GROWTH_FACTOR times @p current, so content that grows a little every frame does not create the
 * resource every frame, and never below @p minimum.
 */
constexpr auto grow_capacity(uint32_t current, uint32_t required, uint32_t minimum) -> uint32_t
{
    const uint32_t grown = current * CAPACITY_GROWTH_FACTOR;
    const uint32_t larger = required > grown ? required : grown;
    return larger > minimum ? larger : minimum;
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
