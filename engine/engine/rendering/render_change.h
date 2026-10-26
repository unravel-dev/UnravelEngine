#pragma once

#include <array>
#include <cstdint>

namespace unravel
{

/**
 * @brief What changed about something the renderer draws, one bit per aspect. A system that keeps what it rendered
 * (a shadow map, a GI card) asks only about the aspects it reads (render_interest), so an edit it does not read
 * leaves its cache alone. Materials count their edits per aspect (render_change_revisions).
 */
struct render_change
{
    using mask = std::uint32_t;

    enum : mask
    {
        none = 0,
        /// The model's world transform: its own move or a parent's.
        placement = 1u << 0,
        /// A bone or submesh node matrix (model_component::update_armature).
        pose = 1u << 1,
        /// The meshes and what they place: mesh assets, LODs, submesh entries.
        geometry = 1u << 2,
        /// Whether and how the model takes part: enabled, active, static, casts shadow.
        membership = 1u << 3,
        /// Which texels are solid: alpha mode and cutoff, base color alpha, color map, dither threshold.
        coverage = 1u << 4,
        /// Which faces draw: cull type.
        sidedness = 1u << 5,
        /// The diffuse surface: base color, color map, metalness, subsurface color.
        albedo = 1u << 6,
        /// The shading normal: normal map, bumpiness.
        normal = 1u << 7,
        /// Roughness and the specular share of metalness.
        specular = 1u << 8,
        /// The ambient occlusion map.
        occlusion = 1u << 9,
        /// Emissive color, intensity and map.
        emission = 1u << 10,
    };

    /// How many aspects there are, one bit each.
    static constexpr std::uint32_t count = 11;

    /// The aspects a model's own state changes.
    static constexpr mask object_aspects = placement | pose | geometry | membership;
    /// The aspects a material's properties change (see material_property_id).
    static constexpr mask material_aspects = coverage | sidedness | albedo | normal | specular | occlusion | emission;
    /// Every aspect.
    static constexpr mask all = object_aspects | material_aspects;
};

static_assert(render_change::all == (1u << render_change::count) - 1u, "render_change::count misses an aspect");

/// One value per render_change aspect, indexed by the aspect's bit.
using render_change_values = std::array<std::uint64_t, render_change::count>;

/// Calls @p visit with the bit index of each aspect in @p changes.
template<typename Visit>
constexpr void for_each_aspect(render_change::mask changes, Visit&& visit)
{
    for(std::uint32_t i = 0; i < render_change::count; ++i)
    {
        if((changes & (1u << i)) != 0u)
        {
            visit(i);
        }
    }
}

/// The aspects whose values differ between @p before and @p after.
constexpr auto get_changed_aspects(const render_change_values& before, const render_change_values& after)
    -> render_change::mask
{
    render_change::mask changed = render_change::none;
    for_each_aspect(render_change::all,
                    [&](std::uint32_t i)
                    {
                        if(before[i] != after[i])
                        {
                            changed |= 1u << i;
                        }
                    });
    return changed;
}

/**
 * @brief Adds @p identity (an object address or an asset version) to the keys of the aspects in @p changes. Spread
 * over the 64 bits first: the keys also sum small edit counts.
 */
inline void add_identity_key(render_change_values& keys, render_change::mask changes, std::uint64_t identity)
{
    constexpr std::uint64_t IDENTITY_MULTIPLIER = 0x9E3779B97F4A7C15ull;
    const std::uint64_t spread = identity * IDENTITY_MULTIPLIER;
    for_each_aspect(changes,
                    [&](std::uint32_t i)
                    {
                        keys[i] += spread;
                    });
}

/**
 * @brief What each system reads, as render_change aspects: what it keeps from an earlier frame is valid while none
 * of its aspects changed. One place, so a system that starts reading another property only extends its mask here.
 */
struct render_interest
{
    /// A caster in a shadow map: where it is, its pose and meshes, whether it casts, which texels are solid and which
    /// faces draw.
    static constexpr render_change::mask shadow_caster = render_change::placement | render_change::pose |
                                                         render_change::geometry | render_change::membership |
                                                         render_change::coverage | render_change::sidedness;
    /// A GI card capture: what a card copies (albedo, normal, emission, coverage) and the card set's sidedness.
    static constexpr render_change::mask gi_card_capture = render_change::geometry | render_change::coverage |
                                                           render_change::sidedness | render_change::albedo |
                                                           render_change::normal | render_change::emission;
    /// A GI card's direct lighting, which reads only the captured normal and depth.
    static constexpr render_change::mask gi_card_direct =
        render_change::geometry | render_change::coverage | render_change::sidedness | render_change::normal;
};

/**
 * @brief Edit counters per aspect (render_change). A cache keeps get(the aspects it reads) from when it was built
 * and compares: the sum moves exactly when an edit touched one of those aspects.
 */
class render_change_revisions
{
public:
    /// Counts one edit of each aspect in @p changes.
    void bump(render_change::mask changes) noexcept
    {
        for_each_aspect(changes,
                        [&](std::uint32_t i)
                        {
                            ++revisions_[i];
                        });
    }

    /// The edits of each aspect, indexed by its bit.
    auto get_each() const noexcept -> const render_change_values&
    {
        return revisions_;
    }

    /// The edits of the aspects in @p changes, summed.
    auto get(render_change::mask changes) const noexcept -> std::uint64_t
    {
        std::uint64_t revision = 0;
        for_each_aspect(changes,
                        [&](std::uint32_t i)
                        {
                            revision += revisions_[i];
                        });
        return revision;
    }

private:
    render_change_values revisions_{};
};

/**
 * @brief The render frame each aspect (render_change) of one object last changed. One system writes it once per
 * frame (model_system for models); any system reads it, without consuming anything another system needs. A cache
 * built in frame F stays valid while has_changed_since(the aspects it reads, F) is false.
 */
class render_change_stamps
{
public:
    /// Records the aspects in @p changes as changed in render frame @p frame.
    void stamp(render_change::mask changes, std::uint64_t frame) noexcept
    {
        for_each_aspect(changes,
                        [&](std::uint32_t i)
                        {
                            frames_[i] = frame;
                        });
    }

    /// The last frame an aspect in @p interest changed; 0 when none ever did.
    auto get_last_change(render_change::mask interest) const noexcept -> std::uint64_t
    {
        std::uint64_t last = 0;
        for_each_aspect(interest,
                        [&](std::uint32_t i)
                        {
                            last = frames_[i] > last ? frames_[i] : last;
                        });
        return last;
    }

    /// Whether an aspect in @p interest changed after render frame @p frame.
    auto has_changed_since(render_change::mask interest, std::uint64_t frame) const noexcept -> bool
    {
        return get_last_change(interest) > frame;
    }

    /// Whether an aspect in @p interest changed within the @p frames frames up to @p frame: it is still settling.
    auto has_changed_within(render_change::mask interest, std::uint64_t frames, std::uint64_t frame) const noexcept
        -> bool
    {
        return frame < get_last_change(interest) + frames;
    }

private:
    render_change_values frames_{};
};

} // namespace unravel
