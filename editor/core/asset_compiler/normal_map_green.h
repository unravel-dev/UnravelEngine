#pragma once

#include <engine/assets/asset_storage.h>
#include <filesystem/filesystem.h>
#include <graphics/format.h>
#include <hpp/optional.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bimg
{
struct ImageContainer;
}

namespace unravel::asset_compiler
{

/// Which way the green channel of a tangent-space normal map points.
enum class normal_green : std::uint8_t
{
    /// The map does not tell: it is flat, small, or its texels disagree.
    unknown,
    /// Up the image, as the engine's shaders read a normal map (the OpenGL and glTF convention).
    up,
    /// Down the image (the DirectX convention): the compile turns such a map over.
    down,
};

/// The direction a map of `direction` has once its Y is turned over; unknown stays unknown.
auto invert_normal_green(normal_green direction) -> normal_green;

/// What a map's texels say about its green channel.
struct normal_green_reading
{
    normal_green direction{normal_green::unknown};
    /// How well the map's two cross slopes agree, from -1 to 1: positive for green up, negative for green down.
    float score{};
    /// Whether the map has slopes at all. A flat map has none, and compiles to the same texels either way.
    bool has_detail{false};
};

/**
 * @brief The parts of a map a model draws: a grid over the map's UV square, with a cell marked where a vertex of
 * the model has its UV. A map read with it is judged by the texels in marked cells alone, so what a baker painted
 * around the UV islands, which is no surface, does not count. A cell is 1/128 of the map's side: padding closer
 * than that to an island's edge is still read.
 */
class normal_map_coverage
{
public:
    /// Cells along each side of the grid.
    static constexpr std::uint32_t SIDE = 128;

    normal_map_coverage();

    /// Marks the cell of a vertex whose UV is (`u`, `v`), `v` growing down the image. A UV outside the UV square
    /// marks nothing and is counted: where vertices lie outside it, the map repeats over the model.
    void add_vertex(float u, float v);

    auto is_marked(std::uint32_t column, std::uint32_t row) const -> bool;

    /// The share of the grid's cells that are marked, from 0 to 1.
    auto get_marked_share() const -> float;

    /**
     * @brief Whether the grid tells which texels the model draws: nearly all of its vertices lie in the UV square
     * and they mark enough of the grid. A map that repeats over the model has no texels it leaves out, and a map
     * drawn by a few large triangles marks too little to tell them, so both are read whole.
     */
    auto is_usable() const -> bool;

private:
    std::vector<std::uint8_t> cells_;
    std::size_t marked_{};
    std::size_t vertices_{};
    std::size_t outside_{};
};

/**
 * @brief Reads which way a normal map's green channel points from its texels.
 *
 * A normal map made from a surface holds that surface's slopes, and the slopes of a surface have no curl: the X
 * slope changes down the image exactly as the Y slope changes across it. Read with green the wrong way, the two
 * changes are opposite. The sign of their correlation is the direction. The reading is unknown unless the
 * correlation is clear and has one sign from fine to coarse detail and in most regions of the map, so a flat map,
 * stripes that run one way, a map under about 256 texels a side and a map put together from sources of both
 * conventions read as unknown.
 *
 * The test tells an odd number of flipped axes from an even one, not which axis: a map with X flipped reads as
 * green down.
 *
 * @param rgba 8-bit RGBA texels, rows from the top of the image, `pitch` bytes apart.
 * @param channels Where the map keeps X and Y. For `xyz` and `xy` they are red and green, or alpha and green when
 * red is flat and alpha is not (the layout older tools gave BC3 normal maps).
 * @param coverage The parts of the map a model draws; null, or one that is not usable, reads the whole map.
 */
auto read_normal_green(const std::uint8_t* rgba,
                       std::uint32_t width,
                       std::uint32_t height,
                       std::uint32_t pitch,
                       gfx::normal_map_channels channels = gfx::normal_map_channels::xyz,
                       const normal_map_coverage* coverage = nullptr) -> normal_green_reading;

/**
 * @brief read_normal_green for an image of any format bimg decodes: its largest mip of at most 2048 texels a side,
 * or its smallest mip scaled down to that. Unknown for a cube map and a volume.
 */
auto read_normal_green(const bimg::ImageContainer& image,
                       gfx::normal_map_channels channels = gfx::normal_map_channels::xyz,
                       const normal_map_coverage* coverage = nullptr) -> normal_green_reading;

/// read_normal_green for an image file a normal map is imported from. Unknown when the file does not load.
auto read_normal_green(const fs::path& file, const normal_map_coverage* coverage = nullptr) -> normal_green_reading;

/**
 * @brief read_normal_green for a compiled normal map, read through the channels its format keeps a normal map in
 * (gfx::get_normal_map_channels). Unknown when the file does not load.
 */
auto read_compiled_normal_green(const fs::path& compiled_file) -> normal_green_reading;

/**
 * @brief The direction a file's name states: up for names such as wall_nor_gl.png and Wall_NormalGL.jpg, down for
 * wall_nor_dx.png and Wall_NormalDX.jpg, unknown for every other name.
 */
auto get_named_normal_green(const fs::path& file) -> normal_green;

/// The direction of the file a normal map is imported from: what its name states, else what its texels say, those
/// of `coverage` alone when it is given and usable.
auto find_normal_green(const fs::path& source_file, const normal_map_coverage* coverage = nullptr) -> normal_green;

/**
 * @brief Whether a texture with these import settings compiles with its Y turned over, as far as the settings and
 * the name of its file tell: with Normal Y Down always, with Up never, and with Auto by the convention the name
 * states, for a normal map. Empty for a normal map on Auto whose name states nothing: the compile reads its texels
 * (read_normal_green) and turns a green-down map over.
 */
auto get_stated_normal_y_inversion(const texture_importer_meta& importer, const fs::path& source_file) -> hpp::optional<bool>;

/// What a model import learns of a normal map on Auto that its materials bind.
struct normal_map_evidence
{
    /// What the texture compile makes of the map on its own: the direction its name states, else the one its
    /// texels read whole.
    normal_green compiled{normal_green::unknown};
    /// What the import knows of the map: the direction its name states, else the one of the texels the model draws
    /// where its UVs tell them, else the one the texels read whole.
    normal_green known{normal_green::unknown};
    /// Whether the map has slopes to turn over. A flat map compiles to the same texels either way, and so does a
    /// file that does not load.
    bool has_detail{true};
};

/// The evidence for the map imported from `source_file`, which the model draws through `coverage`. The file is
/// decoded once, and not at all when its name states the convention.
auto read_normal_map_evidence(const fs::path& source_file, const normal_map_coverage& coverage) -> normal_map_evidence;

/**
 * @brief The direction the normal maps of one model agree on: the one at least three in four of the maps that tell
 * a direction have. Unknown when fewer agree or none tells. A model's maps mostly come from one tool.
 */
auto find_common_normal_green(const std::vector<normal_map_evidence>& maps) -> normal_green;

/**
 * @brief The Normal Y a model import records for a map, given the direction its model's maps agree on. The map is
 * to compile by what the import knows of it, or, where it tells nothing (detail that runs one way, detail too
 * weak to read), by `common`. Unknown, which records nothing and leaves the map on Auto, when the texture compile
 * does that on its own, and for a flat map: only a map the compile would compile the other way gets Up or Down.
 */
auto find_normal_green_to_record(const normal_map_evidence& map, normal_green common) -> normal_green;

} // namespace unravel::asset_compiler
