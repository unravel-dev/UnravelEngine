#include "normal_map_green.h"

#include <bimg/bimg.h>
#include <bx/allocator.h>
#include <bx/filepath.h>
#include <graphics/utils/bgfx_utils.h>
#include <hpp/string_view.hpp>
#include <string_utils/utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace unravel::asset_compiler
{
namespace
{

/// The largest side the test reads a map at; a larger one is averaged down to it.
constexpr std::uint32_t GREEN_MAX_SIDE = 2048;
/// The scales the cross slopes are compared at, each half the size of the one before. Codec noise sits in the
/// finest and the coarsest has few texels, while a real map has one sign in all of them.
constexpr std::size_t GREEN_SCALE_COUNT = 5;
/// A scale with a side under this many texels is not read.
constexpr std::uint32_t GREEN_MIN_SCALE_SIDE = 16;
/// The scales whose regions vote: the two below the finest, which codec noise does not reach.
constexpr std::size_t GREEN_FIRST_VOTING_SCALE = 1;
constexpr std::size_t GREEN_LAST_VOTING_SCALE = 2;
/// The map is split into this many regions a side, each of which votes for a direction.
constexpr std::uint32_t GREEN_REGION_GRID = 8;
/// Regions under this many cells a side do not vote, so a small map has no votes and reads as unknown.
constexpr std::uint32_t GREEN_MIN_REGION_SIDE = 8;
/// A region with less than this share of an average region's detail does not vote.
constexpr double GREEN_MIN_REGION_DETAIL = 0.02;
/// X and Y that stay this close to their means, as a share of the channel's range, make a flat map.
constexpr double GREEN_MIN_DETAIL = 0.004;
/// The mean correlation a direction needs. Maps baked for one object, whose detail is weak, reach about half of
/// it with either sign.
constexpr float GREEN_MIN_SCORE = 0.20f;
/// A scale with fewer cells than this in the parts a model draws is not read.
constexpr std::size_t GREEN_MIN_SCALE_CELLS = 256;
/// The least share of the coverage grid a model's vertices mark for the grid to tell what the model draws.
constexpr float GREEN_MIN_MARKED_SHARE = 0.10f;
/// The largest share of a model's vertices outside the UV square for the map to count as not repeating.
constexpr float GREEN_MAX_OUTSIDE_SHARE = 0.01f;
/// The share of a model's maps that tell a direction which must tell the same one for the model's maps to agree.
constexpr float GREEN_MIN_COMMON_SHARE = 0.75f;
/// A correlation closer to zero than this has no sign.
constexpr float GREEN_MIN_SIGNED = 0.02f;
/// The share of the voting regions that must read the direction the whole map reads.
constexpr double GREEN_MIN_REGION_SHARE = 0.75;
/// Cross slopes larger than the size this share of them are under count as that size.
constexpr double GREEN_OUTLIER_SHARE = 0.99;
/// The least Z squared a vector is given before its slope is taken.
constexpr float GREEN_MIN_Z_SQUARED = 0.04f;
/// Red that stays this close to its mean holds no X, and alpha that leaves its mean by more than the second value
/// holds it instead.
constexpr double GREEN_FLAT_RED = 0.01;
constexpr double GREEN_USED_ALPHA = 0.02;

constexpr std::uint32_t RGBA_RED = 0;
constexpr std::uint32_t RGBA_GREEN = 1;
constexpr std::uint32_t RGBA_ALPHA = 3;
constexpr std::uint32_t RGBA_BYTES = 4;

/// One channel of a map as numbers from 0 to 1, rows from the top.
struct green_channel
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<float> values;
};

/// The X and Y of a map's vectors as its channels store them.
struct green_vectors
{
    green_channel x;
    green_channel y;
};

/// How much smaller a map is read: the power of two that brings its larger side to GREEN_MAX_SIDE or under.
auto get_green_reduction(std::uint32_t width, std::uint32_t height) -> std::uint32_t
{
    std::uint32_t factor = 1;
    while(std::max(width, height) / factor > GREEN_MAX_SIDE)
    {
        factor *= 2;
    }
    return factor;
}

/// The channel at byte `offset` of every RGBA texel, each `factor` x `factor` block of texels averaged into one value.
auto read_green_channel(const std::uint8_t* rgba,
                        std::uint32_t width,
                        std::uint32_t height,
                        std::uint32_t pitch,
                        std::uint32_t offset,
                        std::uint32_t factor) -> green_channel
{
    green_channel channel;
    channel.width = width / factor;
    channel.height = height / factor;
    channel.values.resize(std::size_t(channel.width) * channel.height);
    std::vector<std::uint32_t> sums(channel.width);
    const float to_unit = 1.0f / (255.0f * float(factor) * float(factor));
    for(std::uint32_t row = 0; row < channel.height; ++row)
    {
        std::fill(sums.begin(), sums.end(), 0u);
        for(std::uint32_t sub_row = 0; sub_row < factor; ++sub_row)
        {
            const std::uint8_t* texel = rgba + std::size_t(row * factor + sub_row) * pitch + offset;
            for(std::uint32_t column = 0; column < channel.width; ++column)
            {
                for(std::uint32_t sub_column = 0; sub_column < factor; ++sub_column, texel += RGBA_BYTES)
                {
                    sums[column] += *texel;
                }
            }
        }
        float* values = channel.values.data() + std::size_t(row) * channel.width;
        for(std::uint32_t column = 0; column < channel.width; ++column)
        {
            values[column] = float(sums[column]) * to_unit;
        }
    }
    return channel;
}

/// The channel at half its size, each 2 x 2 block averaged; an odd last row or column is left out.
auto halve_green_channel(const green_channel& channel) -> green_channel
{
    green_channel half;
    half.width = channel.width / 2;
    half.height = channel.height / 2;
    half.values.resize(std::size_t(half.width) * half.height);
    for(std::uint32_t row = 0; row < half.height; ++row)
    {
        const float* upper = channel.values.data() + std::size_t(row) * 2 * channel.width;
        const float* lower = upper + channel.width;
        float* values = half.values.data() + std::size_t(row) * half.width;
        for(std::uint32_t column = 0; column < half.width; ++column)
        {
            values[column] = 0.25f * (upper[column * 2] + upper[column * 2 + 1] + lower[column * 2] + lower[column * 2 + 1]);
        }
    }
    return half;
}

/// The mean square distance of a channel's values from their mean.
auto measure_green_variance(const green_channel& channel) -> double
{
    if(channel.values.empty())
    {
        return 0.0;
    }
    double sum = 0.0;
    double sum_of_squares = 0.0;
    for(const float value : channel.values)
    {
        sum += value;
        sum_of_squares += double(value) * value;
    }
    const double count = double(channel.values.size());
    const double mean = sum / count;
    return std::max(0.0, sum_of_squares / count - mean * mean);
}

auto read_green_vectors(const std::uint8_t* rgba,
                        std::uint32_t width,
                        std::uint32_t height,
                        std::uint32_t pitch,
                        gfx::normal_map_channels channels) -> green_vectors
{
    const auto factor = get_green_reduction(width, height);
    const auto read = [&](std::uint32_t offset)
    {
        return read_green_channel(rgba, width, height, pitch, offset, factor);
    };
    if(channels == gfx::normal_map_channels::x_red_y_alpha)
    {
        return {read(RGBA_RED), read(RGBA_ALPHA)};
    }
    auto red = read(RGBA_RED);
    if(std::sqrt(measure_green_variance(red)) < GREEN_FLAT_RED)
    {
        auto alpha = read(RGBA_ALPHA);
        if(std::sqrt(measure_green_variance(alpha)) > GREEN_USED_ALPHA)
        {
            return {std::move(alpha), read(RGBA_GREEN)};
        }
    }
    return {std::move(red), read(RGBA_GREEN)};
}

auto is_flat_green_map(const green_vectors& vectors) -> bool
{
    return std::sqrt(measure_green_variance(vectors.x) + measure_green_variance(vectors.y)) < GREEN_MIN_DETAIL;
}

/**
 * @brief The two changes the test compares, at the centre of every 2 x 2 block of texels: how the surface's slope
 * across the image changes down the image, and how its slope down the image changes across it, taking green as up.
 * For a surface they are equal when green is up and opposite when green is down.
 */
struct green_cross_slopes
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<float> across_changing_down;
    std::vector<float> down_changing_across;
};

/// The surface's slopes across and down the image for one row of vectors, taking green as up. Z is rebuilt from X
/// and Y and kept away from zero, where the slope of a vector lying in the surface has no bound.
void compute_green_slope_row(const float* x, const float* y, std::uint32_t count, float* across, float* down)
{
    for(std::uint32_t column = 0; column < count; ++column)
    {
        const float nx = x[column] * 2.0f - 1.0f;
        const float ny = y[column] * 2.0f - 1.0f;
        const float nz = std::sqrt(std::clamp(1.0f - nx * nx - ny * ny, GREEN_MIN_Z_SQUARED, 1.0f));
        across[column] = -nx / nz;
        down[column] = ny / nz;
    }
}

auto compute_green_cross_slopes(const green_vectors& vectors) -> green_cross_slopes
{
    const auto width = vectors.x.width;
    const auto height = vectors.x.height;
    green_cross_slopes slopes;
    slopes.width = width - 1;
    slopes.height = height - 1;
    slopes.across_changing_down.resize(std::size_t(slopes.width) * slopes.height);
    slopes.down_changing_across.resize(std::size_t(slopes.width) * slopes.height);

    std::vector<float> upper_across(width);
    std::vector<float> upper_down(width);
    std::vector<float> lower_across(width);
    std::vector<float> lower_down(width);
    compute_green_slope_row(vectors.x.values.data(), vectors.y.values.data(), width, upper_across.data(), upper_down.data());
    for(std::uint32_t row = 0; row < slopes.height; ++row)
    {
        const std::size_t next = std::size_t(row + 1) * width;
        compute_green_slope_row(vectors.x.values.data() + next,
                                vectors.y.values.data() + next,
                                width,
                                lower_across.data(),
                                lower_down.data());
        float* first = slopes.across_changing_down.data() + std::size_t(row) * slopes.width;
        float* second = slopes.down_changing_across.data() + std::size_t(row) * slopes.width;
        for(std::uint32_t column = 0; column < slopes.width; ++column)
        {
            first[column] = 0.5f * ((lower_across[column] + lower_across[column + 1]) -
                                    (upper_across[column] + upper_across[column + 1]));
            second[column] =
                0.5f * ((upper_down[column + 1] + lower_down[column + 1]) - (upper_down[column] + lower_down[column]));
        }
        upper_across.swap(lower_across);
        upper_down.swap(lower_down);
    }
    return slopes;
}

/// For each texel of a `width` x `height` channel, whether every coverage cell it lies in is marked.
auto find_drawn_texels(const normal_map_coverage& coverage, std::uint32_t width, std::uint32_t height)
    -> std::vector<std::uint8_t>
{
    constexpr std::uint32_t SIDE = normal_map_coverage::SIDE;
    // The first and last grid cell a texel column or row reaches: one cell, or several where a texel is larger
    // than a cell.
    const auto get_cells = [](std::uint32_t texel, std::uint32_t texels)
    {
        const std::uint32_t first = texel * SIDE / texels;
        const std::uint32_t last = std::min(SIDE - 1, ((texel + 1) * SIDE - 1) / texels);
        return std::pair<std::uint32_t, std::uint32_t>(first, std::max(first, last));
    };
    std::vector<std::pair<std::uint32_t, std::uint32_t>> column_cells(width);
    for(std::uint32_t column = 0; column < width; ++column)
    {
        column_cells[column] = get_cells(column, width);
    }
    std::vector<std::uint8_t> drawn(std::size_t(width) * height);
    for(std::uint32_t row = 0; row < height; ++row)
    {
        const auto [first_row, last_row] = get_cells(row, height);
        for(std::uint32_t column = 0; column < width; ++column)
        {
            const auto [first_column, last_column] = column_cells[column];
            bool is_drawn = true;
            for(std::uint32_t cell_row = first_row; cell_row <= last_row && is_drawn; ++cell_row)
            {
                for(std::uint32_t cell_column = first_column; cell_column <= last_column && is_drawn; ++cell_column)
                {
                    is_drawn = coverage.is_marked(cell_column, cell_row);
                }
            }
            drawn[std::size_t(row) * width + column] = is_drawn ? 1 : 0;
        }
    }
    return drawn;
}

/**
 * @brief Leaves the cells with a texel the model does not draw out of the cross slopes: they become zero, which
 * takes no part in a sum. `drawn` receives 1 for each cell kept.
 * @return The number of cells kept.
 */
auto keep_drawn_cells(green_cross_slopes& slopes,
                      const normal_map_coverage& coverage,
                      std::uint32_t texel_width,
                      std::uint32_t texel_height,
                      std::vector<std::uint8_t>& drawn) -> std::size_t
{
    const auto texels = find_drawn_texels(coverage, texel_width, texel_height);
    drawn.assign(std::size_t(slopes.width) * slopes.height, 0);
    std::size_t kept = 0;
    for(std::uint32_t row = 0; row < slopes.height; ++row)
    {
        const std::uint8_t* upper = texels.data() + std::size_t(row) * texel_width;
        const std::uint8_t* lower = upper + texel_width;
        const std::size_t first = std::size_t(row) * slopes.width;
        for(std::uint32_t column = 0; column < slopes.width; ++column)
        {
            if(upper[column] && upper[column + 1] && lower[column] && lower[column + 1])
            {
                drawn[first + column] = 1;
                ++kept;
            }
            else
            {
                slopes.across_changing_down[first + column] = 0.0f;
                slopes.down_changing_across[first + column] = 0.0f;
            }
        }
    }
    return kept;
}

/// Limits the values to the size GREEN_OUTLIER_SHARE of them are under: the few texels on a hard edge would
/// otherwise outweigh the rest of the map. With `drawn`, the size is taken from the cells it keeps.
void clip_green_outliers(std::vector<float>& values, std::vector<float>& sizes, const std::vector<std::uint8_t>* drawn)
{
    sizes.clear();
    sizes.reserve(values.size());
    for(std::size_t index = 0; index < values.size(); ++index)
    {
        if(drawn == nullptr || (*drawn)[index] != 0)
        {
            sizes.push_back(std::abs(values[index]));
        }
    }
    if(sizes.empty())
    {
        return;
    }
    // The limit lies between two sizes when the share falls between them.
    const double position = double(sizes.size() - 1) * GREEN_OUTLIER_SHARE;
    const auto index = std::size_t(position);
    const auto at = sizes.begin() + std::ptrdiff_t(index);
    std::nth_element(sizes.begin(), at, sizes.end());
    float limit = *at;
    if(index + 1 < sizes.size())
    {
        const float above = *std::min_element(at + 1, sizes.end());
        limit += (above - limit) * float(position - double(index));
    }
    if(limit <= 0.0f)
    {
        return;
    }
    for(float& value : values)
    {
        value = std::clamp(value, -limit, limit);
    }
}

/// The sums a correlation is made of, over some cells of the cross slopes.
struct green_slope_sums
{
    double first_squared{};
    double second_squared{};
    double product{};

    auto get_detail() const -> double
    {
        return first_squared + second_squared;
    }

    /// From -1 (the two changes are opposite) to 1 (they are equal); 0 where either does not change.
    auto get_correlation() const -> float
    {
        constexpr double NOTHING = 1e-20;
        if(first_squared <= NOTHING || second_squared <= NOTHING)
        {
            return 0.0f;
        }
        return float(product / std::sqrt(first_squared * second_squared));
    }
};

auto sum_green_slopes(const green_cross_slopes& slopes,
                      std::uint32_t left,
                      std::uint32_t top,
                      std::uint32_t width,
                      std::uint32_t height) -> green_slope_sums
{
    green_slope_sums sums;
    for(std::uint32_t row = top; row < top + height; ++row)
    {
        const float* first = slopes.across_changing_down.data() + std::size_t(row) * slopes.width + left;
        const float* second = slopes.down_changing_across.data() + std::size_t(row) * slopes.width + left;
        double first_squared = 0.0;
        double second_squared = 0.0;
        double product = 0.0;
        for(std::uint32_t column = 0; column < width; ++column)
        {
            first_squared += double(first[column]) * first[column];
            second_squared += double(second[column]) * second[column];
            product += double(first[column]) * second[column];
        }
        sums.first_squared += first_squared;
        sums.second_squared += second_squared;
        sums.product += product;
    }
    return sums;
}

/// How many regions of a map read each direction.
struct green_votes
{
    std::uint32_t up{};
    std::uint32_t down{};
};

/// Adds the vote of every region of the scale that has detail to speak of.
void count_green_votes(const green_cross_slopes& slopes, const green_slope_sums& whole, green_votes& votes)
{
    const auto region_width = slopes.width / GREEN_REGION_GRID;
    const auto region_height = slopes.height / GREEN_REGION_GRID;
    if(region_width < GREEN_MIN_REGION_SIDE || region_height < GREEN_MIN_REGION_SIDE)
    {
        return;
    }
    const double least_detail = whole.get_detail() / double(GREEN_REGION_GRID * GREEN_REGION_GRID) * GREEN_MIN_REGION_DETAIL;
    for(std::uint32_t region_row = 0; region_row < GREEN_REGION_GRID; ++region_row)
    {
        for(std::uint32_t region_column = 0; region_column < GREEN_REGION_GRID; ++region_column)
        {
            const auto sums = sum_green_slopes(slopes,
                                               region_column * region_width,
                                               region_row * region_height,
                                               region_width,
                                               region_height);
            if(sums.get_detail() < least_detail)
            {
                continue;
            }
            const float correlation = sums.get_correlation();
            if(correlation > GREEN_MIN_SIGNED)
            {
                ++votes.up;
            }
            else if(correlation < -GREEN_MIN_SIGNED)
            {
                ++votes.down;
            }
        }
    }
}

/// What every scale of a map says about its green channel.
struct green_evidence
{
    /// The correlation of the cross slopes at each scale that was read, finest first.
    std::vector<float> correlations;
    green_votes votes;
};

/// `coverage`: the parts of the map a model draws, which alone are read; null reads the whole map.
auto gather_green_evidence(green_vectors vectors, const normal_map_coverage* coverage) -> green_evidence
{
    green_evidence evidence;
    std::vector<float> sizes;
    std::vector<std::uint8_t> drawn;
    for(std::size_t scale = 0; scale < GREEN_SCALE_COUNT; ++scale)
    {
        if(scale > 0)
        {
            vectors.x = halve_green_channel(vectors.x);
            vectors.y = halve_green_channel(vectors.y);
        }
        if(std::min(vectors.x.width, vectors.x.height) < GREEN_MIN_SCALE_SIDE)
        {
            break;
        }
        auto slopes = compute_green_cross_slopes(vectors);
        if(coverage != nullptr &&
           keep_drawn_cells(slopes, *coverage, vectors.x.width, vectors.x.height, drawn) < GREEN_MIN_SCALE_CELLS)
        {
            continue;
        }
        const auto* kept = coverage != nullptr ? &drawn : nullptr;
        clip_green_outliers(slopes.across_changing_down, sizes, kept);
        clip_green_outliers(slopes.down_changing_across, sizes, kept);
        const auto whole = sum_green_slopes(slopes, 0, 0, slopes.width, slopes.height);
        evidence.correlations.push_back(whole.get_correlation());
        if(scale >= GREEN_FIRST_VOTING_SCALE && scale <= GREEN_LAST_VOTING_SCALE)
        {
            count_green_votes(slopes, whole, evidence.votes);
        }
    }
    return evidence;
}

/// Whether every scale that has a sign has the same one.
auto has_one_green_sign(const std::vector<float>& correlations) -> bool
{
    bool is_positive = false;
    bool is_negative = false;
    for(const float correlation : correlations)
    {
        is_positive = is_positive || correlation > GREEN_MIN_SIGNED;
        is_negative = is_negative || correlation < -GREEN_MIN_SIGNED;
    }
    return is_positive != is_negative;
}

/// Whether enough of the regions that voted read `direction`.
auto do_green_regions_agree(const green_votes& votes, normal_green direction) -> bool
{
    const auto voted = votes.up + votes.down;
    const auto agreeing = direction == normal_green::up ? votes.up : votes.down;
    return voted > 0 && double(agreeing) >= double(voted) * GREEN_MIN_REGION_SHARE;
}

auto judge_green_evidence(const green_evidence& evidence) -> normal_green_reading
{
    normal_green_reading reading;
    if(evidence.correlations.empty())
    {
        return reading;
    }
    float sum = 0.0f;
    for(const float correlation : evidence.correlations)
    {
        sum += correlation;
    }
    reading.score = sum / float(evidence.correlations.size());
    const auto direction = reading.score > 0.0f ? normal_green::up : normal_green::down;
    if(std::abs(reading.score) >= GREEN_MIN_SCORE && has_one_green_sign(evidence.correlations) &&
       do_green_regions_agree(evidence.votes, direction))
    {
        reading.direction = direction;
    }
    return reading;
}

/// The mip the test reads: the largest of at most GREEN_MAX_SIDE texels a side, else the image's smallest.
auto find_green_mip(const bimg::ImageContainer& image, bimg::ImageMip& mip) -> bool
{
    bool is_found = false;
    for(std::uint8_t lod = 0; lod < image.m_numMips; ++lod)
    {
        bimg::ImageMip candidate;
        if(!bimg::imageGetRawData(image, 0, lod, image.m_data, image.m_size, candidate))
        {
            break;
        }
        mip = candidate;
        is_found = true;
        if(std::max(candidate.m_width, candidate.m_height) <= GREEN_MAX_SIDE)
        {
            break;
        }
    }
    return is_found;
}

auto is_green_name_letter(char character) -> bool
{
    return character >= 'a' && character <= 'z';
}

auto is_green_name_separator(char character) -> bool
{
    return character == '_' || character == '-' || character == '.' || character == ' ';
}

/// The direction of the convention's name at `at` in a lower case file name, when one starts there and no letter
/// follows it: "dx", but not the "dx" of "dxt5".
auto match_green_convention(hpp::string_view name, std::size_t at) -> normal_green
{
    struct convention_word
    {
        hpp::string_view text;
        normal_green direction;
    };
    static constexpr std::array<convention_word, 6> WORDS{{{"opengl", normal_green::up},
                                                           {"ogl", normal_green::up},
                                                           {"gl", normal_green::up},
                                                           {"directx", normal_green::down},
                                                           {"d3d", normal_green::down},
                                                           {"dx", normal_green::down}}};
    if(at >= name.size())
    {
        return normal_green::unknown;
    }
    for(const auto& word : WORDS)
    {
        const auto end = at + word.text.size();
        if(name.substr(at, word.text.size()) == word.text && (end == name.size() || !is_green_name_letter(name[end])))
        {
            return word.direction;
        }
    }
    return normal_green::unknown;
}

/// Loads `file` and reads its green direction through the channels `get_channels` gives for its format.
template<typename GetChannels>
auto read_green_of_file(const fs::path& file, const normal_map_coverage* coverage, GetChannels&& get_channels)
    -> normal_green_reading
{
    bimg::ImageContainer* image = imageLoad(bx::FilePath(file.string().c_str()));
    if(image == nullptr)
    {
        return {};
    }
    const auto reading = read_normal_green(*image, get_channels(image->m_format), coverage);
    bimg::imageFree(image);
    return reading;
}

} // namespace

auto invert_normal_green(normal_green direction) -> normal_green
{
    switch(direction)
    {
        case normal_green::up:
            return normal_green::down;
        case normal_green::down:
            return normal_green::up;
        case normal_green::unknown:
        default:
            return normal_green::unknown;
    }
}

normal_map_coverage::normal_map_coverage()
    : cells_(std::size_t(SIDE) * SIDE, 0)
{
}

void normal_map_coverage::add_vertex(float u, float v)
{
    // Half a cell past an edge still belongs to the edge's cell: UVs on the edge are stored a little off it.
    constexpr float EDGE = 0.5f / float(SIDE);
    ++vertices_;
    const bool is_inside = u >= -EDGE && u <= 1.0f + EDGE && v >= -EDGE && v <= 1.0f + EDGE;
    if(!is_inside)
    {
        ++outside_;
        return;
    }
    const auto to_cell = [](float coordinate)
    {
        return std::min(SIDE - 1, std::uint32_t(std::max(coordinate, 0.0f) * float(SIDE)));
    };
    auto& cell = cells_[std::size_t(to_cell(v)) * SIDE + to_cell(u)];
    if(cell == 0)
    {
        cell = 1;
        ++marked_;
    }
}

auto normal_map_coverage::is_marked(std::uint32_t column, std::uint32_t row) const -> bool
{
    return column < SIDE && row < SIDE && cells_[std::size_t(row) * SIDE + column] != 0;
}

auto normal_map_coverage::get_marked_share() const -> float
{
    return float(marked_) / float(cells_.size());
}

auto normal_map_coverage::is_usable() const -> bool
{
    return vertices_ > 0 && float(outside_) <= float(vertices_) * GREEN_MAX_OUTSIDE_SHARE &&
           get_marked_share() >= GREEN_MIN_MARKED_SHARE;
}

auto read_normal_green(const std::uint8_t* rgba,
                       std::uint32_t width,
                       std::uint32_t height,
                       std::uint32_t pitch,
                       gfx::normal_map_channels channels,
                       const normal_map_coverage* coverage) -> normal_green_reading
{
    if(rgba == nullptr || width == 0 || height == 0)
    {
        return {};
    }
    auto vectors = read_green_vectors(rgba, width, height, pitch, channels);
    if(is_flat_green_map(vectors))
    {
        return {};
    }
    const auto* drawn_parts = coverage != nullptr && coverage->is_usable() ? coverage : nullptr;
    auto reading = judge_green_evidence(gather_green_evidence(std::move(vectors), drawn_parts));
    reading.has_detail = true;
    return reading;
}

auto read_normal_green(const bimg::ImageContainer& image,
                       gfx::normal_map_channels channels,
                       const normal_map_coverage* coverage) -> normal_green_reading
{
    bimg::ImageMip mip;
    if(image.m_cubeMap || image.m_depth > 1 || !find_green_mip(image, mip))
    {
        return {};
    }
    const std::uint32_t pitch = mip.m_width * RGBA_BYTES;
    if(mip.m_format == bimg::TextureFormat::RGBA8)
    {
        return read_normal_green(mip.m_data, mip.m_width, mip.m_height, pitch, channels, coverage);
    }
    // bimg decodes every format it parses. For the few it has no decoder for it fills in a checkerboard, whose
    // two cross slopes never meet in a cell, so such a map reads as unknown.
    std::vector<std::uint8_t> rgba(std::size_t(pitch) * mip.m_height);
    bx::DefaultAllocator allocator;
    bimg::imageDecodeToRgba8(&allocator, rgba.data(), mip.m_data, mip.m_width, mip.m_height, pitch, mip.m_format);
    return read_normal_green(rgba.data(), mip.m_width, mip.m_height, pitch, channels, coverage);
}

auto read_normal_green(const fs::path& file, const normal_map_coverage* coverage) -> normal_green_reading
{
    return read_green_of_file(file,
                              coverage,
                              [](bimg::TextureFormat::Enum)
                              {
                                  return gfx::normal_map_channels::xyz;
                              });
}

auto read_compiled_normal_green(const fs::path& compiled_file) -> normal_green_reading
{
    return read_green_of_file(compiled_file,
                              nullptr,
                              [](bimg::TextureFormat::Enum format)
                              {
                                  return gfx::get_normal_map_channels(bgfx::TextureFormat::Enum(format), true);
                              });
}

auto get_named_normal_green(const fs::path& file) -> normal_green
{
    struct normal_word
    {
        hpp::string_view text;
        /// A short word counts only where a word starts: the "nor" of "minor" is not one.
        bool starts_a_word;
    };
    static constexpr std::array<normal_word, 5> WORDS{
        {{"normals", false}, {"normal", false}, {"norm", true}, {"nrm", true}, {"nor", true}}};

    const std::string lowered = string_utils::to_lower(file.stem().string());
    const hpp::string_view name(lowered);
    for(const auto& word : WORDS)
    {
        for(auto at = name.find(word.text); at != hpp::string_view::npos; at = name.find(word.text, at + 1))
        {
            if(word.starts_a_word && at > 0 && is_green_name_letter(name[at - 1]))
            {
                continue;
            }
            auto next = at + word.text.size();
            if(next < name.size() && is_green_name_separator(name[next]))
            {
                ++next;
            }
            const auto direction = match_green_convention(name, next);
            if(direction != normal_green::unknown)
            {
                return direction;
            }
        }
    }
    return normal_green::unknown;
}

auto find_normal_green(const fs::path& source_file, const normal_map_coverage* coverage) -> normal_green
{
    const auto named = get_named_normal_green(source_file);
    return named != normal_green::unknown ? named : read_normal_green(source_file, coverage).direction;
}

auto get_stated_normal_y_inversion(const texture_importer_meta& importer, const fs::path& source_file) -> hpp::optional<bool>
{
    switch(importer.normal_y)
    {
        case texture_importer_meta::normal_direction::down:
            return true;
        case texture_importer_meta::normal_direction::up:
            return false;
        case texture_importer_meta::normal_direction::automatic:
        default:
            break;
    }
    if(importer.type != texture_importer_meta::texture_type::normal_map)
    {
        return false;
    }
    const auto named = get_named_normal_green(source_file);
    if(named == normal_green::unknown)
    {
        return hpp::nullopt;
    }
    return named == normal_green::down;
}

auto read_normal_map_evidence(const fs::path& source_file, const normal_map_coverage& coverage) -> normal_map_evidence
{
    const auto named = get_named_normal_green(source_file);
    if(named != normal_green::unknown)
    {
        return {named, named};
    }
    normal_map_evidence evidence;
    bimg::ImageContainer* image = imageLoad(bx::FilePath(source_file.string().c_str()));
    if(image == nullptr)
    {
        evidence.has_detail = false;
        return evidence;
    }
    const auto whole = read_normal_green(*image);
    evidence.compiled = whole.direction;
    evidence.has_detail = whole.has_detail;
    evidence.known = coverage.is_usable() ? read_normal_green(*image, gfx::normal_map_channels::xyz, &coverage).direction
                                          : evidence.compiled;
    bimg::imageFree(image);
    return evidence;
}

auto find_common_normal_green(const std::vector<normal_map_evidence>& maps) -> normal_green
{
    const auto count = [&maps](normal_green direction)
    {
        return std::count_if(maps.begin(),
                             maps.end(),
                             [direction](const normal_map_evidence& map)
                             {
                                 return map.known == direction;
                             });
    };
    const float down = float(count(normal_green::down));
    const float up = float(count(normal_green::up));
    const float needed = (down + up) * GREEN_MIN_COMMON_SHARE;
    if(down > 0.0f && down >= needed)
    {
        return normal_green::down;
    }
    if(up > 0.0f && up >= needed)
    {
        return normal_green::up;
    }
    return normal_green::unknown;
}

auto find_normal_green_to_record(const normal_map_evidence& map, normal_green common) -> normal_green
{
    if(!map.has_detail)
    {
        return normal_green::unknown;
    }
    const auto wanted = map.known != normal_green::unknown ? map.known : common;
    const bool is_wanted_down = wanted == normal_green::down;
    const bool is_compiled_down = map.compiled == normal_green::down;
    if(is_wanted_down == is_compiled_down)
    {
        return normal_green::unknown;
    }
    return is_wanted_down ? normal_green::down : normal_green::up;
}

} // namespace unravel::asset_compiler
