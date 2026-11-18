#pragma once

#include <engine/settings/target_platform.h>

#include <hpp/optional.hpp>
#include <hpp/string_view.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

/// The project audit: what it reports and how a report is shaped. A report is plain data, readable on any
/// thread and saved as JSON.
namespace unravel::audit
{

/// How much an issue matters, from the least.
enum class severity : std::uint8_t
{
    /// A suggestion: a cost that depends on how the game uses the subject, or a tidier way to do the same.
    info,
    /// A measurable cost or a likely mistake.
    warning,
    /// Broken content, or something that keeps the project from deploying.
    error,
};

/// What an issue costs. An issue names one or more.
enum class area : std::uint16_t
{
    none = 0,
    /// The game does not behave as authored, or cannot ship.
    correctness = 1 << 0,
    /// Processor time each frame.
    cpu = 1 << 1,
    /// Graphics processor time each frame.
    gpu = 1 << 2,
    /// Memory while the game runs.
    memory = 1 << 3,
    /// The size of the build players download.
    download = 1 << 4,
    /// The time a scene or the game takes to load.
    load_time = 1 << 5,
    /// How the game looks or sounds.
    quality = 1 << 6,
};

/// Areas as a set of bits.
using area_mask = std::uint16_t;

/// One area as a set.
constexpr auto to_mask(area value) -> area_mask
{
    return static_cast<area_mask>(value);
}

constexpr auto operator|(area lhs, area rhs) -> area_mask
{
    return static_cast<area_mask>(static_cast<area_mask>(lhs) | static_cast<area_mask>(rhs));
}

constexpr auto operator|(area_mask lhs, area rhs) -> area_mask
{
    return static_cast<area_mask>(lhs | static_cast<area_mask>(rhs));
}

constexpr auto has_area(area_mask mask, area value) -> bool
{
    return (mask & static_cast<area_mask>(value)) != 0;
}

/// Every area, in the order the editor lists them.
constexpr std::array<area, 7> ALL_AREAS{area::correctness,
                                        area::cpu,
                                        area::gpu,
                                        area::memory,
                                        area::download,
                                        area::load_time,
                                        area::quality};

/// What kind of thing an issue is about.
enum class subject_kind : std::uint8_t
{
    /// The project as a whole.
    project,
    /// An asset, by key ("app:/data/textures/wall.png").
    asset,
    /// A project or profile setting, by path ("graphics/limits/global_illumination").
    setting,
    /// A place in a script: the file's key, the member and the line.
    script,
    /// An entity of a scene or prefab: the document's key and the entity's path in it.
    entity,
    /// A settings profile, by name.
    profile,
};

/// Where an issue is.
struct subject
{
    subject_kind kind{subject_kind::project};
    /// The asset or document key, the settings path, the script's key, or the profile's name.
    std::string key;
    /// The entity's uid in its document, the script member ("Player.OnUpdate"), or the profile's content rule;
    /// empty when the key says it all.
    std::string detail;
    /// What the editor shows for the detail: an entity's path in its document. Empty when the detail reads as it is.
    std::string label;
    /// The script line; 0 when none.
    std::uint32_t line{};

    /// The subject as an issue id names it: the kind, the key and the detail, without the line, which moves as a
    /// script is edited.
    auto make_id() const -> std::string;

    friend auto operator==(const subject& lhs, const subject& rhs) -> bool = default;
};

/// Which part of the project a rule reads.
enum class rule_source : std::uint8_t
{
    assets,
    settings,
    content,
    scenes,
    scripts,
    code,
};

/// The platforms a threshold has a value for, in target_platform order.
constexpr std::size_t PLATFORM_COUNT = 3;

/// A number a rule compares against, with a value for each platform.
struct threshold
{
    /// The name the config file and the tools use ("max_size").
    std::string name;
    /// The label the editor shows ("Largest side").
    std::string label;
    /// What the number counts ("pixels", "bytes", "triangles", "Hz").
    std::string unit;
    /// What the threshold decides, in a sentence.
    std::string description;
    /// The value for desktop, android and web.
    std::array<std::int64_t, PLATFORM_COUNT> values{};
};

/// One check the audit makes, as the editor and the tools describe it.
struct rule
{
    /// A stable id ("texture.uncompressed"): ignores, configs and reports name rules by it.
    std::string id;
    /// A short title in title case.
    std::string title;
    severity default_severity{severity::warning};
    area_mask areas{};
    rule_source source{rule_source::assets};
    /// What the rule finds and why it matters, in sentences.
    std::string description;
    /// What to do about it, in sentences.
    std::string recommendation;
    std::vector<threshold> thresholds;
    /// Whether its findings depend on the settings profile.
    bool per_profile{false};
};

/// One finding.
struct issue
{
    /// The rule's id.
    std::string rule;
    severity level{severity::warning};
    area_mask areas{};
    subject where;
    /// Tells two findings of one rule on one subject apart (the API a script calls); empty when one is enough.
    std::string discriminator;
    /// The finding, in sentences, with the numbers it was judged by.
    std::string message;
    /// More facts, one sentence each: each profile's own numbers when they differ, what an engine call allocates.
    std::vector<std::string> details;
    /// Every line of the subject the finding is on, in order; `where.line` is the first. Empty when it has none.
    std::vector<std::uint32_t> lines;
    /// How the code is reached: from the update method or coroutine the engine calls to the method with the
    /// finding, each a "Type.Method". Empty for findings that are not in code.
    std::vector<std::string> call_path;
    /// The profiles it applies to; empty when it does not depend on one.
    std::vector<std::string> profiles;
    /// The fix the editor can apply, by id; empty when none.
    std::string fix;
    /// Whether the project's audit settings ignore it, and why.
    bool is_ignored{false};
    std::string ignore_reason;

    /// The stable id ignores and reports name it by: rule, subject and discriminator.
    auto get_id() const -> std::string;
};

/// One cell of an insight table: text, a number it sorts by, and the asset it names when it names one.
struct insight_cell
{
    std::string text;
    double number{};
    bool is_number{false};
    /// An asset or script key to select from the cell; empty when it names none.
    std::string link;
};

/// What a column of an insight table holds, which decides how it reads and sorts.
enum class column_kind : std::uint8_t
{
    text,
    /// An asset or script key: its file name stands out from its folder.
    path,
    /// A count or a measure, right-aligned.
    number,
    /// A size in bytes, right-aligned, with a bar of its share of the column's largest.
    bytes,
    /// A change of size in bytes: a growth stands out from a shrink.
    change,
    /// A date and time.
    time,
};

/// A table of facts with no judgement attached: the largest assets, the textures and their formats.
struct insight_table
{
    std::string id;
    std::string title;
    /// What the table lists, in a sentence.
    std::string description;
    std::vector<std::string> columns;
    std::vector<std::vector<insight_cell>> rows;
    /// Each column's kind, by index; a column past its end is text.
    std::vector<column_kind> kinds;
    /// What the table is about, as the Insights view groups it: "Assets", "Builds", "Scripts".
    std::string group;
    /// The column the rows are sorted by at first, or -1 for the order they were made in.
    int sort_column{-1};
    bool sort_descending{false};
    /// Why the table has no rows, in a sentence; empty when a table without rows needs no reason.
    std::string empty_text;

    auto get_kind(std::size_t column) const -> column_kind;
};

/// What one audit found.
struct report
{
    /// The engine version that made the report, in full ("1.2.0-320").
    std::string engine_version;
    std::string project;
    /// Seconds since the Unix epoch when the run finished.
    std::int64_t finished_at{};
    /// How long the run took.
    double seconds{};
    /// The profiles the run judged.
    std::vector<std::string> profiles;
    std::vector<issue> issues;
    std::vector<insight_table> insights;
    /// What the run could not check, one sentence each ("Code analysis did not run: ...").
    std::vector<std::string> notes;
    /// The rules the run checked: turned on, with what they read there (the script rules need the analysis, the
    /// scene rules the loaded documents). Empty in reports from before it was kept.
    std::vector<std::string> checked_rules;
    /// Why there is no report to judge (an --audit run that could not start); empty for a run that finished.
    std::string error;

    /// The issue with this id, or null.
    auto find(const std::string& id) const -> const issue*;
    auto find(const std::string& id) -> issue*;
    /// How many issues of this level are not ignored.
    auto count(severity level) const -> std::size_t;
};

/// What changed between two reports, by issue id.
struct report_change
{
    /// Issues of the newer report that the older one did not have.
    std::vector<std::string> added;
    /// Issues of the older report that the newer one no longer has.
    std::vector<std::string> fixed;
};

auto get_severity_name(severity level) -> const char*;
auto parse_severity(hpp::string_view name) -> hpp::optional<severity>;
auto get_area_name(area value) -> const char*;
auto parse_area(hpp::string_view name) -> hpp::optional<area>;
/// The names of the areas in a mask, in ALL_AREAS order.
auto get_area_names(area_mask mask) -> std::vector<std::string>;
auto get_subject_kind_name(subject_kind kind) -> const char*;
auto parse_subject_kind(hpp::string_view name) -> hpp::optional<subject_kind>;
auto get_column_kind_name(column_kind kind) -> const char*;
auto parse_column_kind(hpp::string_view name) -> hpp::optional<column_kind>;
auto get_rule_source_name(rule_source source) -> const char*;
auto get_platform_name(target_platform platform) -> const char*;
auto parse_platform(hpp::string_view name) -> hpp::optional<target_platform>;

/// The issues of `newer` that `older` lacks, and the reverse. Ignored issues count like the others. An issue counts
/// only when both runs checked its rule: a rule one of them did not run neither finds nor fixes anything.
auto compare_reports(const report& older, const report& newer) -> report_change;

} // namespace unravel::audit
