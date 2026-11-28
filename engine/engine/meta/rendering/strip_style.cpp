#include "strip_style.hpp"

#include <engine/meta/core/common/basetypes.hpp>
#include <engine/meta/core/math/vector.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT(strip_style)
{
    entt::meta_factory<strip_shape>{}
        .type("strip_shape"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "strip_shape"},
            entt::attribute{"pretty_name", "Strip Shape"},
        })
        .data<strip_shape::flat>("flat"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "flat"},
            entt::attribute{"pretty_name", "Flat"},
        })
        .data<strip_shape::rounded>("rounded"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "rounded"},
            entt::attribute{"pretty_name", "Rounded"},
        })
        .data<strip_shape::tube>("tube"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "tube"},
            entt::attribute{"pretty_name", "Tube"},
        });

    entt::meta_factory<strip_alignment>{}
        .type("strip_alignment"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "strip_alignment"},
            entt::attribute{"pretty_name", "Strip Alignment"},
        })
        .data<strip_alignment::camera>("camera"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "camera"},
            entt::attribute{"pretty_name", "Camera"},
        })
        .data<strip_alignment::local_axis>("local_axis"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "local_axis"},
            entt::attribute{"pretty_name", "Local Axis"},
        })
        .data<strip_alignment::world_axis>("world_axis"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "world_axis"},
            entt::attribute{"pretty_name", "World Axis"},
        });

    entt::meta_factory<strip_uv_mode>{}
        .type("strip_uv_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "strip_uv_mode"},
            entt::attribute{"pretty_name", "Strip UV Mode"},
        })
        .data<strip_uv_mode::stretch>("stretch"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "stretch"},
            entt::attribute{"pretty_name", "Stretch"},
        })
        .data<strip_uv_mode::tile>("tile"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "tile"},
            entt::attribute{"pretty_name", "Tile"},
        })
        .data<strip_uv_mode::per_segment>("per_segment"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "per_segment"},
            entt::attribute{"pretty_name", "Per Segment"},
        });

    // A setting shows only where it applies.
    const auto when = [](auto test)
    {
        return entt::property_predicate<bool>(
            [test](const entt::meta_any& object)
            {
                const auto* style = object.try_cast<strip_style>();
                return style != nullptr && test(*style);
            });
    };
    const auto is_band = when([](const strip_style& s) { return s.shape != strip_shape::tube; });
    const auto is_tube = when([](const strip_style& s) { return s.shape == strip_shape::tube; });
    const auto has_axis = when([](const strip_style& s)
                               { return s.shape != strip_shape::tube && s.alignment != strip_alignment::camera; });
    const auto is_tiled = when([](const strip_style& s) { return s.uv_mode == strip_uv_mode::tile; });

    entt::meta_factory<strip_style>{}
        .type("strip_style"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "strip_style"},
            entt::attribute{"pretty_name", "Strip"},
        })
        .data<&strip_style::shape>("shape"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "shape"},
            entt::attribute{"pretty_name", "Shape"},
            entt::attribute{"tooltip", "Flat: a band of two vertices a point. Rounded: a band whose normals lean out at its edges, so it shades as a cylinder (cables, beams). Tube: a ring of vertices around each point, right from every side and in its shadow (ropes, pipes)."},
        })
        .data<&strip_style::alignment>("alignment"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "alignment"},
            entt::attribute{"pretty_name", "Alignment"},
            entt::attribute{"tooltip", "What the band's face turns toward. Camera: the camera that draws it. Local Axis: a direction that turns with the entity. World Axis: a direction of the world, such as up for a path on the ground."},
            entt::attribute{"predicate", is_band},
        })
        .data<&strip_style::axis>("axis"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "axis"},
            entt::attribute{"pretty_name", "Axis"},
            entt::attribute{"tooltip", "The direction the band's face turns toward."},
            entt::attribute{"predicate", has_axis},
        })
        .data<&strip_style::tube_sides>("tube_sides"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "tube_sides"},
            entt::attribute{"pretty_name", "Tube Sides"},
            entt::attribute{"min", STRIP_MIN_TUBE_SIDES},
            entt::attribute{"max", STRIP_MAX_TUBE_SIDES},
            entt::attribute{"tooltip", "The sides of the tube's ring. More are rounder and cost more vertices."},
            entt::attribute{"predicate", is_tube},
        })
        .data<&strip_style::width>("width"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "width"},
            entt::attribute{"pretty_name", "Width"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "The strip's width in metres: a tube's diameter."},
        })
        .data<&strip_style::width_over_length>("width_over_length"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "width_over_length"},
            entt::attribute{"pretty_name", "Width Over Length"},
            entt::attribute{"tooltip", "Scales the Width along the strip, from its start on the left to its end on the right. It is read at every cross-section: add Subdivisions for a shape that changes between two points."},
        })
        .data<&strip_style::color_over_length>("color_over_length"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "color_over_length"},
            entt::attribute{"pretty_name", "Color Over Length"},
            entt::attribute{"tooltip", "The colour and opacity along the strip, written to its vertex colour and read at every cross-section. A material shows it through its Vertex Color node, as the engine's strip material does."},
        })
        .data<&strip_style::uv_mode>("uv_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "uv_mode"},
            entt::attribute{"pretty_name", "UV Mode"},
            entt::attribute{"tooltip", "How a texture runs along the strip. Stretch: once from start to end. Tile: one repeat every Tile Length metres, so it keeps its size as the strip grows, and on a trail it stays where it was laid. Per Segment: one repeat between each pair of points."},
        })
        .data<&strip_style::tile_length>("tile_length"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "tile_length"},
            entt::attribute{"pretty_name", "Tile Length"},
            entt::attribute{"min", STRIP_MIN_TILE_LENGTH},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "Metres of strip per repeat of the texture."},
            entt::attribute{"predicate", is_tiled},
        })
        .data<&strip_style::subdivisions>("subdivisions"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "subdivisions"},
            entt::attribute{"pretty_name", "Subdivisions"},
            entt::attribute{"min", 0u},
            entt::attribute{"max", STRIP_MAX_ADDED_VERTICES},
            entt::attribute{"tooltip", "Points added between each pair along a smooth curve through them. 0 keeps the segments straight."},
        })
        .data<&strip_style::corner_vertices>("corner_vertices"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "corner_vertices"},
            entt::attribute{"pretty_name", "Corner Vertices"},
            entt::attribute{"min", 0u},
            entt::attribute{"max", STRIP_MAX_ADDED_VERTICES},
            entt::attribute{"tooltip", "Cross-sections added around each sharp corner: its outer edge turns as an arc about the inner one. 0 leaves corners pointed."},
            entt::attribute{"predicate", is_band},
        })
        .data<&strip_style::cap_vertices>("cap_vertices"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cap_vertices"},
            entt::attribute{"pretty_name", "Cap Vertices"},
            entt::attribute{"min", 0u},
            entt::attribute{"max", STRIP_MAX_ADDED_VERTICES},
            entt::attribute{"tooltip", "Vertices of the half disc that rounds each end of a band. Any number closes the ends of a tube. 0 leaves the ends cut straight and open."},
        });
}

SAVE(strip_style)
{
    try_save(ar, ser20::make_nvp("shape", obj.shape));
    try_save(ar, ser20::make_nvp("alignment", obj.alignment));
    try_save(ar, ser20::make_nvp("axis", obj.axis));
    try_save(ar, ser20::make_nvp("tube_sides", obj.tube_sides));
    try_save(ar, ser20::make_nvp("width", obj.width));
    try_save(ar, ser20::make_nvp("width_over_length", obj.width_over_length));
    try_save(ar, ser20::make_nvp("color_over_length", obj.color_over_length));
    try_save(ar, ser20::make_nvp("uv_mode", obj.uv_mode));
    try_save(ar, ser20::make_nvp("tile_length", obj.tile_length));
    try_save(ar, ser20::make_nvp("subdivisions", obj.subdivisions));
    try_save(ar, ser20::make_nvp("corner_vertices", obj.corner_vertices));
    try_save(ar, ser20::make_nvp("cap_vertices", obj.cap_vertices));
}
SAVE_INSTANTIATE(strip_style, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(strip_style, ser20::oarchive_binary_t);

LOAD(strip_style)
{
    try_load(ar, ser20::make_nvp("shape", obj.shape));
    try_load(ar, ser20::make_nvp("alignment", obj.alignment));
    try_load(ar, ser20::make_nvp("axis", obj.axis));
    try_load(ar, ser20::make_nvp("tube_sides", obj.tube_sides));
    try_load(ar, ser20::make_nvp("width", obj.width));
    try_load(ar, ser20::make_nvp("width_over_length", obj.width_over_length));
    try_load(ar, ser20::make_nvp("color_over_length", obj.color_over_length));
    try_load(ar, ser20::make_nvp("uv_mode", obj.uv_mode));
    try_load(ar, ser20::make_nvp("tile_length", obj.tile_length));
    try_load(ar, ser20::make_nvp("subdivisions", obj.subdivisions));
    try_load(ar, ser20::make_nvp("corner_vertices", obj.corner_vertices));
    try_load(ar, ser20::make_nvp("cap_vertices", obj.cap_vertices));
}
LOAD_INSTANTIATE(strip_style, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(strip_style, ser20::iarchive_binary_t);

} // namespace unravel
