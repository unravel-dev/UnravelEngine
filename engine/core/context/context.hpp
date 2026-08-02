#ifndef HPP_CONTEXT
#define HPP_CONTEXT

#include <hpp/string_view.hpp>
#include <hpp/type_index.hpp>

#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace rtti
{

struct context
{
    template<typename T, typename D = T, typename... Args>
    auto add(Args&&... args) -> T&
    {
        const auto id = hpp::type_id<T>();
        //        std::cout << "context::" << __func__ << " < " << hpp::type_name_str<T>() << " >() -> " << index <<
        //        std::endl;

        std::shared_ptr<T> obj = std::make_shared<D>(std::forward<Args>(args)...);
        objects_[id] = obj;
        return *obj;
    }

    template<typename T>
    auto has() const -> bool
    {
        const auto id = hpp::type_id<T>();
        return objects_.find(id) != objects_.end();
    }

    template<typename T>
    auto get() -> T&
    {
        const auto id = hpp::type_id<T>();
        return *reinterpret_cast<T*>(objects_.at(id).get());
    }

    template<typename T>
    auto get() const -> const T&
    {
        const auto id = hpp::type_id<T>();
        return *reinterpret_cast<const T*>(objects_.at(id).get());
    }

    template<typename T>
    auto get_cached() -> T&
    {
        static T& cached = get<T>();
        return cached;
    }

    template<typename T>
    auto get_cached() const -> const T&
    {
        static const T& cached = get<T>();
        return cached;
    }


    template<typename T>
    auto get_or_empalce() -> T&
    {
        const auto id = hpp::type_id<T>();
        auto it = objects_.find(id);
        if(it == objects_.end())
        {
            return add<T>();
        }


        return *reinterpret_cast<T*>(it->second.get());
    }

    template<typename T>
    void remove()
    {
        const auto id = hpp::type_id<T>();
        //        std::cout << "context::" << __func__ << " < " << hpp::type_name_str<T>() << " >() -> " << index <<
        //        std::endl;
        objects_.erase(id);
    }

    auto empty() const -> bool
    {
        return objects_.empty();
    }

    void print_types() const
    {
        for(const auto& kvp : objects_)
        {
            std::cout << " < " << kvp.first.name() << " >() -> " << kvp.first.hash_code() << std::endl;
        }
    }

private:
    std::map<hpp::type_index, std::shared_ptr<void>> objects_;
};

/**
 * @brief Heterogeneous store keyed by NAME rather than by type.
 *
 * @ref context holds exactly one instance per type, which is the right shape for services -- there
 * is one asset manager, one physics world. It is the wrong shape whenever several instances of the
 * SAME type must coexist and be told apart by role rather than by type: per-camera render state is
 * the motivating case, where two cameras each need their own cascade, their own history buffers and
 * their own accumulation counters, all of identical type.
 *
 * The trade a string key makes is that the key no longer carries the type. @ref context can
 * reinterpret_cast safely precisely because the key IS the type; here it is not, so each entry
 * remembers what was stored in it and every lookup checks. A name collision between two subsystems
 * is the failure mode this introduces and @ref context cannot have, so it fails as a null rather
 * than as a wrong-typed reference to someone else's object.
 *
 * Values are held through @c shared_ptr<void>, which keeps the concrete deleter captured at
 * construction, so non-copyable and non-movable types work. @c std::any would not: it requires
 * copy-constructible, which rules out anything owning a GPU handle.
 */
class named_context
{
public:
    /**
     * @brief Stores a value under @p id, replacing anything already there.
     *
     * Replaces regardless of the previous entry's type. @ref get_or_emplace is the accessor to
     * prefer when the intent is "mine if it exists".
     */
    template<typename T, typename D = T, typename... Args>
    auto add(const hpp::string_view& id, Args&&... args) -> T&
    {
        std::shared_ptr<T> obj = std::make_shared<D>(std::forward<Args>(args)...);
        T& result = *obj;
        element value{hpp::type_id<T>(), std::move(obj)};
        auto it = objects_.find(id);
        if(it == objects_.end())
        {
            objects_.emplace(std::string(id), std::move(value));
        }
        else
        {
            it->second = std::move(value);
        }
        return result;
    }

    /// @brief Whether anything at all is stored under @p id, whatever its type.
    auto contains(const hpp::string_view& id) const -> bool
    {
        return objects_.find(id) != objects_.end();
    }

    /// @brief Whether a @p T is stored under @p id. False when the name holds another type.
    template<typename T>
    auto has(const hpp::string_view& id) const -> bool
    {
        return try_get<T>(id) != nullptr;
    }

    /// @brief Null when @p id is absent OR holds a different type.
    template<typename T>
    auto try_get(const hpp::string_view& id) -> T*
    {
        auto it = objects_.find(id);
        if(it == objects_.end() || it->second.type != hpp::type_id<T>())
        {
            return nullptr;
        }
        // static_cast, not reinterpret_cast: the type was just verified, so this is the same
        // pointer adjustment the original conversion made and the compiler can check it.
        return static_cast<T*>(it->second.object.get());
    }

    template<typename T>
    auto try_get(const hpp::string_view& id) const -> const T*
    {
        return const_cast<named_context*>(this)->try_get<T>(id);
    }

    /**
     * @brief The value under @p id, constructing it from @p args when absent.
     *
     * A name found holding a DIFFERENT type is replaced. That is a programming error rather than a
     * state to recover from -- two subsystems have picked the same name -- but replacing keeps the
     * caller's invariant (it always gets a valid @p T) instead of returning a reference to
     * something of the wrong type.
     */
    template<typename T, typename... Args>
    auto get_or_emplace(const hpp::string_view& id, Args&&... args) -> T&
    {
        if(auto* existing = try_get<T>(id))
        {
            return *existing;
        }
        return add<T>(id, std::forward<Args>(args)...);
    }

    void remove(const hpp::string_view& id)
    {
        auto it = objects_.find(id);
        if(it != objects_.end())
        {
            objects_.erase(it);
        }
    }

    void clear()
    {
        objects_.clear();
    }

    auto empty() const -> bool
    {
        return objects_.empty();
    }

    auto size() const -> size_t
    {
        return objects_.size();
    }

private:
    /**
     * @brief What was stored, and what type it was stored as.
     *
     * Held BY VALUE rather than as a pointer to the static inside @c hpp::type_id: a type_index is
     * a hash and a string_view, so copying it costs nothing and removes any question about what
     * outlives what.
     *
     * Deliberately left as an aggregate with no default member initialisers. @c hpp::type_index has
     * no default constructor, so an element cannot be created without saying what type it holds --
     * which is the invariant this whole class rests on. Neither member is @c const, or the implicit
     * copy assignment would be deleted and @ref add could not replace an existing entry.
     */
    struct element
    {
        hpp::type_index type;
        std::shared_ptr<void> object;
    };

    /// Transparent comparator, so a lookup by @c hpp::string_view does not allocate a
    /// @c std::string first. Only insertion pays for the key.
    std::map<std::string, element, std::less<>> objects_;
};

} // namespace rtti
#endif
