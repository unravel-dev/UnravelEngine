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
    // Every mesh carries a vertex colour and a second UV set: white and a copy of the first set when the source has
    // none, since bgfx gives a shader no default for an attribute the bound layout lacks.
    decl.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Uint8, true, true)
        .add(bgfx::Attrib::Tangent, 3, bgfx::AttribType::Uint8, true, true)
        .add(bgfx::Attrib::Bitangent, 3, bgfx::AttribType::Uint8, true, true)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
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
