#pragma once
#include "serialization.h"

#include "archives/tagged_binary.hpp"
#include "types/packed.hpp"

#include <hpp/source_location.hpp>
#include <ser20/types/vector.hpp>

#include <istream>
#include <ostream>
#include <type_traits>

namespace ser20
{
/**
 * @brief The engine's binary archive: tagged binary documents (archives/tagged_binary.hpp).
 *
 * It reads by name like the JSON archive, so one SAVE / LOAD serves both: a field may be absent, added or
 * skipped, and the code has no branch for the format. Compiled assets, compiled scenes and prefabs, and
 * copies made inside the process are binary documents.
 */
using oarchive_binary_t = TaggedBinaryOutputArchive;
using iarchive_binary_t = TaggedBinaryInputArchive;
} // namespace ser20

static_assert(can_probe_keys<ser20::iarchive_binary_t>,
              "The binary input archive must answer whether a name exists without throwing.");
static_assert(can_restore_nodes<ser20::iarchive_binary_t>,
              "The binary input archive must be able to leave the nodes a failed load entered.");
static_assert(can_restore_nodes<ser20::oarchive_binary_t>,
              "The binary output archive must be able to close the nodes a failed save opened.");

namespace serialization
{
/**
 * @brief Sizes values for count elements read from a node with bytes_left bytes: at once when that costs no more
 *        than a few times those bytes, else only to the elements it holds, growing as elements are read.
 *
 * A count is never more than the bytes it is read from, an element taking at least one. An element far larger in
 * memory than in the document would still let a count a broken document overstates allocate many times the
 * document: such a count costs only the elements that are there before the load fails.
 */
template<class V>
void size_for_elements(V& values, std::size_t count, std::size_t bytes_left)
{
    using element = typename V::value_type;
    constexpr uint64_t allowed_growth = 16;
    constexpr uint64_t allowed_slack = 4096;
    // In 64 bits, which a 32-bit size_t (the browser) would wrap in.
    const auto needed = uint64_t(count) * sizeof(element);
    if(count <= bytes_left && needed <= uint64_t(bytes_left) * allowed_growth + allowed_slack)
    {
        values.resize(count);
    }
    else
    {
        values.resize(std::min(values.size(), count));
    }
}
} // namespace serialization

namespace ser20
{
/**
 * @brief Loads a vector from a binary document, stepping over the elements the path filter in force
 *        leaves out (serialization::load_vector_elements).
 *
 * A vector of numbers written as one blob is read whole, and only the elements the filter lets through
 * are kept. Chosen over ser20's loads for this archive as the more specialized one; vectors of bool
 * keep ser20's.
 */
template<class T, class A>
inline void SER20_LOAD_FUNCTION_NAME(iarchive_binary_t& ar, std::vector<T, A>& vector)
    requires(!std::is_same_v<T, bool>)
{
    size_type size{};
    ar(make_size_tag(size));
    const auto count = static_cast<std::size_t>(size);

    if constexpr(std::is_arithmetic_v<T>)
    {
        if(ar.isBlobNext())
        {
            // A blob holds the numbers themselves: a count past its bytes is refused before it sizes anything.
            if(count > ar.remainingBytes() / sizeof(T))
            {
                serialization::tagged::detail::fail("A list in the binary document counts more numbers than it holds.");
            }
            vector.resize(count);
            const auto* path = serialization::get_path_context();
            if(path == nullptr || !path->is_recording())
            {
                ar.loadBinaryValue(vector.data(), vector.size() * sizeof(T));
                return;
            }
            auto& context = get_vector_serialization_context();
            std::vector<T> loaded(vector.size());
            ar.loadBinaryValue(loaded.data(), loaded.size() * sizeof(T));
            for(std::size_t index = 0; index < vector.size(); ++index)
            {
                context.begin_element_serialization(index);
                if(context.should_serialize_element_at(index))
                {
                    vector[index] = loaded[index];
                }
                context.end_element_serialization(index);
            }
            return;
        }
    }

    serialization::size_for_elements(vector, count, ar.remainingBytes());
    serialization::load_vector_elements(ar, vector, count);
}

// A packed list is one value of its own type, not a node of the element type: no node around it.
template<class V>
inline void prologue(oarchive_binary_t&, serialization::packed_list<V> const&)
{
}
template<class V>
inline void prologue(iarchive_binary_t&, serialization::packed_list<V> const&)
{
}
template<class V>
inline void epilogue(oarchive_binary_t&, serialization::packed_list<V> const&)
{
}
template<class V>
inline void epilogue(iarchive_binary_t&, serialization::packed_list<V> const&)
{
}

/// A packed list: the elements' values in rows.
template<class V>
inline void SER20_SAVE_FUNCTION_NAME(oarchive_binary_t& ar, serialization::packed_list<V> const& list)
{
    const serialization::path_context_pause unfiltered;
    const auto& values = *list.values;
    ar.savePacked(values.size(),
                  [&]()
                  {
                      for(const auto& value : values)
                      {
                          ar(value);
                      }
                  });
}

/// Loads into a list of its own and keeps it only when every row read: a refused list leaves the values as they were.
template<class V>
inline void SER20_LOAD_FUNCTION_NAME(iarchive_binary_t& ar, serialization::packed_list<V>& list)
{
    const serialization::path_context_pause unfiltered;
    V loaded;
    ar.loadPacked(
        [&](std::size_t count)
        {
            serialization::size_for_elements(loaded, count, ar.remainingBytes());
            for(std::size_t index = 0; index < count; ++index)
            {
                if(index == loaded.size())
                {
                    loaded.emplace_back();
                }
                ar(loaded[index]);
            }
        });
    *list.values = std::move(loaded);
}
} // namespace ser20

namespace serialization
{
/// Whether bytes hold a binary document.
inline auto is_binary_document(const void* data, size_t size) noexcept -> bool
{
    return tagged::is_document(data, size);
}

/// Whether the rest of the stream holds a binary document, from its first bytes; the read position stays where
/// it was. For a check that treats any other file as "not current" without a warning.
inline auto is_binary_document(std::istream& stream) -> bool
{
    auto* buffer = stream.rdbuf();
    if(buffer == nullptr)
    {
        return false;
    }
    // A stream that cannot go back is not read from: its first bytes would be gone.
    const auto start = buffer->pubseekoff(0, std::ios::cur, std::ios::in);
    if(start == std::streampos(-1))
    {
        return false;
    }
    char head[tagged::header_size]{};
    const auto read = buffer->sgetn(head, static_cast<std::streamsize>(sizeof(head)));
    const bool is_document = read == static_cast<std::streamsize>(sizeof(head)) && tagged::is_document(head, sizeof(head));
    if(buffer->pubseekpos(start, std::ios::in) != start)
    {
        stream.setstate(std::ios::failbit);
        return false;
    }
    return is_document;
}

/**
 * @brief Writes the binary document fn saves into the stream, which must not translate line ends (a
 *        string stream, or a file opened with std::ios::binary). False when the stream failed, or when
 *        fn returns a bool and it is false (a try_save that failed).
 *
 * output_format::binary is in force while fn saves, so a document written inside it (write_document)
 * is binary too.
 */
template<typename F>
auto write_binary(std::ostream& stream, F&& fn) -> bool
{
    bool saved = true;
    {
        const scoped_output_format binary(output_format::binary);
        ser20::oarchive_binary_t ar(stream);
        if constexpr(std::is_void_v<std::invoke_result_t<F, ser20::oarchive_binary_t&>>)
        {
            std::forward<F>(fn)(ar);
        }
        else
        {
            saved = static_cast<bool>(std::forward<F>(fn)(ar));
        }
    }
    // A file stream holds the end of the document in its buffer until it is flushed: a write that fails
    // then is a failed save too.
    stream.flush();
    return saved && stream.good();
}

namespace detail
{
/// Calls fn with an archive made of args; false when either throws. report_failed_loads says whether that is
/// logged, and whether try_load logs a value that will not load (TaggedBinaryInputArchive::reportsFailedLoads).
template<typename F, typename... Args>
auto read_binary_with(const hpp::source_location& loc, bool report_failed_loads, F&& fn, Args&&... args) -> bool
{
    try
    {
        ser20::iarchive_binary_t ar(std::forward<Args>(args)...);
        ar.reportFailedLoads(report_failed_loads);
        return static_cast<bool>(std::forward<F>(fn)(ar));
    }
    catch(const tagged::not_a_document&)
    {
        // A file of an older format is not current: its caller decides what that means, with nothing to warn about.
        return false;
    }
    catch(const std::exception& e)
    {
        if(report_failed_loads)
        {
            log_warning(e.what(), loc);
        }
        return false;
    }
}
} // namespace detail

/**
 * @brief Reads the compiled file's binary document in the rest of the stream and calls fn with its archive.
 *        Returns what fn returns, or false: quietly when the stream holds no binary document (a file of an
 *        older format, which its caller treats as not current), with the reason logged when the document is
 *        broken or a load fn makes throws. A value that will not load is logged too: only the engine writes
 *        these files, from the types that read them.
 */
template<typename F>
auto read_binary(std::istream& stream, F&& fn, const hpp::source_location& loc = hpp::source_location::current())
    -> bool
{
    return detail::read_binary_with(loc, true, std::forward<F>(fn), stream);
}

/// Reads the binary document in the bytes, in place, as read_binary(std::istream&) reads one from a stream.
template<typename F>
auto read_binary(const void* data,
                 size_t size,
                 F&& fn,
                 const hpp::source_location& loc = hpp::source_location::current()) -> bool
{
    return detail::read_binary_with(loc, true, std::forward<F>(fn), static_cast<const uint8_t*>(data), size);
}

/**
 * @brief Reads only the start of the binary document in the rest of a seekable stream - its header, its
 *        dictionary and at most head_size bytes of its values - as read_binary reads all of it.
 *
 * For a check that needs a document's first fields and not its bulk: a node the bytes read cut short
 * ends where they do, so its count reads but what lies past them is absent. It fails quietly, a value
 * the bytes cut short included: a caller that needs more reads the whole document then.
 */
template<typename F>
auto read_binary_head(std::istream& stream,
                      size_t head_size,
                      F&& fn,
                      const hpp::source_location& loc = hpp::source_location::current()) -> bool
{
    return detail::read_binary_with(loc, false, std::forward<F>(fn), stream, head_size);
}
} // namespace serialization
