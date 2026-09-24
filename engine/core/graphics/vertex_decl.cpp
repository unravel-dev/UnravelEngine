#include "vertex_decl.h"

namespace gfx
{

void screen_pos_vertex::init(bgfx::VertexLayout& decl)
{
    decl.begin().add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float).end();
}

void pos_vertex::init(bgfx::VertexLayout& decl)
{
    decl.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
}

void pos_texcoord0_vertex::init(bgfx::VertexLayout& decl)
{
    decl.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
}

void mesh_vertex::init(bgfx::VertexLayout& decl)
{
    decl.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        // this is for barycentric coords eventually
        //.add(attribute::Color1, 4, attribute_type::Uint8, true)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Uint8, true, true)
        .add(bgfx::Attrib::Tangent, 3, bgfx::AttribType::Uint8, true, true)
        .add(bgfx::Attrib::Bitangent, 3, bgfx::AttribType::Uint8, true, true)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
}

void pos_texcoord0_color0_vertex::init(bgfx::VertexLayout& decl)
{
    decl.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
}
} // namespace gfx
