#include "multi_edit.h"
#include "inspectors.h"

#include <editor/editing/actions/composite_action.h>
#include <editor/editing/actions/entity_actions.h>
#include <editor/editing/editing_manager.h>

#include <serialization/document_paths.h>
#include <serialization/sparse_document.h>
#include <string_utils/utils.h>

#include <imgui/imgui.h>
#include <ser20/external/simdjson/simdjson.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

namespace unravel
{
namespace
{

/// The node a component's saved document holds it under.
constexpr const char* MULTI_EDIT_COMPONENT_ROOT = "component";
/// How often the marks of differing rows are worked out again while nothing is edited, in seconds.
constexpr double MULTI_EDIT_MIXED_REFRESH_SECONDS = 0.5;
/// Above this many selected entities differing rows are not marked: working them out would cost frames. Edits
/// still reach every entity.
constexpr size_t MULTI_EDIT_MIXED_LIMIT = 500;

/// The differing rows of one component, and when they were worked out.
struct mixed_rows
{
    serialization::path_set paths;
    double time{};
};

struct multi_edit_state
{
    bool is_active{};
    entt::handle shown{};
    std::vector<entt::handle> others;
    entt::meta_type component{};
    std::map<entt::id_type, mixed_rows> mixed;
    std::vector<multi_edit::asset_identity> other_assets;
    /// The frame a drag field changed its value in: an edit made then moves each entity by the same amounts, kept in
    /// the field's range.
    int drag_frame{-1};
    double drag_min{};
    double drag_max{};
};

auto get_state() -> multi_edit_state&
{
    static multi_edit_state state;
    return state;
}

auto get_seconds() -> double
{
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

/// The paths of @p changed that lie in the row at @p row_path or hold it: what an edit of the row changed, without
/// what the component's setters changed beside it. All of @p changed when none does or the row has no path.
auto keep_row_paths(const serialization::path_set& changed, const std::string& row_path) -> serialization::path_set
{
    if(row_path.empty())
    {
        return changed;
    }
    serialization::path_set kept;
    for(const auto& path : changed)
    {
        if(serialization::is_path_within(path, row_path) || serialization::is_path_within(row_path, path))
        {
            kept.insert(path);
        }
    }
    return kept.empty() ? changed : kept;
}

/// @p key under @p path, as a row path names it.
auto join_mixed_path(const std::string& path, std::string_view key) -> std::string
{
    return path.empty() ? std::string(key) : path + "/" + std::string(key);
}

/// Element @p index of the array at @p path, as a row path names it: "materials[0]".
auto join_mixed_element(const std::string& path, size_t index) -> std::string
{
    return path + "[" + std::to_string(index) + "]";
}

/// Every value under @p value, by path: what differs when only one of the entities has it.
void collect_mixed_value_paths(simdjson::dom::element value, const std::string& path, serialization::path_set& out)
{
    simdjson::dom::object object;
    if(value.get(object) == simdjson::SUCCESS && object.size() > 0)
    {
        for(const auto field : object)
        {
            collect_mixed_value_paths(field.value, join_mixed_path(path, field.key), out);
        }
        return;
    }
    simdjson::dom::array array;
    if(value.get(array) == simdjson::SUCCESS && array.size() > 0)
    {
        size_t index = 0;
        for(const auto element : array)
        {
            collect_mixed_value_paths(element, join_mixed_element(path, index++), out);
        }
        return;
    }
    out.insert(path);
}

void collect_differing_paths(simdjson::dom::element lhs, simdjson::dom::element rhs, const std::string& path, serialization::path_set& out);

void collect_differing_members(simdjson::dom::object lhs, simdjson::dom::object rhs, const std::string& path, serialization::path_set& out)
{
    for(const auto field : lhs)
    {
        const auto child = join_mixed_path(path, field.key);
        simdjson::dom::element other;
        if(rhs[field.key].get(other) == simdjson::SUCCESS)
        {
            collect_differing_paths(field.value, other, child, out);
        }
        else
        {
            collect_mixed_value_paths(field.value, child, out);
        }
    }
    for(const auto field : rhs)
    {
        simdjson::dom::element other;
        if(lhs[field.key].get(other) != simdjson::SUCCESS)
        {
            collect_mixed_value_paths(field.value, join_mixed_path(path, field.key), out);
        }
    }
}

void collect_differing_elements(simdjson::dom::array lhs, simdjson::dom::array rhs, const std::string& path, serialization::path_set& out)
{
    auto lhs_it = lhs.begin();
    auto rhs_it = rhs.begin();
    for(size_t index = 0; lhs_it != lhs.end() || rhs_it != rhs.end(); ++index)
    {
        const auto child = join_mixed_element(path, index);
        if(lhs_it != lhs.end() && rhs_it != rhs.end())
        {
            collect_differing_paths(*lhs_it, *rhs_it, child, out);
        }
        else
        {
            collect_mixed_value_paths(lhs_it != lhs.end() ? *lhs_it : *rhs_it, child, out);
        }
        if(lhs_it != lhs.end())
        {
            ++lhs_it;
        }
        if(rhs_it != rhs.end())
        {
            ++rhs_it;
        }
    }
}

/// The paths where @p lhs and @p rhs differ. Arrays are compared element by element, so an element that differs (a
/// material slot, an entry of a list) marks its own rows, not every row of the array.
void collect_differing_paths(simdjson::dom::element lhs, simdjson::dom::element rhs, const std::string& path, serialization::path_set& out)
{
    simdjson::dom::object lhs_object;
    simdjson::dom::object rhs_object;
    if(lhs.get(lhs_object) == simdjson::SUCCESS && rhs.get(rhs_object) == simdjson::SUCCESS)
    {
        collect_differing_members(lhs_object, rhs_object, path, out);
        return;
    }
    simdjson::dom::array lhs_array;
    simdjson::dom::array rhs_array;
    if(lhs.get(lhs_array) == simdjson::SUCCESS && rhs.get(rhs_array) == simdjson::SUCCESS)
    {
        collect_differing_elements(lhs_array, rhs_array, path, out);
        return;
    }
    if(simdjson::minify(lhs) != simdjson::minify(rhs))
    {
        out.insert(path);
    }
}

/// The component node of a saved component state, parsed by @p parser.
auto parse_component_node(simdjson::dom::parser& parser, const std::string& state, simdjson::dom::element& out) -> bool
{
    simdjson::dom::element document;
    return parser.parse(state).get(document) == simdjson::SUCCESS &&
           document[MULTI_EDIT_COMPONENT_ROOT].get(out) == simdjson::SUCCESS;
}

/// The paths where two saved states of a component differ, element by element in arrays: what an edit changed.
auto find_changed_paths(const std::string& before, const std::string& after) -> serialization::path_set
{
    serialization::path_set changed;
    simdjson::dom::parser before_parser;
    simdjson::dom::parser after_parser;
    simdjson::dom::element before_node;
    simdjson::dom::element after_node;
    if(parse_component_node(before_parser, before, before_node) && parse_component_node(after_parser, after, after_node))
    {
        collect_differing_paths(before_node, after_node, {}, changed);
    }
    return changed;
}

/// The value at @p path ("light/intensity", "materials[0]/roughness") under @p node; false when there is none.
auto find_value_at_path(simdjson::dom::element node, const std::string& path, simdjson::dom::element& out) -> bool
{
    out = node;
    size_t position = 0;
    while(position < path.size())
    {
        if(path[position] == '/')
        {
            ++position;
            continue;
        }
        if(path[position] == '[')
        {
            const auto close = path.find(']', position);
            simdjson::dom::array array;
            if(close == std::string::npos || out.get(array) != simdjson::SUCCESS)
            {
                return false;
            }
            const auto index = static_cast<size_t>(std::strtoull(path.c_str() + position + 1, nullptr, 10));
            if(array.at(index).get(out) != simdjson::SUCCESS)
            {
                return false;
            }
            position = close + 1;
            continue;
        }
        const auto end = std::min(path.find_first_of("/[", position), path.size());
        simdjson::dom::object object;
        if(out.get(object) != simdjson::SUCCESS ||
           object[std::string_view(path).substr(position, end - position)].get(out) != simdjson::SUCCESS)
        {
            return false;
        }
        position = end;
    }
    return true;
}

/// The number at @p path under @p node; none when it holds none.
auto read_number_at_path(simdjson::dom::element node, const std::string& path) -> hpp::optional<double>
{
    simdjson::dom::element value;
    double number = 0.0;
    if(!find_value_at_path(node, path, value) || !value.is_number() || value.get_double().get(number) != simdjson::SUCCESS)
    {
        return hpp::nullopt;
    }
    return number;
}

/// The number of elements of the array at @p path under @p node; 0 when there is none.
auto read_array_size_at_path(simdjson::dom::element node, const std::string& path) -> size_t
{
    simdjson::dom::element value;
    simdjson::dom::array array;
    return find_value_at_path(node, path, value) && value.get(array) == simdjson::SUCCESS ? array.size() : 0;
}

/**
 * @brief The array @p array_path is paired with (PAIRED_PROPERTY_ATTRIBUTE): the sibling whose element i it holds for,
 *        as a model's material instances hold for its shared materials. Its own saved list may stop early (instances
 *        without overrides), so its length is the sibling's. Empty for an array no property pairs.
 */
auto find_pairing_array(const entt::meta_type& component_type, const std::string& array_path) -> std::string
{
    const auto split = array_path.rfind('/');
    const std::string parent = split == std::string::npos ? std::string{} : array_path.substr(0, split);
    const std::string name = split == std::string::npos ? array_path : array_path.substr(split + 1);
    if(parent.find('[') != std::string::npos)
    {
        return {};
    }
    auto type = component_type;
    for(size_t position = 0; type && position < parent.size();)
    {
        const auto end = std::min(parent.find('/', position), parent.size());
        const auto data = type.data(entt::hashed_string{parent.substr(position, end - position).c_str()}.value());
        type = data ? data.type() : entt::meta_type{};
        position = end + 1;
    }
    if(!type)
    {
        return {};
    }
    for(auto [id, data] : type.data())
    {
        if(entt::get_attribute_as<std::string>(data, PAIRED_PROPERTY_ATTRIBUTE) == name)
        {
            return join_mixed_path(parent, entt::get_name(data));
        }
    }
    return {};
}

/**
 * @brief The documents an edit of the drawn entity is loaded into the others from, one per target.
 *
 * Each is the drawn entity's state after the edit, written for one target: a list keeps the target's own length,
 * and one shorter than the edited element grows to it, taking the drawn entity's elements it gains whole; the
 * elements the edit does not reach are not loaded. An edit that adds or removes elements gives every target the drawn
 * entity's list. A paired list (a model's material instances) is written at the drawn entity's saved length: a slot
 * past it has no overrides there, and its owner reverts the listed ones. After a drag, each changed number is the
 * target's own moved by the amount the drag moved the drawn entity's, as the gizmo moves them, and kept in the
 * field's range; otherwise every target takes the drawn entity's value.
 */
class edit_carrier
{
public:
    /// What one target loads: a state, and the paths of it to load.
    struct target_load
    {
        std::string state;
        serialization::path_set paths;
    };

    /// The range a drag keeps each target's moved numbers in.
    struct drag_range
    {
        double min{};
        double max{};
    };

    edit_carrier(const entt::meta_type& component_type,
                 const std::string& before,
                 const std::string& after,
                 const serialization::path_set& changed,
                 hpp::optional<drag_range> drag)
        : type_(component_type)
        , changed_(changed)
        , after_state_(after)
        , drag_(drag)
    {
        has_after_ = parse_component_node(after_parser_, after, after_node_);
        simdjson::dom::parser before_parser;
        simdjson::dom::element before_node;
        if(!has_after_ || !parse_component_node(before_parser, before, before_node))
        {
            return;
        }
        find_resized_lists(before_node);
        if(!drag_)
        {
            return;
        }
        for(const auto& path : changed)
        {
            const auto from = read_number_at_path(before_node, path);
            const auto to = read_number_at_path(after_node_, path);
            if(from && to)
            {
                offsets_.emplace(path, *to - *from);
            }
        }
    }

    auto make_load(const std::string& target_before) -> target_load
    {
        simdjson::dom::element target_node;
        if(!has_after_ || !parse_component_node(target_parser_, target_before, target_node))
        {
            return {after_state_, changed_};
        }
        target_load load;
        std::map<std::string, size_t> lengths;
        for(const auto& path : changed_)
        {
            add_target_path(target_node, path, load.paths, lengths);
        }
        std::map<std::string, double> numbers;
        for(const auto& [path, offset] : offsets_)
        {
            const auto own = read_number_at_path(target_node, path);
            numbers.emplace(path,
                            own ? std::clamp(*own + offset, drag_->min, drag_->max)
                                : read_number_at_path(after_node_, path).value_or(0.0));
        }
        load.state = fmt::format("{{\"{}\":", MULTI_EDIT_COMPONENT_ROOT);
        write_node(after_node_, {}, {numbers, lengths, target_node}, load.state);
        load.state += "}";
        return load;
    }

private:
    /// What write_node writes in place of the drawn entity's values.
    struct target_values
    {
        const std::map<std::string, double>& numbers;
        const std::map<std::string, size_t>& lengths;
        simdjson::dom::element target;
    };

    /// Notes the lists the edit added elements to or removed elements from. A paired list is left out: its saved
    /// length follows its overrides, not its elements.
    void find_resized_lists(simdjson::dom::element before_node)
    {
        for(const auto& path : changed_)
        {
            for(size_t open = path.find('['); open != std::string::npos; open = path.find('[', open + 1))
            {
                const std::string array_path = path.substr(0, open);
                const bool is_resized =
                    read_array_size_at_path(before_node, array_path) != read_array_size_at_path(after_node_, array_path);
                if(is_resized && find_pairing_array(type_, array_path).empty())
                {
                    resized_lists_.insert(array_path);
                }
            }
        }
    }

    /// Adds what the target loads of the changed @p path, and the length each list on the way takes: the target's own,
    /// or up to the edited element. An element the target lacks is loaded whole, with the ones before it it lacks; a
    /// list the edit resized is loaded whole.
    void add_target_path(simdjson::dom::element target_node,
                         const std::string& path,
                         serialization::path_set& paths,
                         std::map<std::string, size_t>& lengths) const
    {
        for(size_t open = path.find('['); open != std::string::npos; open = path.find('[', open + 1))
        {
            const auto close = path.find(']', open);
            if(close == std::string::npos)
            {
                break;
            }
            const std::string array_path = path.substr(0, open);
            if(resized_lists_.count(array_path) > 0)
            {
                paths.insert(array_path);
                return;
            }
            const auto index = static_cast<size_t>(std::strtoull(path.c_str() + open + 1, nullptr, 10));
            const auto own_count = read_array_size_at_path(target_node, array_path);
            const auto pairing = find_pairing_array(type_, array_path);
            const auto count = pairing.empty() ? own_count : read_array_size_at_path(target_node, pairing);
            if(pairing.empty())
            {
                auto& length = lengths.try_emplace(array_path, own_count).first->second;
                length = std::max(length, index + 1);
            }
            else
            {
                lengths.emplace(array_path, read_array_size_at_path(after_node_, array_path));
            }
            if(index >= count)
            {
                for(size_t added = count; added <= index; ++added)
                {
                    paths.insert(join_mixed_element(array_path, added));
                }
                return;
            }
        }
        paths.insert(path);
    }

    /// @p node as JSON, with the target's numbers and list lengths in place of the drawn entity's.
    static void write_node(simdjson::dom::element node, const std::string& path, const target_values& values, std::string& out)
    {
        if(const auto found = values.numbers.find(path); found != values.numbers.end())
        {
            const bool is_integer = node.is_int64() || node.is_uint64();
            out += is_integer ? fmt::format("{}", static_cast<int64_t>(std::llround(found->second)))
                              : fmt::format("{}", found->second);
            return;
        }
        simdjson::dom::object object;
        if(node.get(object) == simdjson::SUCCESS)
        {
            out += '{';
            bool is_first = true;
            for(const auto field : object)
            {
                out += is_first ? "" : ",";
                out += fmt::format("\"{}\":", string_utils::escape_json(std::string(field.key)));
                write_node(field.value, join_mixed_path(path, field.key), values, out);
                is_first = false;
            }
            out += '}';
            return;
        }
        simdjson::dom::array array;
        if(node.get(array) == simdjson::SUCCESS)
        {
            write_array(array, path, values, out);
            return;
        }
        out += simdjson::minify(node);
    }

    /// An array of the drawn entity's state, or for a list on the way to an edit, at the length the target takes:
    /// past the drawn entity's elements, the target's own fill it, which the load steps over.
    static void write_array(simdjson::dom::array array, const std::string& path, const target_values& values, std::string& out)
    {
        const auto found = values.lengths.find(path);
        if(found == values.lengths.end())
        {
            out += simdjson::minify(array);
            return;
        }
        out += '[';
        size_t index = 0;
        for(const auto element : array)
        {
            if(index == found->second)
            {
                break;
            }
            out += index == 0 ? "" : ",";
            write_node(element, join_mixed_element(path, index++), values, out);
        }
        for(; index < found->second; ++index)
        {
            simdjson::dom::element own;
            out += index == 0 ? "" : ",";
            out += find_value_at_path(values.target, join_mixed_element(path, index), own) ? simdjson::minify(own) : "null";
        }
        out += ']';
    }

    entt::meta_type type_;
    serialization::path_set changed_;
    std::string after_state_;
    simdjson::dom::parser after_parser_;
    simdjson::dom::parser target_parser_;
    simdjson::dom::element after_node_;
    bool has_after_{};
    std::map<std::string, double> offsets_;
    hpp::optional<drag_range> drag_;
    /// The lists the edit added elements to or removed elements from.
    std::set<std::string> resized_lists_;
};

/// Works out the rows of @p type that differ between the drawn entity and any other.
auto find_mixed_paths(const multi_edit_state& state, const entt::meta_type& type) -> serialization::path_set
{
    serialization::path_set mixed;
    const auto shown_state = prefab_override_context::save_component_state(state.shown, type);
    simdjson::dom::parser shown_parser;
    simdjson::dom::element shown_node;
    if(shown_state.empty() || !parse_component_node(shown_parser, shown_state, shown_node))
    {
        return mixed;
    }
    simdjson::dom::parser other_parser;
    for(const auto& other : state.others)
    {
        const auto other_state = prefab_override_context::save_component_state(other, type);
        simdjson::dom::element other_node;
        if(other_state.empty() || other_state == shown_state || !parse_component_node(other_parser, other_state, other_node))
        {
            continue;
        }
        collect_differing_paths(shown_node, other_node, {}, mixed);
    }
    return mixed;
}

} // namespace

namespace multi_edit
{

void begin(entt::handle shown, std::vector<entt::handle> others)
{
    auto& state = get_state();
    const bool is_same = state.shown == shown && state.others == others;
    state.is_active = !others.empty();
    state.shown = shown;
    state.others = std::move(others);
    state.component = {};
    if(!is_same)
    {
        state.mixed.clear();
    }
}

void end()
{
    auto& state = get_state();
    state.is_active = false;
    state.component = {};
}

auto is_active() -> bool
{
    return get_state().is_active;
}

auto get_targets(entt::handle shown) -> std::vector<entt::handle>
{
    const auto& state = get_state();
    std::vector<entt::handle> targets{shown};
    if(state.is_active && shown == state.shown)
    {
        targets.insert(targets.end(), state.others.begin(), state.others.end());
    }
    return targets;
}

auto get_count() -> size_t
{
    const auto& state = get_state();
    return state.is_active ? state.others.size() + 1 : 1;
}

auto all_have(const entt::meta_type& type) -> bool
{
    const auto& state = get_state();
    // The others of a selection drawn earlier are kept for comparison only: they are not selected now.
    if(!state.is_active)
    {
        return true;
    }
    return std::all_of(state.others.begin(),
                       state.others.end(),
                       [&](const entt::handle& other)
                       {
                           return has_component(other, type);
                       });
}

void set_component(const entt::meta_type& type)
{
    get_state().component = type;
}

auto covers_edit() -> bool
{
    const auto& state = get_state();
    return state.is_active && static_cast<bool>(state.component);
}

void record_edit(rtti::context& ctx,
                 const std::string& step_name,
                 const std::string& row_path,
                 const std::string& pretty_row_path,
                 const std::function<void()>& write)
{
    auto& state = get_state();
    const auto type = state.component;
    std::vector<entt::handle> targets{state.shown};
    std::copy_if(state.others.begin(),
                 state.others.end(),
                 std::back_inserter(targets),
                 [&](const entt::handle& other)
                 {
                     return has_component(other, type);
                 });
    std::vector<std::string> states_before;
    states_before.reserve(targets.size());
    for(const auto& target : targets)
    {
        states_before.push_back(prefab_override_context::save_component_state(target, type));
    }

    write();
    const auto shown_after = prefab_override_context::save_component_state(state.shown, type);
    const auto changed = keep_row_paths(find_changed_paths(states_before.front(), shown_after), row_path);
    // A drag moves every entity by the same amounts; a typed or picked value is given to all of them.
    hpp::optional<edit_carrier::drag_range> drag;
    if(state.drag_frame == ImGui::GetFrameCount())
    {
        drag = edit_carrier::drag_range{state.drag_min, state.drag_max};
    }
    edit_carrier carrier(type, states_before.front(), shown_after, changed, drag);

    auto edits = std::make_shared<composite_action_t>();
    const auto make_step = [&](entt::handle target, const std::string& state_before)
    {
        auto step = std::make_shared<entity_component_state_action_t>(target, type, state_before);
        step->override_path = row_path;
        step->pretty_override_path = pretty_row_path;
        return step;
    };
    edits->add_sub_action(make_step(state.shown, states_before.front()));
    for(size_t i = 1; i < targets.size(); ++i)
    {
        if(!changed.empty())
        {
            // Only what the edit changed is loaded: the rest of each component stays as it is.
            const auto load = carrier.make_load(states_before[i]);
            const serialization::scoped_path_filter filter(MULTI_EDIT_COMPONENT_ROOT, load.paths);
            load_component_state(targets[i], type, load.state);
        }
        edits->add_sub_action(make_step(targets[i], states_before[i]));
    }
    // Named after the row, so undo and redo point at it.
    edits->name = step_name;
    ctx.get_cached<editing_manager>().do_action(step_name, edits);
    state.mixed.erase(type.info().index());
}

void note_drag_change(double min, double max)
{
    // Typing into a drag field, or the click that ends the typing, gives the typed value to every entity.
    if(ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::GetIO().WantTextInput)
    {
        auto& state = get_state();
        state.drag_frame = ImGui::GetFrameCount();
        state.drag_min = min;
        state.drag_max = max;
    }
}

auto is_row_mixed(const std::string& row_path) -> bool
{
    auto& state = get_state();
    if(!state.is_active || !state.component || row_path.empty() || state.others.size() > MULTI_EDIT_MIXED_LIMIT)
    {
        return false;
    }
    auto& rows = state.mixed[state.component.info().index()];
    const double now = get_seconds();
    if(rows.time == 0.0 || now - rows.time > MULTI_EDIT_MIXED_REFRESH_SECONDS)
    {
        rows.paths = find_mixed_paths(state, state.component);
        rows.time = now;
    }
    return serialization::is_path_related(row_path, rows.paths);
}

void begin_assets(std::vector<asset_identity> others)
{
    get_state().other_assets = std::move(others);
}

void end_assets()
{
    get_state().other_assets.clear();
}

auto get_other_assets() -> const std::vector<asset_identity>&
{
    return get_state().other_assets;
}

} // namespace multi_edit
} // namespace unravel
