#pragma once
#include "ser20/access.hpp"
#include "ser20/ser20.hpp"
#include "ser20/types/polymorphic.hpp"
#include "ser20/types/vector.hpp"
#include <hpp/source_location.hpp>
#include <hpp/concepts.hpp>
#include <hpp/string_view.hpp>

#include <concepts>
#include <cstdint>
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
    /// Whether the document being read states a property as a whole. Asked about a property
    /// the document does not have, where it means the document removed it.
    std::function<bool(const std::string&)> is_property_stated_whole_callback;
    /// Whether a statement here or above keeps a property of the document's own content as a
    /// whole - one the document then has to be checked for.
    std::function<bool(const std::string&)> is_property_kept_whole_callback;
    /// Told about a property kept as a whole that the document no longer has: the statement
    /// keeping it is stale.
    std::function<void(const std::string&)> on_stale_property_callback;
    bool recording_enabled = false;
    bool ignore_next_push = false;

    auto push_segment(hpp::string_view segment) -> bool;
    void pop_segment();

    /**
     * @brief The joined path, e.g. "entities[0]/<uuid>/components/transform_component/x".
     *
     * Maintained incrementally by push_segment/pop_segment rather than rebuilt on demand:
     * during a prefab resync this is read once per property of every entity, and building
     * it there through a stringstream was the largest single cost on that path. Returned
     * by reference for the same reason - the caller must not outlive the next push or pop.
     */
    auto get_current_path() const -> const std::string&;

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

    auto is_property_stated_whole(const std::string& property_path) const -> bool
    {
        return is_property_stated_whole_callback && is_property_stated_whole_callback(property_path);
    }

    auto is_property_kept_whole(const std::string& property_path) const -> bool
    {
        return is_property_kept_whole_callback && is_property_kept_whole_callback(property_path);
    }

    void note_stale_property(const std::string& property_path) const
    {
        if(on_stale_property_callback)
        {
            on_stale_property_callback(property_path);
        }
    }

private:
    /// The joined path itself, always current.
    std::string path_;
    /// One entry per pushed segment: the offset just past it in path_, so a pop is a
    /// resize back to the previous entry (which also drops the separator).
    std::vector<std::size_t> segment_ends_;
};

/**
 * @brief What a document is being written for.
 *
 * Read by create_oarchive_associative when it builds the writer, so the choice travels
 * with the operation rather than through every save signature on the way down.
 */
enum class output_format
{
    /// Indented and diffable. Project source - scenes, prefabs, materials: files that live
    /// in version control and get read, reviewed and occasionally hand-edited.
    readable,

    /// No indentation. For documents no human will ever see: clone buffers, undo
    /// snapshots, editor checkpoints - written and read back within one session, often
    /// many times a second.
    compact,

    /// A binary document (serialization/binary_archive.h): the JSON archive's structure and
    /// reading rules in binary. For documents nothing but the engine reads - copies made inside
    /// the process and compiled assets, scenes and prefabs. Loaders tell it from JSON by its
    /// first bytes.
    binary,
};

/// Defaults to readable, so a site that has not opted in keeps writing files people can
/// read. Silence means "no change", never "unreadable output on disk".
auto get_output_format() -> output_format;

/**
 * @brief Selects the output format for the writers created inside this scope.
 *
 * Nested-safe; the innermost scope wins and the previous value is restored on exit.
 * Thread-local, matching the save/load contexts it sits alongside.
 */
struct scoped_output_format
{
    explicit scoped_output_format(output_format format);
    ~scoped_output_format();

    scoped_output_format(const scoped_output_format&) = delete;
    scoped_output_format(scoped_output_format&&) = delete;
    auto operator=(const scoped_output_format&) -> scoped_output_format& = delete;
    auto operator=(scoped_output_format&&) -> scoped_output_format& = delete;

private:
    output_format previous_;
};

auto get_path_context() -> path_context*;

/**
 * @brief While alive, the path filter in force is off: what is read or written reads whole, and the filter is back
 *        when the scope ends. For a load that keeps the listed parts itself, and for a packed list, whose rows are
 *        read in order, so none may be held back.
 */
class path_context_pause
{
public:
    path_context_pause();
    ~path_context_pause();

    path_context_pause(const path_context_pause&) = delete;
    auto operator=(const path_context_pause&) -> path_context_pause& = delete;

private:
    path_context* paused_{};
};
void set_path_context(path_context* ctx);

/**
 * @brief Counts NVP lookups that did not find their name.
 *
 * Loading an entity probes every serializable component type by name, so most lookups
 * miss - an absent optional field is the normal case, not an error. This counts all of
 * them, however they were detected.
 *
 * Diagnostic only; nothing branches on it. Costs nothing on a successful lookup.
 */
auto failed_lookup_count() -> uint64_t;

/**
 * @brief Of those, how many were detected by catching an exception.
 *
 * Archives satisfying can_probe_names answer "absent" with a scan, so this should stay
 * near zero on the associative path. If it climbs back towards failed_lookup_count(), the
 * cheap probe has stopped being used - which is a several-fold slowdown on every scene
 * load, and is exactly what the benchmark table exists to catch.
 */
auto thrown_lookup_count() -> uint64_t;

void reset_failed_lookup_count();
void note_failed_lookup();
void note_thrown_lookup();

// Convenience function to get current deserialization path
auto get_current_deserialization_path() -> std::string;

// RAII helper for path segments
struct path_segment_guard
{
    path_segment_guard() = default;
    path_segment_guard(hpp::string_view segment);
    ~path_segment_guard();

    void push_segment(hpp::string_view segment);
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

/**
 * @brief Archives that can be asked whether a name exists without throwing.
 *
 * The associative archives report "no such name at this level" by throwing, which is the
 * expected answer for every optional field - and loading an entity probes every
 * serializable component type by name, so most answers are misses. A throw costs
 * microseconds on Windows; asking first costs a scan. Archives that expose hasNextName
 * get the cheap path, everything else keeps the original behaviour.
 */
template<typename Archive>
concept can_probe_names = requires(const Archive& ar, const char* name) {
    {
        ar.hasNextName(name)
    } -> std::same_as<bool>;
};

/**
 * @brief Archives that find a name by the hash ser20 pairs with it (NameValuePair::hash), never
 *        comparing text.
 */
template<typename Archive>
concept can_probe_keys = requires(const Archive& ar, const char* name, std::uint64_t hash) {
    {
        ar.hasNextKey(name, hash)
    } -> std::same_as<bool>;
};

/**
 * @brief Input archives that list the member keys of the current object in one pass, as the hashes
 *        NameValuePair::hash holds, without moving the read position.
 *
 * A loader that would ask for many names one by one - loading an entity asks for every serializable
 * component type - reads which ones the object has instead, and asks only for those.
 */
template<typename Archive>
concept can_list_member_keys = requires(const Archive& ar) { ar.forEachMemberKey([](std::uint64_t) {}); };

/**
 * @brief Archives that can return to a node after a load or save inside it threw.
 *
 * A throw between entering a node and finishing it leaves the archive inside that node, so
 * whoever catches it and carries on reads - or writes - its next names at the wrong level,
 * where a load finds every one of them absent. Archives that report their depth are put back
 * where the failed load or save started instead.
 */
template<typename Archive>
concept can_restore_nodes = requires(Archive& ar, std::size_t depth) {
    {
        ar.getNodeDepth()
    } -> std::same_as<std::size_t>;
    ar.restoreNodeDepth(depth);
};

/**
 * @brief Archives that read or write values in step, with no names, inside a packed list's rows
 *        (serialization/types/packed.hpp).
 *
 * There a value that fails leaves every later value in the wrong bytes, so the failure is passed on and
 * the whole list fails, instead of loading or saving askew.
 */
template<typename Archive>
concept can_hold_rows = requires(const Archive& ar) {
    {
        ar.isInRows()
    } -> std::same_as<bool>;
};

/**
 * @brief Input archives that say, per document, whether a load that throws is logged.
 *
 * A compiled file only the engine writes, read by the types that wrote it, reports one: the data and the
 * code disagree, and nothing else would say so. A JSON source can hold what another build wrote, and is
 * read quietly, as are the documents made from it.
 */
template<typename Archive>
concept can_report_failed_loads = requires(const Archive& ar) {
    {
        ar.reportsFailedLoads()
    } -> std::same_as<bool>;
};

template<typename Archive, typename T>
inline auto try_serialize_direct(Archive& ar,
                          ser20::NameValuePair<T>&& t,
                          const hpp::source_location& loc = hpp::source_location::current()) -> bool
{
    if constexpr(is_loading_archive<Archive>() && can_probe_keys<Archive>)
    {
        // A name made at run time is hashed once, for the probe and the load.
        t.hash = ser20::hash_of_name(t.name, t.hash);
        // Absent is ordinary, not exceptional. Answering it here keeps the throw for
        // what it is meant for: a name that is present but whose contents will not load.
        if(!ar.hasNextKey(t.name, t.hash))
        {
            serialization::note_failed_lookup();
            return false;
        }
    }
    else if constexpr(is_loading_archive<Archive>() && can_probe_names<Archive>)
    {
        if(!ar.hasNextName(t.name))
        {
            serialization::note_failed_lookup();
            return false;
        }
    }

    [[maybe_unused]] std::size_t depth{};
    if constexpr(can_restore_nodes<Archive>)
    {
        depth = ar.getNodeDepth();
    }

    try
    {
        ar(std::forward<ser20::NameValuePair<T>>(t));
    }
    catch(const std::exception& e)
    {
        if constexpr(can_restore_nodes<Archive>)
        {
            // The caller carries on with its next name, which must be looked up - or written -
            // at the node this load or save started in, not inside the child it failed in.
            ar.restoreNodeDepth(depth);
        }
        if constexpr(can_hold_rows<Archive>)
        {
            if(ar.isInRows())
            {
                throw;
            }
        }
        serialization::note_failed_lookup();
        serialization::note_thrown_lookup();
        // A save that throws stops partway through what it was writing and nothing repairs that,
        // so it is always reported, as is a load of a compiled file the engine wrote itself.
        if constexpr(!is_loading_archive<Archive>())
        {
            serialization::log_warning(e.what(), loc);
        }
        else if constexpr(can_report_failed_loads<Archive>)
        {
            if(ar.reportsFailedLoads())
            {
                serialization::log_warning(e.what(), loc);
            }
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

namespace serialization
{
/**
 * @brief Loads the count elements of a vector from its document, stepping over each element the path filter in force
 *        leaves out (Archive::skipValue). Elements the vector does not hold yet are added as they are read.
 *
 * Elements are read in order: one left unread would put the next in its place, so a reload that keeps one material
 * slot of a model as it is would give the slot after it the kept slot's instance. The engine's vector loads of both
 * document archives (associative_archive.h, binary_archive.h) read their elements here.
 */
template<typename Archive, typename T, typename A>
void load_vector_elements(Archive& ar, std::vector<T, A>& vector, std::size_t count)
{
    auto& context = ser20::get_vector_serialization_context();
    for(std::size_t index = 0; index < count; ++index)
    {
        if(index == vector.size())
        {
            vector.emplace_back();
        }
        context.begin_element_serialization(index);
        if(context.should_serialize_element_at(index))
        {
            ar(vector[index]);
        }
        else
        {
            ar.skipValue();
        }
        context.end_element_serialization(index);
    }
}
} // namespace serialization

namespace serialization
{
/// Where the read position of ar is: what tells whether a load read past the member it was at. An archive that
/// keeps member names in a dictionary gives its position; for one whose names live in each member, the name is it.
template<typename Archive>
auto read_position(const Archive& ar) -> const void*
{
    if constexpr(requires { ar.getReadPosition(); })
    {
        return ar.getReadPosition();
    }
    else
    {
        return ar.getNodeName();
    }
}

/**
 * @brief Loads the object named key whose members are named at run time - a list keyed by name - calling
 *        load_member(name) for each member in document order, which loads it with try_load under that name.
 *
 * A member that does not load is left out and the rest still load: one the load did not read past is stepped
 * over (Archive::skipValue). False when the document has no such object.
 */
template<typename Archive, typename F>
auto load_named_members(Archive& ar, const char* key, F&& load_member) -> bool
{
    if(!ar.hasNextName(key))
    {
        return false;
    }
    ar.setNextName(key);
    ar.startNode();
    while(const char* name = ar.getNodeName())
    {
        const auto* position = read_position(ar);
        const std::string member = name;
        if(!load_member(member) && read_position(ar) == position)
        {
            ar.skipValue();
        }
    }
    ar.finishNode();
    return true;
}
} // namespace serialization

template<typename F>
inline auto serialize_check(hpp::string_view name, F&& serialize_callback) -> bool
{
    auto path_ctx = serialization::get_path_context();
    if(path_ctx)
    {
        serialization::path_segment_guard guard(name);
        // By reference: the path is maintained incrementally and lives until the guard
        // pops. Copying it here cost an allocation per property of every entity.
        const auto& path = path_ctx->get_current_path();
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
    ar.setNextName(name);
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

