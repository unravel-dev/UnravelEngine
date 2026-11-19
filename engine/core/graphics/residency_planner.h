#pragma once

#include <array>
#include <cstdint>
#include <vector>

/// What GPU memory pressure asks of the evictable resources each frame, worked out without bgfx so it
/// can be tested on any backend.
///
/// A resource holds its full GPU copy (level 0), a copy without its top mips (a level above 0), or
/// none (evicted). A texture whose small mips stay on the GPU has a tail level: the level that leaves
/// only the mips at or below the tail size. Everything else is evicted whole.
namespace gfx::residency
{

/// Most levels a resource can have: the mips of a 32768 texture.
inline constexpr std::size_t k_max_levels = 16;

/// The order in which idle resources give up memory.
enum class strategy : std::uint8_t
{
    lru,           ///< Least recently used first.
    lfu,           ///< Least often used first.
    largest_first, ///< Most bytes freed first.
    age_ttl,       ///< Every resource idle longer than request::max_idle_frames, oldest first.
};

/// One tracked resource as the planner sees it.
struct candidate
{
    /// GPU bytes at each level, from level 0 up to the tail level or the current level.
    std::array<std::uint64_t, k_max_levels> bytes{};
    /// Top mips the GPU copy lacks now. Meaningless while evicted.
    std::uint8_t level{0};
    /// The level that keeps only the tail; 0 when the resource has no tail and is evicted whole.
    std::uint8_t tail_level{0};
    /// False while evicted: no GPU copy.
    bool has_handle{true};
    /// Frame of the last read of any kind.
    std::uint64_t last_use{0};
    /// Frame of the last read that needs every mip.
    std::uint64_t last_full_use{0};
    /// Frame the planner last took memory from it; 0 when it never did.
    std::uint64_t last_drop{0};
    /// Frames between reads it showed the last time a read needed what a drop took: it stays that much
    /// longer past the minimum age before it counts as unread. 0 when that gap was long or never seen.
    std::uint64_t reuse_frames{0};
    /// Reads since it was registered.
    std::uint64_t use_count{0};
};

/// What one pass may do.
struct request
{
    /// The current frame.
    std::uint64_t frame{0};
    strategy order{strategy::lru};
    /// Resources read within this many frames are not idle, and nothing rises within this many frames of
    /// a drop.
    std::uint32_t min_age_frames{60};
    /// For strategy::age_ttl: drop resources idle for more than this many frames.
    std::uint32_t max_idle_frames{0};
    /// Most drops in the pass; 0 for no limit.
    std::uint32_t max_drops{0};
    /// Bytes the drops should free; 0 for no drops. With strategy::age_ttl or ignore_age any other
    /// value drops every resource that qualifies.
    std::uint64_t drop_bytes{0};
    /// Drop resources at any age, read recently or not (the profiler's evict all).
    bool ignore_age{false};
    /// After the idle resources, take one level from textures in use that only sampled reads used
    /// lately, while drop_bytes is not reached.
    bool drop_in_use{false};
    /// Most drops of textures in use in the pass, each a texture created; 0 for no limit. Counted
    /// within max_drops.
    std::uint32_t max_in_use_drops{0};
    /// Bytes rises may allocate; 0 for no rises.
    std::uint64_t rise_bytes{0};
    /// Most bytes rises upload in the pass; 0 for no limit. The pass's first rise may exceed it, so a
    /// texture larger than the limit still rises.
    std::uint64_t max_upload_bytes{0};
    /// Most rises in the pass, each a texture created; 0 for no limit.
    std::uint32_t max_rises{0};
    /// A reduced resource read within this many frames rises when there is room.
    std::uint32_t wanted_frames{2};
};

/// One change the planner asks for.
struct action
{
    /// Index of the resource in the candidates passed to plan().
    std::uint32_t index{0};
    /// The level to go to; ignored when evict is true.
    std::uint8_t level{0};
    /// Release the GPU copy.
    bool evict{false};
};

/// What a pass asks for and what it adds up to.
struct plan_result
{
    /// Drops first, then rises, in the order to apply them.
    std::vector<action> actions;
    std::uint64_t dropped_bytes{0};
    std::uint32_t drops{0};
    std::uint64_t raised_bytes{0};
    std::uint32_t rises{0};
    /// Reduced resources read lately that did not rise in this pass.
    std::uint32_t backlog{0};
};

/// The smallest level at which @p width x @p height keeps only mips whose longer side is at most
/// @p tail_size, given @p num_mips; the tail keeps at least one mip. A level whose top mip is no
/// longer a whole number of blocks (@p block_width x @p block_height) when the full texture's is,
/// which Direct3D refuses, gives way to the largest level below it that is. 0 when no level above
/// 0 qualifies.
auto calc_tail_level(std::uint32_t width,
                     std::uint32_t height,
                     std::uint8_t num_mips,
                     std::uint32_t tail_size,
                     std::uint32_t block_width = 1,
                     std::uint32_t block_height = 1) -> std::uint8_t;

/// The changes that move @p candidates toward what @p req asks. Deterministic: equal inputs give equal
/// plans, and ties keep the order of @p candidates.
///
/// Drops first, while drop_bytes is not reached and max_drops allows: idle resources (not read for
/// min_age_frames) in the strategy's order go to their tail level, or are evicted when they have no
/// tail; then, with drop_in_use, textures read lately but not at full size for min_age_frames lose one
/// level each, largest saving first, never below the tail. Rises only when drop_bytes is 0:
/// reduced resources read within wanted_frames and not dropped for min_age_frames, most recently read
/// first, each to the highest level whose bytes fit what is left of rise_bytes and of the upload
/// limit.
auto plan(const std::vector<candidate>& candidates, const request& req) -> plan_result;

} // namespace gfx::residency
