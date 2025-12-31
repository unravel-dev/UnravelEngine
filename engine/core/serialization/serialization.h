#pragma once
#include "ser20/access.hpp"
#include "ser20/ser20.hpp"
#include "ser20/types/polymorphic.hpp"
#include "ser20/types/vector.hpp"
#include <hpp/source_location.hpp>
#include <hpp/concepts.hpp>

#include <functional>
#include <string>
#include <vector>
#include <stack>
#include <unordered_map>

#define SERIALIZE_FUNCTION_NAME                    SER20_SERIALIZE_FUNCTION_NAME
#define SAVE_FUNCTION_NAME                         SER20_SAVE_FUNCTION_NAME
#define LOAD_FUNCTION_NAME                         SER20_LOAD_FUNCTION_NAME
#define SAVE_MINIMAL_FUNCTION_NAME                 SER20_SAVE_MINIMAL_FUNCTION_NAME
#define LOAD_MINIMAL_FUNCTION_NAME                 SER20_LOAD_MINIMAL_FUNCTION_NAME
#define SERIALIZE_REGISTER_TYPE_WITH_NAME(T, Name) SER20_REGISTER_TYPE_WITH_NAME(T, Name)
namespace serialization
{
using namespace ser20;
using log_callback_t = std::function<void(const std::string&, const hpp::source_location& loc)>;

struct init_data
{
    log_callback_t warning_logger;
};
void log_warning(const std::string& log_msg, const hpp::source_location& loc = hpp::source_location::current());
void init(const init_data& data = {});

// Path tracking for deserialization
struct path_context
{
    std::function<bool(const std::string&)> should_serialize_property_callback;
    std::vector<std::string> path_segments;
    bool recording_enabled = false;
    bool ignore_next_push = false;
    
    auto push_segment(const std::string& segment) -> bool;
    void pop_segment();
    auto get_current_path() const -> std::string;
    void enable_recording();
    void disable_recording();
    auto is_recording() const -> bool;
    void clear();

    auto should_serialize_property(const std::string& property_path) const -> bool
    {
        if(should_serialize_property_callback)
        {
            return should_serialize_property_callback(property_path);
        }
        return true;
    }
};

auto get_path_context() -> path_context*;
void set_path_context(path_context* ctx);

// Convenience function to get current deserialization path
auto get_current_deserialization_path() -> std::string;

// RAII helper for path segments
struct path_segment_guard
{
    path_segment_guard() = default;
    path_segment_guard(const std::string& segment);
    ~path_segment_guard();

    void push_segment(const std::string& segment);
    void pop_segment();
    // Non-copyable and non-movable to avoid double-popping
    path_segment_guard(const path_segment_guard&) = delete;
    path_segment_guard& operator=(const path_segment_guard&) = delete;
    path_segment_guard(path_segment_guard&&) = delete;
    path_segment_guard& operator=(path_segment_guard&&) = delete;
    
private:
    bool was_pushed_ = false;
};

struct path_skip_segment_guard
{
    path_skip_segment_guard(bool ignore_next_push = false);
    ~path_skip_segment_guard();
    
    // Non-copyable and non-movable to avoid double-popping
    path_skip_segment_guard(const path_skip_segment_guard&) = delete;
    path_skip_segment_guard& operator=(const path_skip_segment_guard&) = delete;
    path_skip_segment_guard(path_skip_segment_guard&&) = delete;
    path_skip_segment_guard& operator=(path_skip_segment_guard&&) = delete;
    
private:
};


} // namespace serialization

#define SERIALIZABLE(T)                                                                                                \
                                                                                                                       \
public:                                                                                                                \
    friend class serialization::access;                                                                                \
    template<typename Archive>                                                                                         \
    friend void SAVE_FUNCTION_NAME(Archive& ar, T const&);                                                             \
    template<typename Archive>                                                                                         \
    friend void LOAD_FUNCTION_NAME(Archive& ar, T&);

#define SERIALIZE_INLINE(cls)                                                                                          \
    template<typename Archive>                                                                                         \
    inline void SERIALIZE_FUNCTION_NAME(Archive& ar, cls& obj)

#define SAVE_INLINE(cls)                                                                                               \
    template<typename Archive>                                                                                         \
    inline void SAVE_FUNCTION_NAME(Archive& ar, cls const& obj)

#define LOAD_INLINE(cls)                                                                                               \
    template<typename Archive>                                                                                         \
    inline void LOAD_FUNCTION_NAME(Archive& ar, cls& obj)

#define SERIALIZE_EXTERN(cls)                                                                                          \
    template<typename Archive>                                                                                         \
    extern void SERIALIZE_FUNCTION_NAME(Archive& ar, cls& obj)

#define SAVE_EXTERN(cls)                                                                                               \
    template<typename Archive>                                                                                         \
    extern void SAVE_FUNCTION_NAME(Archive& ar, cls const& obj)

#define LOAD_EXTERN(cls)                                                                                               \
    template<typename Archive>                                                                                         \
    extern void LOAD_FUNCTION_NAME(Archive& ar, cls& obj)

#define SERIALIZE(cls)                                                                                                 \
    template<typename Archive>                                                                                         \
    void SERIALIZE_FUNCTION_NAME(Archive& ar, cls& obj)

#define SAVE(cls)                                                                                                      \
    template<typename Archive>                                                                                         \
    void SAVE_FUNCTION_NAME(Archive& ar, cls const& obj)

#define LOAD(cls)                                                                                                      \
    template<typename Archive>                                                                                         \
    void LOAD_FUNCTION_NAME(Archive& ar, cls& obj)

#define SERIALIZE_INSTANTIATE(cls, Archive) template void SERIALIZE_FUNCTION_NAME(Archive& archive, cls& obj)

#define SAVE_INSTANTIATE(cls, Archive) template void SAVE_FUNCTION_NAME(Archive& archive, cls const& obj)

#define LOAD_INSTANTIATE(cls, Archive) template void LOAD_FUNCTION_NAME(Archive& archive, cls& obj)

template<typename Archive>
constexpr inline auto is_binary_archive() -> bool
{
    return false;
}

// Check if Archive is loading (deserializing)
template<typename Archive>
constexpr inline auto is_loading_archive() -> bool
{
    return Archive::is_loading::value;
}

// Specialized handler for sequence containers with per-element override support
template<typename Archive, typename T>
inline auto try_serialize_sequence_container(Archive& ar,
                                             ser20::NameValuePair<T>&& t,
                                             const hpp::source_location& loc = hpp::source_location::current()) -> bool;

template<typename Archive, typename T>
inline auto try_serialize_direct(Archive& ar,
                          ser20::NameValuePair<T>&& t,
                          const hpp::source_location& loc = hpp::source_location::current()) -> bool
{
    try
    {
        ar(std::forward<ser20::NameValuePair<T>>(t));
    }
    catch(const std::exception& e)
    {
        if constexpr(is_binary_archive<Archive>())
        {
            serialization::log_warning(e.what(), loc);
        }
        return false;
    }
    return true;
}

// template<typename Archive, typename T>
// inline auto try_serialize_sequence_container(Archive& ar,
//                                              ser20::NameValuePair<T>&& t,
//                                              const hpp::source_location& loc) -> bool
// {
//     using decayed_type = std::decay_t<T>;
//     // static_assert(std::is_same_v<T, std::decay_t<T>>, "T should be decayed type");
    
//     auto path_ctx = serialization::get_path_context();
    
//     // If not loading or no path context, use normal serialization
//     if constexpr(!is_loading_archive<Archive>())
//     {
//         return try_serialize_direct(ar, std::forward<ser20::NameValuePair<T>>(t), loc);
//     }
//     else
//     {
//         if(!path_ctx || !path_ctx->is_recording())
//         {
//             return try_serialize_direct(ar, std::forward<ser20::NameValuePair<T>>(t), loc);
//         }
        
        
        
//         // Deserialize the entire container
//         bool success = try_serialize_direct(ar, std::forward<ser20::NameValuePair<T>>(t), loc);
        
//         // Restore overridden elements (only if they fit in the new container size)
//         // for(auto& [idx, value] : overridden_elements)
//         // {
//         //     if(idx < t.value.size())
//         //     {
//         //         // Use iterator-based approach for containers that support it
//         //         auto it = t.value.begin();
//         //         std::advance(it, static_cast<typename decayed_type::difference_type>(idx));
//         //         *it = std::move(value);
//         //     }
//         //     else
//         //     {
//         //         // Element index is out of bounds in the new container - log warning
//         //         std::string msg = "Cannot restore overridden element [" + std::to_string(idx) + 
//         //                           "] in '" + t.name + "': index out of bounds (container size: " + 
//         //                           std::to_string(t.value.size()) + ")";
//         //         serialization::log_warning(msg, loc);
//         //     }
//         // }
        
//         return success;
//     }
// }

template<typename F>
inline auto serialize_check(const std::string& name, F&& serialize_callback) -> bool
{
    auto path_ctx = serialization::get_path_context();
    if(path_ctx)
    {
        serialization::path_segment_guard guard(name);
        auto path = path_ctx->get_current_path();
        if(!path_ctx->should_serialize_property(path))
        {
            return false;
        }
        return serialize_callback();
    }
    return serialize_callback();
}


template<typename Archive, typename T>
inline auto try_serialize(Archive& ar,
                          ser20::NameValuePair<T>&& t,
                          const hpp::source_location& loc = hpp::source_location::current()) -> bool
{
    bool result = serialize_check(t.name, [&]() -> bool
    {
        return try_serialize_direct(ar, std::forward<ser20::NameValuePair<T>>(t), loc);
    });
   
    return result;
}


template<typename Archive, typename T>
inline auto try_save(Archive& ar,
                     ser20::NameValuePair<T>&& t,
                     const hpp::source_location& loc = hpp::source_location::current()) -> bool
{
    return try_serialize(ar, std::forward<ser20::NameValuePair<T>>(t), loc);
}

template<typename Archive, typename T>
inline auto try_load(Archive& ar,
                     ser20::NameValuePair<T>&& t,
                     const hpp::source_location& loc = hpp::source_location::current()) -> bool
{
    return try_serialize(ar, std::forward<ser20::NameValuePair<T>>(t), loc);
}


template<typename Archive, typename F>
inline auto try_serialize(Archive& ar,
                     const char* name,
                     F&& serialize_callback,
                     const hpp::source_location& loc = hpp::source_location::current()) -> bool
{
    if constexpr(!is_binary_archive<Archive>())
    {
        ar.setNextName(name);
    }
    return serialize_callback();
}

template<typename Archive, typename F>
inline auto try_load(Archive& ar,
                     const char* name,
                     F&& load_callback,
                     const hpp::source_location& loc = hpp::source_location::current()) -> bool
{
    return try_serialize(ar, name, std::forward<F>(load_callback), loc);
}

template<typename Archive, typename F>
inline auto try_save(Archive& ar,
                     const char* name,
                     F&& save_callback,
                     const hpp::source_location& loc = hpp::source_location::current()) -> bool
{
    return try_serialize(ar, name, std::forward<F>(save_callback), loc);
}

