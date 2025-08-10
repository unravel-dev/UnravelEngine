#pragma once
#include "seq_common.h"
#include "seq_core.h"
#include "seq_math.h"
#include "seq_updater.h"
#include <hpp/utility.hpp>
#include <vector>

namespace seq
{

template<typename Object, typename T, typename Setter, typename Getter>
seq_action create_from_to_impl(const std::string& creator_name,
                               Object* object,
                               const T& begin,
                               const T& end,
                               const Setter& setter_func,
                               const Getter& getter_func,
                               const duration_t& duration,
                               const sentinel_t& sentinel,
                               const ease_t& ease_func)
{
    if(!object)
    {
        return {};
    }
    auto creator = [object,
                    begin = begin,
                    end = end,
                    sentinel = sentinel,
                    ease_func = ease_func,
                    setter_func = setter_func,
                    getter_func = getter_func]()
    {
        auto initialize_func = [begin, setter_func](Object* object, const sentinel_t&, seq_action& self)
        {
            hpp::invoke(setter_func, *object, begin);
            inspector::update_begin_value(self, begin);
            return begin;
        };

        auto updater_func = [setter_func, getter_func](Object* object, const T& next, seq_action& self) mutable
        {
            hpp::invoke(setter_func, *object, next);

            // TODO move calc inside
            inspector::update_action_status(self, hpp::invoke(getter_func, *object));
        };

        auto getter = [getter_func](Object* object, seq_action&) mutable
        {
            return hpp::invoke(getter_func, *object);
        };

        return create_action_updater(object,
                                     end,
                                     sentinel,
                                     std::move(initialize_func),
                                     std::move(updater_func),
                                     std::move(getter),
                                     ease_func);
    };

    auto action = seq_action(std::move(creator), duration, sentinel);
    inspector::add_info(action, creator_name, *object, end, ease_func);
    return action;
}

template<typename Object, typename T, typename Setter, typename Getter>
seq_action create_to_impl(const std::string& creator_name,
                          Object* object,
                          const T& end,
                          const Setter& setter_func,
                          const Getter& getter_func,
                          const duration_t& duration,
                          const sentinel_t& sentinel,
                          const ease_t& ease_func)
{
    auto creator = [object,
                    end = end,
                    sentinel = sentinel,
                    ease_func = ease_func,
                    setter_func = setter_func,
                    getter_func = getter_func]()
    {
        auto initialize_func = [getter_func](Object* object, const sentinel_t& sentinel, seq_action& self)
        {
            if(sentinel.expired())
            {
                T out{};
                inspector::update_begin_value(self, out);
                return out;
            }

            // TODO move calc inside
            T out = hpp::invoke(getter_func, *object);
            inspector::update_begin_value(self, out);
            return out;
        };

        auto updater_func = [setter_func, getter_func](Object* object, const T& next, seq_action& self) mutable
        {
            hpp::invoke(setter_func, *object, next);

            // TODO move calc inside
            inspector::update_action_status(self, hpp::invoke(getter_func, *object));
        };

        auto getter = [getter_func](Object* object, seq_action&) mutable
        {
            return hpp::invoke(getter_func, *object);
        };

        return create_action_updater(object,
                                     end,
                                     sentinel,
                                     std::move(initialize_func),
                                     std::move(updater_func),
                                     std::move(getter),
                                     ease_func);
    };

    auto action = seq_action(std::move(creator), duration, sentinel);
    inspector::add_info(action, creator_name, *object, end, ease_func);
    return action;
}

template<typename Object, typename T, typename Setter, typename Getter>
seq_action create_by_impl(const std::string& creator_name,
                          Object* object,
                          const T& amount,
                          const Setter& setter_func,
                          const Getter& getter_func,
                          const duration_t& duration,
                          const sentinel_t& sentinel,
                          const ease_t& ease_func)
{
    if(!object)
    {
        return {};
    }
    auto creator = [object,
                    amount = amount,
                    sentinel = sentinel,
                    ease_func = ease_func,
                    setter_func = setter_func,
                    getter_func = getter_func]()
    {
        auto initialize_func = [](Object*, const sentinel_t&, seq_action& self)
        {
            T out{};
            inspector::update_begin_value(self, out);
            return out;
        };

        auto updater_func =
            [setter_func, getter_func, prev = T{}](Object* object, const T& next, seq_action& self) mutable
        {
            T current = hpp::invoke(getter_func, *object);

            const auto updated = current + (next - prev);
            hpp::invoke(setter_func, *object, updated);

            // TODO move calc inside
            inspector::update_action_status(self, hpp::invoke(getter_func, *object));

            prev = next;
        };

        auto getter = [getter_func](Object* object, seq_action&) mutable
        {
            return hpp::invoke(getter_func, *object);
        };

        return create_action_updater(object,
                                     amount,
                                     sentinel,
                                     std::move(initialize_func),
                                     std::move(updater_func),
                                     std::move(getter),
                                     ease_func);
    };

    auto action = seq_action(std::move(creator), duration, sentinel);
    inspector::add_info(action, creator_name, *object, amount, ease_func);
    return action;
}

} // namespace seq
