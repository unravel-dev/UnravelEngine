#include "text_component.h"
#include <cstdlib> // for std::strtof
#include <hpp/string_view.hpp>

#include <engine/profiler/profiler.h>

#include <libunibreak/graphemebreak.h>
#include <libunibreak/linebreak.h>
#include <libunibreak/linebreakdef.h>
#include <libunibreak/unibreakdef.h>

namespace unravel
{

namespace
{

// Conversion ratio: 1 meter = 10 pixel units
constexpr float PIXELS_PER_METER = 10.0f;

constexpr float METERS_PER_PIXEL = 1.0f / PIXELS_PER_METER;

auto fade(uint32_t c, float alphaMultiplier) -> uint32_t
{
    math::color c0(c);
    math::color result = c0.value * alphaMultiplier;
    return result;
};
// Only these three drive uniform changes in submit_text_buffer
auto can_batch_with(const text_style& lhs, const text_style& rhs) -> bool
{
    constexpr float EPS = 1e-6f;
    auto feq = [&](float a, float b)
    {
        return std::fabs(a - b) < EPS;
    };

    return feq(lhs.outline_width, rhs.outline_width) && feq(lhs.shadow_softener, rhs.shadow_softener) &&
           fade(lhs.shadow_color, lhs.opacity) == fade(rhs.shadow_color, rhs.opacity);
}
// Applies all style settings from a rich_state to the text buffer.
void apply_style(gfx::text_buffer_manager& manager, gfx::text_buffer_handle tb, const text_style& state)
{
    manager.set_text_color(tb, fade(state.text_color, state.opacity));
    manager.set_background_color(tb, fade(state.background_color, state.opacity));
    manager.set_foreground_color(tb, fade(state.foreground_color, state.opacity));
    manager.set_overline_color(tb, fade(state.overline_color, state.opacity));
    manager.set_underline_color(tb, fade(state.underline_color, state.opacity));
    manager.set_strike_through_color(tb, fade(state.strike_color, state.opacity));
    manager.set_outline_width(tb, state.outline_width);
    manager.set_outline_color(tb, fade(state.outline_color, state.opacity));
    manager.set_drop_shadow_offset(tb, state.shadow_offsets.x, state.shadow_offsets.y);
    manager.set_drop_shadow_color(tb, fade(state.shadow_color, state.opacity));
    manager.set_drop_shadow_softener(tb, state.shadow_softener);
    manager.set_style(tb, state.style_flags);
}

// safe float parser: tries from_chars, then strtof
auto safe_parse_float(hpp::string_view const& s, float def = 0.0f) -> float
{
    float value = def;

    // 2) fallback: copy into a std::string (which *is* NUL-terminated)
    {
        std::string tmp(s.data(), s.size());
        char* endptr = nullptr;
        errno = 0;
        float v = std::strtof(tmp.c_str(), &endptr);
        if(errno == 0 && endptr == tmp.c_str() + tmp.size())
        {
            return v;
        }
    }

    // 3) neither worked → default
    return def;
}

// -------------------------------------------------
// 1) super‐fast color parser
// -------------------------------------------------
// clang-format off
static constexpr std::pair<hpp::string_view, uint32_t> k_named_colors[] = {
    { "black",   0xFF000000u },
    { "white",   0xFFFFFFFFu },
    { "red",     0xFF0000FFu },
    { "green",   0xFF00FF00u },
    { "blue",    0xFFFF0000u },
    { "yellow",  0xFF00FFFFu },
    { "cyan",    0xFFFFFF00u },
    { "magenta", 0xFFFF00FFu },
    { "gray",    0xFF808080u },
    { "grey",    0xFF808080u },
    { "orange",  0xFF00A5FFu },
    { "purple",  0xFF800080u },
    { "pink",    0xFFCBC0FFu },
    { "brown",   0xFF2A2AFFu },
    { "maroon",  0xFF000080u },
    { "olive",   0xFF008080u },
    { "navy",    0xFF800000u },
    { "teal",    0xFF808000u },
    { "silver",  0xFFC0C0C0u },
    { "gold",    0xFF00D7FFu },
};
// clang-format on
auto hex_nib(char c) -> uint8_t
{
    if(c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if(c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if(c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return 0;
}
auto parse_color(hpp::string_view s) -> uint32_t
{
    // 1) hash-hex
    if(!s.empty() && s[0] == '#')
    {
        auto nib = [&](char c)
        {
            if(c >= '0' && c <= '9')
            {
                return uint8_t(c - '0');
            }
            if(c >= 'a' && c <= 'f')
            {
                return uint8_t(c - 'a' + 10);
            }
            if(c >= 'A' && c <= 'F')
            {
                return uint8_t(c - 'A' + 10);
            }
            return uint8_t(0);
        };

        const char* p = s.data() + 1;
        uint8_t r = nib(p[0]) << 4 | nib(p[1]);
        uint8_t g = nib(p[2]) << 4 | nib(p[3]);
        uint8_t b = nib(p[4]) << 4 | nib(p[5]);
        uint8_t a = (s.size() == 9) ? (nib(p[6]) << 4 | nib(p[7])) : 0xFFu;

        // construct via your color struct…
        math::color col(r, g, b, a);
        return static_cast<uint32_t>(col);
    }

    // 2) named lookup
    for(auto& kv : k_named_colors)
    {
        if(kv.first == s)
        {
            return kv.second;
        }
    }

    // 3) fallback: opaque white
    return static_cast<uint32_t>(math::color::white());
}

// -------------------------------------------------
// 2) parse segments (unchanged)
// -------------------------------------------------

// Splits `in` into a sequence of RichSegments, respecting nested tags

auto parse_rich_segments(const hpp::string_view& in, const text_style& main_style, bool is_rich) -> segment_list
{
    segment_list out;
    if(!is_rich)
    {
        out.emplace_back(rich_segment{.text = in, .state = {.style = main_style}});
        return out;
    }

    // Stack of (state, tag_name) so we can pop by name
    text_vector<std::pair<rich_state, hpp::string_view>> open_tags;
    open_tags.reserve(16);
    open_tags.emplace_back(rich_state{.style = main_style}, hpp::string_view{}); // base

    size_t pos = 0, text_start = 0, len = in.size();
    while(pos < len)
    {
        // 1) Find next '<'
        size_t open = in.find('<', pos);
        if(open == hpp::string_view::npos)
        {
            break;
        }

        // 2) Emit text before it
        if(open > text_start)
        {
            out.push_back({hpp::string_view(in.data() + text_start, open - text_start), open_tags.back().first});
        }

        // 3) Try to find matching '>'
        size_t close = in.find('>', open + 1);
        if(close == hpp::string_view::npos)
        {
            // No closing '>' → treat this '<' as literal
            out.push_back({.text=hpp::string_view(in.data() + open, 1), .state=open_tags.back().first});
            pos = open + 1;
            text_start = pos;
            continue;
        }

        // 4) If there's another '<' before that '>', it's not a tag
        size_t stray = in.find('<', open + 1);
        if(stray != hpp::string_view::npos && stray < close)
        {
            // treat the first '<' as literal
            out.push_back({.text = hpp::string_view(in.data() + open, 1), .state = open_tags.back().first});
            pos = open + 1;
            text_start = pos;
            continue;
        }

        // 5) We have a well-formed tag [open..close]
        auto inner = in.substr(open + 1, close - open - 1);
        pos = close + 1;
        text_start = pos;

        if(inner.empty())
        {
            // "<>" → literal
            out.push_back({.text = hpp::string_view(in.data() + open, 2), .state = open_tags.back().first});
            continue;
        }

        // 6) Closing tag?
        if(inner[0] == '/')
        {
            bool found = false;
            auto name = inner.substr(1);
            if(!name.empty())
            {
                // Pop the last matching tag by name
                for(auto it = open_tags.rbegin(); it != open_tags.rend(); ++it)
                {
                    if(it->second == name)
                    {
                        open_tags.erase(std::next(it).base());
                        found = true;
                        break;
                    }
                }
            }
            if(!found)
            {
                // emit literal "</...>"
                out.push_back(
                    {.text = hpp::string_view(in.data() + open, close - open + 1), .state = open_tags.back().first});
            }
            continue;
        }

        // 7) Opening/inline tag
        size_t eq = inner.find('=');
        hpp::string_view key = eq == hpp::string_view::npos ? inner : inner.substr(0, eq);
        hpp::string_view val = eq == hpp::string_view::npos ? hpp::string_view{} : inner.substr(eq + 1);

        rich_state ns = open_tags.back().first;
        bool recognized = true;

        if(key == "color")
        {
            ns.style.text_color = parse_color(val);
        }
        else if(key == "alpha" || key == "opacity")
        {
            // parse as float 0.0–1.0, defaulting to 1
            float f = safe_parse_float(val, 1.0f);
            f = math::clamp(f, 0.0f, 1.0f);
            ns.style.opacity *= f;
        }
        else if(key == "background-color" || key == "bgcolor")
        {
            ns.style.background_color = parse_color(val);
            ns.style.style_flags |= gfx::style_background;
        }
        else if(key == "foreground-color" || key == "fgcolor")
        {
            ns.style.foreground_color = parse_color(val);
            ns.style.style_flags |= gfx::style_foreground;
        }
        else if(key == "overline-color")
        {
            ns.style.overline_color = parse_color(val);
            ns.style.style_flags |= gfx::style_overline;
        }
        else if(key == "overline" || key == "o")
        {
            ns.style.overline_color = ns.style.text_color;
            ns.style.style_flags |= gfx::style_overline;
        }
        else if(key == "underline-color")
        {
            ns.style.underline_color = parse_color(val);
            ns.style.style_flags |= gfx::style_underline;
        }
        else if(key == "underline" || key == "u")
        {
            ns.style.underline_color = ns.style.text_color;
            ns.style.style_flags |= gfx::style_underline;
        }
        else if(key == "strikethrough-color" || key == "strike-color")
        {
            ns.style.strike_color = parse_color(val);
            ns.style.style_flags |= gfx::style_strike_through;
        }
        else if(key == "strikethrough" || key == "s")
        {
            ns.style.strike_color = ns.style.text_color;
            ns.style.style_flags |= gfx::style_strike_through;
        }
        else if(key == "outline-width")
        {
            ns.style.outline_width = safe_parse_float(val);
        }
        else if(key == "outline-color")
        {
            ns.style.outline_color = parse_color(val);
        }
        else if(key == "shadow-offset" || key == "drop-shadow-offset")
        {
            std::string tmp(val);
            std::replace(tmp.begin(), tmp.end(), ',', ' ');
            std::istringstream ss(tmp);
            ss >> ns.style.shadow_offsets.x >> ns.style.shadow_offsets.y;
        }
        else if(key == "shadow-color" || key == "drop-shadow-color")
        {
            ns.style.shadow_color = parse_color(val);
        }
        else if(key == "shadow-softener" || key == "drop-shadow-softener")
        {
            ns.style.shadow_softener = safe_parse_float(val);
        }
        else if(key == "nobr")
        {
            ns.no_break = true;
        }
        else if(key == "style")
        {
            uint32_t f = 0;
            size_t p = 0;
            while(p < val.size())
            {
                auto c = val.find_first_of("|,", p);
                auto sub = val.substr(p, c == hpp::string_view::npos ? hpp::string_view::npos : c - p);
                if(sub == "underline")
                {
                    f |= gfx::style_underline;
                }
                else if(sub == "overline")
                {
                    f |= gfx::style_overline;
                }
                else if(sub == "strikethrough" || sub == "strike")
                {
                    f |= gfx::style_strike_through;
                }
                else if(sub == "background")
                {
                    f |= gfx::style_background;
                }
                else if(sub == "foreground")
                {
                    f |= gfx::style_foreground;
                }
                if(c == hpp::string_view::npos)
                {
                    break;
                }
                p = c + 1;
            }
            ns.style.style_flags = f;
        }
        else
        {
            // unrecognized → emit literally
            out.push_back(
                {.text = hpp::string_view(in.data() + open, close - open + 1), .state = open_tags.back().first});
            continue;
        }

        // 8) push new tag state
        open_tags.emplace_back(ns, key);
    }

    // 9) Emit any trailing text
    if(text_start < len)
    {
        out.push_back(
            {.text = hpp::string_view(in.data() + text_start, len - text_start), .state = open_tags.back().first});
    }

    return out;
}

void measure_all_widths(text_vector<word_frag>& frags, const scaled_font& base_font)
{
    for(auto& f : frags)
    {
        text_metrics m;
        m.metrics.append_text(base_font.handle, f.txt.data(), f.txt.data() + f.txt.size());
        f.base_width = m.metrics.get_width();
        f.scaled_width = f.base_width;
    }
}

auto measure_line_width(segment_list& frags, const scaled_font& base_font) -> float
{
    float w = 0.0f;
    for(auto& f : frags)
    {
        text_metrics m;
        m.metrics.append_text(base_font.handle, f.text.data(), f.text.data() + f.text.size());
        w += m.metrics.get_width();
    }

    return w;
}

auto measure_text_width(const hpp::string_view& txt, const scaled_font& base_font) -> float
{
    text_metrics m;
    m.metrics.append_text(base_font.handle, txt.data(), txt.data() + txt.size());
    return m.metrics.get_width();
}

struct linebreak_ctx
{
    const segment_list* segments;
    const text_vector<size_t>* offsets; // size = segments.size()+1
    size_t total_len;                   // = offsets.back()
};
// -------------------------------------------------
// 1) Build a small context object holding your fragments
// -------------------------------------------------
// context passed into set_linebreaks

// — your callback from before —
auto get_next_char_frag(const void* ctx_void,
                        size_t /*len*/,
                        size_t* ip // in/out byte‐offset into the whole virtual stream
                        ) -> utf32_t
{
    auto const* ctx = static_cast<const linebreak_ctx*>(ctx_void);
    size_t pos = *ip;
    if(pos >= ctx->total_len)
    {
        return EOS;
    }

    auto& offsets = *ctx->offsets;
    auto& segments = *ctx->segments;

    // figure out which segment contains byte 'pos'
    auto it = std::upper_bound(offsets.begin(), offsets.end(), pos);
    size_t seg_idx = (it - offsets.begin()) - 1;
    size_t seg_start = offsets[seg_idx];
    auto const& txt = segments[seg_idx].text;

    size_t local_ip = pos - seg_start;
    assert(local_ip <= txt.size());

    auto txt_data = txt.data();
    auto txt_size = txt.size();

    // delegate UTF-8 decoding:
    utf32_t cp = ub_get_next_char_utf8(reinterpret_cast<const utf8_t*>(txt_data), txt_size, &local_ip);

    // advance global position
    *ip = seg_start + local_ip;
    return cp;
}

auto tokenize_fragments_and_measure(const segment_list& segments,
                                    text_component::overflow_type type,
                                    const scaled_font& font,
                                    scratch_cache& cache) -> fragment_list&
{
    // fragment_list frags;
    auto& frags = cache.frags;
    frags.clear();
    frags.reserve(segments.size() * 4);

    cache.offsets.resize(segments.size() + 1);
    cache.offsets[0] = 0;
    for(size_t i = 0; i < segments.size(); ++i)
    {
        cache.offsets[i + 1] = cache.offsets[i] + segments[i].text.size();
    }

    // 1) build offsets[] and compute total length
    linebreak_ctx ctx;
    ctx.segments = &segments;
    ctx.offsets = &cache.offsets;
    ctx.total_len = cache.offsets.back();

    // 2) allocate global break‐map
    cache.lb.resize(ctx.total_len);

    if(type == text_component::overflow_type::grapheme)
    {
        cache.wb.resize(ctx.total_len);
    }

    // 3) ask libunibreak to fill it, using our callback
    set_linebreaks(&ctx,
                   ctx.total_len,
                   /*lang=*/nullptr,
                   LBOT_PER_CODE_UNIT,
                   cache.lb.data(),
                   get_next_char_frag);

    // 4) If grapheme‐mode, also fill grapheme map
    if(type == text_component::overflow_type::grapheme)
    {
        set_graphemebreaks(&ctx, ctx.total_len, cache.wb.data(), get_next_char_frag);
    }

    // 5) now scan each segment exactly as before, but consult cache.brks[global_i]
    for(size_t seg_i = 0; seg_i < segments.size(); ++seg_i)
    {
        auto const& seg = segments[seg_i];
        auto const& state = seg.state;
        auto const& s = seg.text;
        size_t const base = cache.offsets[seg_i];
        size_t n = s.size();

        size_t start = 0;
        while(start < n)
        {
            // scan forward looking for next break point
            size_t idx = start;
            bool found_lb = false;
            bool found_wb = false;
            size_t break_end = 0;

            while(idx < n)
            {
                // decode one code‐unit
                unsigned char b0 = s[idx];
                size_t cp_len = (b0 < 0x80 ? 1 : (b0 < 0xE0 ? 2 : (b0 < 0xF0 ? 3 : 4)));
                if(idx + cp_len > n)
                {
                    cp_len = n - idx;
                }

                size_t global_cp_end = base + idx + cp_len;
                char lbv = cache.lb[global_cp_end - 1];
                if(lbv == LINEBREAK_MUSTBREAK)
                {
                    found_lb = true;

                    break_end = idx + cp_len;
                    break;
                }
                if(type == text_component::overflow_type::word)
                {
                    if(lbv == LINEBREAK_ALLOWBREAK)
                    {
                        found_wb = true;
                        break_end = idx + cp_len;
                        break;
                    }
                }
                else if(type == text_component::overflow_type::grapheme)
                {
                    char wbv = cache.wb[global_cp_end - 1];
                    if(wbv == GRAPHEMEBREAK_BREAK)
                    {
                        found_wb = true;
                        break_end = idx + cp_len;
                        break;
                    }
                }

                idx += cp_len;
            }

            size_t frag_len = 0;
            break_type brk = break_type::allowbreak;

            if(!found_lb && !found_wb)
            {
                // no more breaks → emit the tail
                frag_len = n - start;
                brk = break_type::nobreak;

                // extract slice
                std::string_view slice(s.data() + start, frag_len);
                std::string_view break_slice(s.data() + start + frag_len, 0);

                auto w = measure_text_width(slice, font);
                frags.push_back({slice, break_slice, state, brk, w, w});

                start = n;
            }
            else
            {
                // need to backtrack to codepoint boundary
                size_t cp0 = break_end - 1;
                while(cp0 > start && (uint8_t(s[cp0]) & 0xC0) == 0x80)
                {
                    --cp0;
                }

                if(found_lb)
                {
                    // drop the break code-point itself
                    frag_len = cp0 - start;
                    brk = break_type::mustbreak;
                }
                else // foundWB
                {
                    // include the break code-point
                    frag_len = break_end - start;
                }

                // extract slice
                std::string_view slice(s.data() + start, frag_len);
                std::string_view break_slice(s.data() + start + frag_len, break_end - (start + frag_len));

                auto w = measure_text_width(slice, font);
                frags.push_back({slice, break_slice, state, brk, w, w});

                start = break_end;
            }
        }
    }

    return frags;
}

// --------------------------------------------------------------------
//  A) Given the raw total height (n·line_h), subtract off the extra
//     leading above the capline and the extra descent below the baseline.
//     That gives you the distance from capline…baseline.
// --------------------------------------------------------------------
auto compute_typographic_height(float total_h, float above_capline, float below_baseline, uint32_t alignment) -> float
{
    bool typographic = (alignment & align::typographic_mask) != 0;
    if(!typographic)
    {
        return total_h;
    }

    // remove the extra space above the first capline and below the last baseline
    return total_h - (above_capline + below_baseline);
}

auto apply_typographic_adjustment(float total_h, float scale, const scaled_font& fnt, uint32_t alignment) -> float
{
    const auto& info = fnt.get_info();
    float above_capline = info.ascender - info.capline;
    ;
    float below_baseline = -info.descender;

    return compute_typographic_height(total_h, above_capline * scale, below_baseline * scale, alignment);
}

// --------------------------------------------
// helper: merge into same-state run
// --------------------------------------------
void merge_into_line(segment_list& line, const word_frag& f)
{
    if(!line.empty() && can_batch_with(line.back().state.style, f.state.style) &&
       line.back().text.data() + line.back().text.size() == f.txt.data())
    {
        // extend previous
        auto& back = line.back();
        back.text = {back.text.data(), back.text.size() + f.txt.size()};
    }
    else
    {
        line.push_back({f.txt, f.state});
    }
}

// -------------------------------------------------
// 4) scale + wrap, store per‐line width
// -------------------------------------------------

auto wrap_fragments(const fragment_list& frags, float max_width_px, scratch_cache& cache) -> text_layout
{
    auto& atoms = cache.atoms;
    atoms.clear();
    atoms.reserve(frags.size());

    {
        frag_atom cur;
        cur.parts.reserve(4);
        for(auto const& f : frags)
        {
            cur.parts.push_back(f);
            cur.width += f.scaled_width;
            if(f.brk == break_type::allowbreak || f.brk == break_type::mustbreak)
            {
                cur.brk = f.brk;
                atoms.push_back(std::move(cur));
                cur = {};
                cur.parts.reserve(4);
            }
        }
        if(!cur.parts.empty())
        {
            cur.brk = break_type::allowbreak;
            atoms.push_back(std::move(cur));
        }
    }

    // --- Greedy line‐fitting of atoms ---
    text_layout lines;
    lines.reserve(atoms.size());

    wrapped_line cur_line;
    cur_line.segments.reserve(16);
    float cur_w = 0;

    for(auto& atom : atoms)
    {
        // (1) If this atom would overflow, flush current line first:
        if(cur_w + atom.width > max_width_px && !cur_line.segments.empty())
        {
            cur_line.width = cur_w;
            lines.push_back(std::move(cur_line));
            cur_line = {};
            cur_line.segments.reserve(16);
            cur_w = 0;
        }

        // (2) Append the atom:
        for(auto& frag : atom.parts)
        {
            merge_into_line(cur_line.segments, frag);
        }
        cur_w += atom.width;

        // (3) If it was a forced break, now flush:
        if(atom.brk == break_type::mustbreak)
        {
            cur_line.width = cur_w;
            if(!atom.parts.empty())
            {
                cur_line.brk_symbol = atom.parts.back().brk_symbol;
            }
            lines.push_back(std::move(cur_line));
            cur_line = {};
            cur_line.segments.reserve(16);
            cur_w = 0;
        }
    }

    // --- Final flush ---
    if(!cur_line.segments.empty())
    {
        cur_line.width = cur_w;
        lines.push_back(std::move(cur_line));
    }

    return lines;
}
// -------------------------------------------------
// 5) top-level API: one tokenize + one measure + O(log N) cheap wraps
//    reuses the last “good” layout instead of recomputing
// -------------------------------------------------
auto wrap_lines(text_component::overflow_type type,
                uint32_t alignment,
                const segment_list& segments,
                scratch_cache& cache,
                uint32_t& calculated_font_size, // out param
                const asset_handle<font>& font,
                const urange32_t& auto_size_range,
                float bound_w_px,
                float bound_h_px) -> text_layout
{
    // a) measure at base size
    uint32_t base_size = auto_size_range.min;
    auto base_font = font.get()->get_scaled_font(base_size);

    // b) tokenize fragments and measure
    auto& frags = tokenize_fragments_and_measure(segments, type, *base_font, cache);

    text_layout best_layout = wrap_fragments(frags, bound_w_px, cache);
    uint32_t best = base_size;

    // c) binary search…
    uint32_t lo = base_size + 1, hi = auto_size_range.max;
    while(lo <= hi)
    {
        uint32_t mid = (lo + hi) >> 1;
        float scale = float(mid) / float(base_size);

        // scale every frag
        for(auto& f : frags)
        {
            f.scaled_width = f.base_width * scale;
        }

        // wrap at this scale
        auto layout_mid = wrap_fragments(frags, bound_w_px, cache);

        // vertical fit?
        float total_h = layout_mid.size() * (base_font->get_line_height() * scale);
        total_h = apply_typographic_adjustment(total_h, scale, *base_font, alignment);

        if(total_h > bound_h_px)
        {
            hi = mid - 1;
            continue;
        }

        // horizontal fit?
        bool ok_h = true;
        for(auto& wl : layout_mid)
        {
            if(wl.width > bound_w_px)
            {
                ok_h = false;
                break;
            }
        }

        if(ok_h)
        {
            // success: record and try larger
            best = mid;
            best_layout = std::move(layout_mid);
            lo = mid + 1;
        }
        else
        {
            // too wide: shrink
            hi = mid - 1;
        }
    }

    // d) emit
    calculated_font_size = best;
    return best_layout;
}

// tokenize + measure at one fixed font size, then wrap
auto wrap_fixed_size(text_component::overflow_type type,
                     const segment_list& segments,
                     scratch_cache& cache,
                     const scaled_font& font,
                     float max_width_px) -> text_layout
{
    // 1) tokenize & base-measure
    auto& frags = tokenize_fragments_and_measure(segments, type, font, cache);

    // 2) single greedy wrap (width + must_break)
    return wrap_fragments(frags, max_width_px, cache);
}

// --------------------------------------------------------------------
// Compute the Y-offset (pen_y for the first line) so that the
//     block of text (either its full total_h or its usable height)
//     is positioned according to your chosen alignment.
// --------------------------------------------------------------------
auto compute_vertical_offset(uint32_t alignment,
                             float bounds_h_m,
                             float total_h,
                             float above_capline,
                             float below_baseline) -> float
{
    float bounds_h_px = bounds_h_m * PIXELS_PER_METER;

    float usable_h = compute_typographic_height(total_h, above_capline, below_baseline, alignment);

    float offset_px{};
    switch(alignment & align::vertical_text_mask)
    {
        case align::top:
            return 0.0f;
        case align::middle:
            return (bounds_h_px - total_h) * 0.5f;
        case align::bottom:
            return (bounds_h_px - total_h);
        case align::capline:
            return -above_capline;
        case align::midline:
            return -above_capline + (bounds_h_px - usable_h) * 0.5f;
        case align::baseline:
            return below_baseline + (bounds_h_px - total_h);
        default:
            return 0.0f;
    }
}
// Compute horizontal offset (left, center, right), converting bounds from meters to pixels then back.
auto compute_horizontal_offset(uint32_t alignment, float bounds_width_m, float line_width_px) -> float
{
    float bounds_width_px = bounds_width_m * PIXELS_PER_METER;
    float offset_px{};
    switch(alignment & (align::horizontal_mask))
    {
        case align::center:
            offset_px = (bounds_width_px - line_width_px) * 0.5f;
            break;
        case align::right:
            offset_px = (bounds_width_px - line_width_px);
            break;
        default:
            offset_px = 0.0f;
            break; // Left
    }
    return offset_px;
}
} // namespace

void text_component::set_text(const std::string& text)
{
    if(text_ == text)
    {
        return;
    }
    text_ = text;
    text_dirty_ = true;
}

auto text_component::get_text() const -> const std::string&
{
    return text_;
}

void text_component::set_style(const text_style& style)
{
    if(style_ == style)
    {
        return;
    }
    style_ = style;
    text_dirty_ = true;
}

auto text_component::get_style() const -> const text_style&
{
    return style_;
}

void text_component::set_buffer_type(const buffer_type& type)
{
    if(type_ == type)
    {
        return;
    }
    type_ = type;
    text_dirty_ = true;
}
auto text_component::get_buffer_type() const -> const buffer_type&
{
    return type_;
}

void text_component::set_overflow_type(const overflow_type& type)
{
    if(overflow_type_ == type)
    {
        return;
    }
    overflow_type_ = type;
    text_dirty_ = true;
}
auto text_component::get_overflow_type() const -> const overflow_type&
{
    return overflow_type_;
}

void text_component::set_font(const asset_handle<font>& font)
{
    if(font_ == font && font_version_ == font.link_version())
    {
        return;
    }
    font_ = font;

    scaled_font_dirty_ = true;
}

auto text_component::get_font() const -> const asset_handle<font>&
{
    return font_;
}

auto text_component::get_scaled_font() const -> const scaled_font&
{
    if(font_.link_version() != font_version_ || scaled_font_dirty_)
    {
        recreate_scaled_font();
    }

    if(scaled_font_)
    {
        return *scaled_font_;
    }

    static const scaled_font empty;
    return empty;
}

auto text_component::get_builder() const -> text_buffer_builder&
{
    bool dirty = text_dirty_ || scaled_font_dirty_;
    // nothing to do if clean or font isn’t ready
    if(!dirty || !get_scaled_font().is_valid())
    {
        return *builder_;
    }

    // APPLOG_INFO_PERF(std::chrono::microseconds);
    uint32_t alignment = align_.flags;

    auto buf_type =
        (type_ == buffer_type::static_buffer)
            ? gfx::buffer_type::Static
            : (type_ == buffer_type::dynamic_buffer ? gfx::buffer_type::Dynamic : gfx::buffer_type::Transient);

    // 1) parse rich segments once
    auto segments = parse_rich_segments(text_, style_, is_rich_);

    // 2) compute our pixel bounds
    float bound_w = area_.width * PIXELS_PER_METER;
    float bound_h = area_.height * PIXELS_PER_METER;

    text_layout& layout = scratch_.layout;

    if(!auto_size_)
    {
        // --- NO AUTO-FIT PATH ---
        // pick the fixed font size:
        calculated_font_size_ = font_size_;
        scaled_font_ = font_.get()->get_scaled_font(calculated_font_size_);
        auto& fixed_font = *scaled_font_;

        layout = wrap_fixed_size(overflow_type_, segments, scratch_, fixed_font, bound_w);
    }
    else
    {
        // 3) run the unified wrap+auto-fit routine
        //    this will:
        //      * tokenize & base-measure at auto_size_range_.min
        //      * binary-search the best size in [min..max]
        //      * return a text_layout with each line’s .width already set
        layout = wrap_lines(overflow_type_,
                            alignment,
                            segments,
                            scratch_,
                            calculated_font_size_,
                            font_,
                            auto_size_font_range_,
                            bound_w,
                            bound_h);
        // wrap_lines will update calculated_font_size_ for you
        scaled_font_ = font_.get()->get_scaled_font(calculated_font_size_);
    }

    auto& final_font = *scaled_font_;

    // 4) compute vertical offset once
    const auto& info = final_font.get_info();
    float line_h = final_font.get_line_height();
    float above_capline = info.ascender - info.capline;
    float below_baseline = -info.descender;

    float total_h = float(layout.size()) * line_h;
    float offset_y = compute_vertical_offset(alignment, area_.height, total_h, above_capline, below_baseline);

    // 5) lay out each line
    float pen_y = offset_y;

    // 0) clear out old buffers
    builder_->destroy_buffers();
    debug_builder_->destroy_buffers();

    rich_segment* last_segment{};
    for(auto& wl : layout)
    {
        float offset_x = compute_horizontal_offset(alignment, area_.width, wl.width);
        float pen_x = offset_x;

        for(auto& seg : wl.segments)
        {
            bool create_new = !(last_segment && can_batch_with(last_segment->state.style, seg.state.style));
            if(create_new)
            {
                auto buf = builder_->manager.create_text_buffer(FONT_TYPE_DISTANCE_OUTLINE_DROP_SHADOW_IMAGE, buf_type);
                builder_->buffers.push_back({buf});
            }

            auto& buf = builder_->buffers.back().handle;
            apply_style(builder_->manager, buf, seg.state.style);
            builder_->manager.set_apply_kerning(buf, apply_kerning_);
            builder_->manager.set_pen_origin(buf, offset_x, offset_y);
            builder_->manager.set_pen_position(buf, pen_x, pen_y);
            builder_->manager.append_text(buf, final_font.handle, seg.text.data(), seg.text.data() + seg.text.size());
            builder_->manager.get_pen_position(buf, &pen_x, &pen_y);

            last_segment = &seg;
        }

        pen_y += line_h;
    }

    // {
    //     debug_builder_->destroy_buffers();
    //     auto buf = debug_builder_->manager.create_text_buffer(FONT_TYPE_DISTANCE, buf_type);

    //     debug_builder_->manager.set_background_color(buf, 0xffffffff);
    //     for(size_t i = 0; i < 6; ++i)
    //     {
    //         debug_builder_->manager.append_atlas_face(buf, i);
    //     }
    //     debug_builder_->buffers.push_back({buf});
    // }

    // 6) mark clean
    text_dirty_ = false;
    scaled_font_dirty_ = false;
    return *builder_;
}

void text_component::set_font_size(uint32_t font_size)
{
    if(font_size_ == font_size)
    {
        return;
    }
    font_size_ = font_size;

    scaled_font_dirty_ = true;
}

auto text_component::get_font_size() const -> uint32_t
{
    return font_size_;
}

void text_component::set_auto_size(bool auto_size)
{
    if(auto_size_ == auto_size)
    {
        return;
    }

    auto_size_ = auto_size;
    text_dirty_ = true;
}

auto text_component::get_auto_size() const -> bool
{
    return auto_size_;
}

auto text_component::get_render_font_size() const -> uint32_t
{
    return calculated_font_size_;
}

void text_component::set_is_rich_text(bool is_rich)
{
    if(is_rich_ == is_rich)
    {
        return;
    }

    is_rich_ = is_rich;
    text_dirty_ = true;
}

auto text_component::get_is_rich_text() const -> bool
{
    return is_rich_;
}

void text_component::set_apply_kerning(bool apply_kerning)
{
    if(apply_kerning_ == apply_kerning)
    {
        return;
    }

    apply_kerning_ = apply_kerning;
    text_dirty_ = true;
}

auto text_component::get_apply_kerning() const -> bool
{
    return apply_kerning_;
}

void text_component::set_alignment(const alignment& align)
{
    if(align_.flags == align.flags)
    {
        return;
    }

    align_ = align;
    text_dirty_ = true;
}

auto text_component::get_alignment() const -> const alignment&
{
    return align_;
}

void text_component::set_area(const fsize_t& area)
{
    if(area_ == area)
    {
        return;
    }

    area_ = area;
    text_dirty_ = true;
}

auto text_component::get_area() const -> const fsize_t&
{
    return area_;
}

void text_component::set_auto_size_range(const urange32_t& range)
{
    if(auto_size_font_range_ == range)
    {
        return;
    }

    auto_size_font_range_ = range;
    text_dirty_ = true;
}

auto text_component::get_auto_size_range() const -> const urange32_t&
{
    return auto_size_font_range_;
}

auto text_component::get_bounds() const -> math::bbox
{
    const auto& area = get_area();
    math::bbox bbox;
    bbox.min.x = -area.width * 0.5f;
    bbox.min.y = area.height * 0.5f;
    bbox.min.z = 0;

    bbox.max.x = area.width * 0.5f;
    bbox.max.y = -area.height * 0.5f;
    bbox.max.z = 0.001f;

    return bbox;
}

auto text_component::get_render_bounds() const -> math::bbox
{
    const auto& area = get_render_area();
    math::bbox bbox;
    bbox.min.x = -area.width * 0.5f;
    bbox.min.y = area.height * 0.5f;
    bbox.min.z = 0;

    bbox.max.x = area.width * 0.5f;
    bbox.max.y = -area.height * 0.5f;
    bbox.max.z = 0.001f;

    return bbox;
}

auto text_component::get_render_buffers_count() const -> size_t
{
    return get_builder().buffers.size();
}

auto text_component::get_lines(bool include_breaks) const -> text_vector<text_line>
{
    auto& builder = get_builder();
    auto& layout = scratch_.layout;

    text_vector<text_line> lines;
    lines.reserve(layout.size());
    for(const auto& layout_line : layout)
    {
        auto& line = lines.emplace_back();

        for(auto& seg : layout_line.segments)
        {
            line.line += std::string(seg.text);
        }

        if(include_breaks)
        {
            line.break_symbol = std::string(layout_line.brk_symbol);
        }
    }

    return lines;
}

auto text_component::meters_to_px(float meters) const -> float
{
    return meters * PIXELS_PER_METER;
}
auto text_component::px_to_meters(float px) const -> float
{
    return px * METERS_PER_PIXEL;
}

auto text_component::can_be_rendered() const -> bool
{
    const auto& font = get_scaled_font();
    const auto font_scaled_size = get_font_size();

    return font_scaled_size > 0 && font.is_valid();
}

auto text_component::get_render_area() const -> fsize_t
{
    const auto& builder = get_builder();
    fsize_t result;
    for(auto& sb : builder.buffers)
    {
        auto r = builder.manager.get_rectangle(sb.handle);

        result.width = std::max(result.width, r.width);
        result.height = std::max(result.height, r.height);
    }

    auto area = get_area();
    result.width = std::max(result.width * METERS_PER_PIXEL, area.width);
    result.height = std::max(result.height * METERS_PER_PIXEL, area.height);
    return result;
}

void text_component::recreate_scaled_font() const
{
    font_version_ = font_.link_version();

    if(!font_)
    {
        scaled_font_.reset();
        return;
    }
    auto font = font_.get();
    if(!font)
    {
        scaled_font_.reset();
        return;
    }
    scaled_font_ = font->get_scaled_font(get_font_size());
}

void text_component::submit(gfx::view_id id, const math::transform& world, uint64_t state)
{
    if(!can_be_rendered())
    {
        return;
    }
    auto fit_m = get_area();
    float fit_px_w = fit_m.width * PIXELS_PER_METER;
    float fit_px_h = fit_m.height * PIXELS_PER_METER;

    math::transform pivot;
    pivot.translate(-fit_px_w * 0.5f, -fit_px_h * 0.5f);

    static const auto unit_scale = math::transform::scaling({METERS_PER_PIXEL, -METERS_PER_PIXEL, 1.0f});

    auto text_transform = world * unit_scale * pivot;
    auto& builder = get_builder();

    const auto& font = get_scaled_font();

    for(auto& sb : builder.buffers)
    {
        gfx::set_transform((const float*)text_transform);
        builder.manager.submit_text_buffer(sb.handle, font.handle, id, state);
    }

    for(auto& sb : debug_builder_->buffers)
    {
        gfx::set_transform((const float*)text_transform);
        debug_builder_->manager.submit_text_buffer(sb.handle, font.handle, id, state);
    }
}
} // namespace unravel
