#pragma once

#include <bgfx/bgfx.h>

namespace gfx
{

template<typename T>
struct vertex
{
    static auto get_layout() -> const bgfx::VertexLayout&
    {
        static bgfx::VertexLayout s_decl = []()
        {
            bgfx::VertexLayout decl;
            T::init(decl);
            return decl;
        }();
        return s_decl;
    }
};

struct screen_pos_vertex : vertex<screen_pos_vertex>
{
    float x = 0.0f;
    float y = 0.0f;

    static void init(bgfx::VertexLayout& decl);
};

struct pos_vertex : vertex<pos_vertex>
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    static void init(bgfx::VertexLayout& decl);
};

struct pos_texcoord0_vertex : vertex<pos_texcoord0_vertex>
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    float u = 0.0f;
    float v = 0.0f;

    static void init(bgfx::VertexLayout& decl);
};

struct mesh_vertex : vertex<mesh_vertex>
{
    static void init(bgfx::VertexLayout& decl);
};

struct pos_texcoord0_color0_vertex : vertex<pos_texcoord0_color0_vertex>
{
    static void init(bgfx::VertexLayout& decl);
};
} // namespace gfx
