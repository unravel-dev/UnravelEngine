#include "ui_element_handles.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Plugin.h>

#include <deque>
#include <unordered_map>
#include <vector>

namespace unravel
{
namespace
{

static_assert(sizeof(ui_element_handle) == sizeof(uint64_t), "handles pack a 32-bit index and generation");

/// Destroyed handles kept for scripts to drain. Only elements scripts touched have handles, but
/// while no script drains them (edit mode) the oldest are dropped past this count.
constexpr size_t MAX_PENDING_DESTROYED_HANDLES = 65536;

struct handle_slot
{
    Rml::Element* element = nullptr;
    uint32_t generation = 1;
};

struct orphan_element
{
    Rml::ElementDocument* document = nullptr;
    Rml::ElementPtr element;
};

struct handle_table
{
    std::vector<handle_slot> slots;
    std::vector<uint32_t> free_slots;
    std::unordered_map<Rml::Element*, uint32_t> slot_by_element;
    std::deque<ui_element_handle> destroyed;
    std::unordered_map<Rml::Element*, orphan_element> orphans;
    uint64_t layout_serial = 1;
};

auto get_table() -> handle_table&
{
    static handle_table table;
    return table;
}

auto make_handle(uint32_t index, uint32_t generation) -> ui_element_handle
{
    // Index + 1 in the low half, so no live handle is 0.
    return static_cast<ui_element_handle>((uint64_t(generation) << 32) | (uint64_t(index) + 1));
}

/// Ends @p element's handle: the slot is reused under the next generation.
void release_element(Rml::Element* element)
{
    auto& table = get_table();
    auto it = table.slot_by_element.find(element);
    if(it == table.slot_by_element.end())
    {
        return;
    }
    const uint32_t index = it->second;
    auto& slot = table.slots[index];
    const auto handle = make_handle(index, slot.generation);
    slot.element = nullptr;
    ++slot.generation;
    table.free_slots.push_back(index);
    table.slot_by_element.erase(it);
    if(table.destroyed.size() >= MAX_PENDING_DESTROYED_HANDLES)
    {
        table.destroyed.pop_front();
    }
    table.destroyed.push_back(handle);
}

/// Destroys the kept orphans of @p document, or every kept orphan for null.
void release_orphans(Rml::ElementDocument* document)
{
    // Moved out first: destroying an orphan re-enters the table through the destroy hook.
    std::vector<Rml::ElementPtr> released;
    auto& orphans = get_table().orphans;
    for(auto it = orphans.begin(); it != orphans.end();)
    {
        if(!document || it->second.document == document)
        {
            released.push_back(std::move(it->second.element));
            it = orphans.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

class element_handle_plugin final : public Rml::Plugin
{
public:
    auto GetEventClasses() -> int override
    {
        return EVT_ELEMENT | EVT_DOCUMENT;
    }

    void OnDocumentUnload(Rml::ElementDocument* document) override
    {
        release_orphans(document);
    }

    void OnElementDestroy(Rml::Element* element) override
    {
        release_element(element);
    }
};

element_handle_plugin plugin;
bool is_plugin_registered = false;

} // namespace

auto get_ui_element_handle(Rml::Element* element) -> ui_element_handle
{
    if(!element)
    {
        return 0;
    }
    auto& table = get_table();
    if(auto it = table.slot_by_element.find(element); it != table.slot_by_element.end())
    {
        return make_handle(it->second, table.slots[it->second].generation);
    }
    uint32_t index = 0;
    if(!table.free_slots.empty())
    {
        index = table.free_slots.back();
        table.free_slots.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>(table.slots.size());
        table.slots.emplace_back();
    }
    table.slots[index].element = element;
    table.slot_by_element[element] = index;
    return make_handle(index, table.slots[index].generation);
}

auto resolve_ui_element_handle(ui_element_handle handle) -> Rml::Element*
{
    const auto raw = static_cast<uint64_t>(handle);
    const auto index_plus_one = static_cast<uint32_t>(raw & 0xffffffffu);
    const auto generation = static_cast<uint32_t>(raw >> 32);
    const auto& table = get_table();
    if(index_plus_one == 0 || index_plus_one > table.slots.size())
    {
        return nullptr;
    }
    const auto& slot = table.slots[index_plus_one - 1];
    return slot.generation == generation ? slot.element : nullptr;
}

auto take_destroyed_ui_element_handle() -> ui_element_handle
{
    auto& table = get_table();
    if(table.destroyed.empty())
    {
        return 0;
    }
    const auto handle = table.destroyed.front();
    table.destroyed.pop_front();
    return handle;
}

auto adopt_orphan_ui_element(Rml::ElementDocument* document, Rml::ElementPtr element) -> Rml::Element*
{
    auto* raw = element.get();
    if(raw)
    {
        get_table().orphans[raw] = orphan_element{document, std::move(element)};
    }
    return raw;
}

auto take_orphan_ui_element(Rml::Element* element) -> Rml::ElementPtr
{
    auto& orphans = get_table().orphans;
    auto it = orphans.find(element);
    if(it == orphans.end())
    {
        return nullptr;
    }
    auto owned = std::move(it->second.element);
    orphans.erase(it);
    return owned;
}

void mark_ui_layout_stale()
{
    ++get_table().layout_serial;
}

auto get_ui_layout_serial() -> uint64_t
{
    return get_table().layout_serial;
}

void register_ui_element_handles()
{
    if(!is_plugin_registered)
    {
        Rml::RegisterPlugin(&plugin);
        is_plugin_registered = true;
    }
}

void unregister_ui_element_handles()
{
    release_orphans(nullptr);
    if(is_plugin_registered)
    {
        Rml::UnregisterPlugin(&plugin);
        is_plugin_registered = false;
    }
    // Every element goes with RmlUi: every handle ends, generations moving past the old ones.
    auto& table = get_table();
    for(auto& [element, index] : table.slot_by_element)
    {
        auto& slot = table.slots[index];
        table.destroyed.push_back(make_handle(index, slot.generation));
        slot.element = nullptr;
        ++slot.generation;
        table.free_slots.push_back(index);
    }
    table.slot_by_element.clear();
}

} // namespace unravel
