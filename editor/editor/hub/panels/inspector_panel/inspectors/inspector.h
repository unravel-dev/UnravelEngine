#pragma once

#include <context/context.hpp>
#include <reflection/reflection.h>
#include <reflection/registration.h>

#include <editor/imgui/integration/imgui.h>

namespace unravel
{
class property_layout
{
public:
    property_layout();

    property_layout(const entt::meta_data& prop, bool columns = true);
    property_layout(const std::string& name, bool columns = true);
    property_layout(const std::string& name, const std::string& tooltip, bool columns = true);
    property_layout(const std::string& name, const std::function<void()>& callback, bool columns = true);

    ~property_layout();

    void set_data(const entt::meta_data& prop, bool columns = true);
    void set_data(const std::string& name, const std::string& tooltip, bool columns = true);

    void push_layout(bool auto_proceed_to_next_column = true);
    auto push_tree_layout(ImGuiTreeNodeFlags flags = 0) -> bool;
    void pop_layout();

    void prepare_for_item();

    static auto get_current() -> property_layout*;

private:
    bool pushed_{};
    std::string name_;
    std::string tooltip_;
    std::function<void()> callback_;
    bool columns_{};
    bool open_{};
    bool columns_open_{};
};

struct var_info
{
    bool read_only{};
    bool is_property{};
};

struct inspect_result
{
    bool changed{};
    bool edit_finished{};
    bool change_recorded{};

    auto operator|=(const inspect_result& rhs) -> inspect_result&
    {
        changed |= rhs.changed;
        edit_finished |= rhs.edit_finished;
        change_recorded |= rhs.change_recorded;
        return *this;
    }

    auto operator|(const inspect_result& rhs) const -> inspect_result
    {
        inspect_result result{};
        result.changed |= rhs.changed;
        result.edit_finished |= rhs.edit_finished;
        result.change_recorded |= rhs.change_recorded;
        return result;
    }
};

struct meta_any_proxy;
struct meta_any_proxy_impl
{
    std::function<void(entt::meta_any&)> getter;
    std::function<void(meta_any_proxy& proxy, const entt::meta_any&)> setter;
    std::function<std::string()> get_name;
};

struct meta_any_proxy
{
    std::shared_ptr<meta_any_proxy_impl> impl = std::make_shared<meta_any_proxy_impl>();
};

auto make_proxy(entt::meta_any& var) -> meta_any_proxy;


struct inspector : crtp_meta_type<inspector>
{
    template<typename T>
    static void create_and_register(const entt::meta_type& inspected_type,
                                    std::unordered_map<entt::id_type, std::shared_ptr<inspector>>& type_map)
    {
        type_map[inspected_type.info().index()] = std::make_shared<T>();
    }

    using attribute_getter = std::function<entt::meta_any(const char*)>;

    virtual ~inspector() = default;

    virtual void before_inspect(const entt::meta_data& prop);
    virtual void after_inspect(const entt::meta_data& prop);
    virtual auto inspect(rtti::context& ctx,
                             entt::meta_any& var,
                             const meta_any_proxy& var_proxy,
                             const var_info& info,
                             const entt::meta_custom& custom) -> inspect_result = 0;

    // Virtual method to refresh the inspector's state
    virtual auto refresh(rtti::context& ctx) -> void
    {
    }

    std::unique_ptr<property_layout> layout_{};
    bool open_{};
};

REFLECT_INLINE(inspector)
{
    entt::meta_factory<inspector>{}.type("inspector"_hs);
}
#define REFLECT_INSPECTOR_INLINE(inspector_type, inspected_type)                                                       \
    REFLECT_INLINE(inspector_type)                                                                                     \
    {                                                                                                                  \
        entt::meta_factory<inspector_type>{}                                                                           \
            .type(entt::hashed_string{#inspector_type})                                                                \
            .custom<entt::attributes>(                                                                                 \
                entt::attributes{entt::attribute{"inspected_type", entt::resolve<inspected_type>()}})                  \
            .base<inspector>()                                                                                         \
            .func<&inspector::create_and_register<inspector_type>>("create_and_register"_hs);                          \
    }

#define REFLECT_INSPECTOR(inspector_type, inspected_type)                                                              \
    REFLECT(inspector_type)                                                                                            \
    {                                                                                                                  \
        entt::meta_factory<inspector_type>{}                                                                           \
            .type(entt::hashed_string{#inspector_type})                                                                \
            .custom<entt::attributes>(                                                                                 \
                entt::attributes{entt::attribute{"inspected_type", entt::resolve<inspected_type>()}})                  \
            .base<inspector>()                                                                                         \
            .func<&inspector::create_and_register<inspector_type>>("create_and_register"_hs);                          \
    }

} // namespace unravel
