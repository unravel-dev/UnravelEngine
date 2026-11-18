#pragma once
#include "cereal_optional_nvp.h"
#include "serialization.h"


#define SER20_ASSOCIATIVE_ARCHIVE_XML 0
#define SER20_ASSOCIATIVE_ARCHIVE_SIMDJSON 1
#define SER20_ASSOCIATIVE_ARCHIVE_RAPIDJSON 2
#define SER20_ASSOCIATIVE_ARCHIVE_YAML 3

#define SER20_ASSOCIATIVE_ARCHIVE SER20_ASSOCIATIVE_ARCHIVE_SIMDJSON

namespace ser20
{
class membuf : public std::streambuf
{
public:
    membuf(const uint8_t* buf, size_t size)
    {
        auto cbegin = reinterpret_cast<char*>(const_cast<uint8_t*>(buf));
        this->setg(cbegin, cbegin, cbegin + size);
    }

    membuf(const char* buf, size_t size)
    {
        auto cbegin = const_cast<char*>(buf);
        this->setg(cbegin, cbegin, cbegin + size);
    }
};
}

#if SER20_ASSOCIATIVE_ARCHIVE == SER20_ASSOCIATIVE_ARCHIVE_XML

#include <ser20/archives/xml.hpp>
namespace ser20
{
using oarchive_associative_t = XMLOutputArchive;
using iarchive_associative_t = XMLInputArchive;

inline auto create_oarchive_associative(std::ostream& stream)
{
    return oarchive_associative_t(stream);
}

inline auto create_iarchive_associative(std::istream& stream)
{
    return iarchive_associative_t(stream);
}

inline auto create_iarchive_associative(const uint8_t* buf, size_t len)
{
    membuf mbuf(buf, len);
    std::istream stream(&mbuf);
    return create_iarchive_associative(stream);
}

inline auto create_iarchive_associative(const char* buf, size_t len)
{
    membuf mbuf(buf, len);
    std::istream stream(&mbuf);
    return create_iarchive_associative(stream);
}

} // namespace ser20
#elif SER20_ASSOCIATIVE_ARCHIVE == SER20_ASSOCIATIVE_ARCHIVE_SIMDJSON
#include <ser20/archives/simdjson.hpp>
namespace ser20
{
using oarchive_associative_t = simd::JSONOutputArchive;
using iarchive_associative_t = simd::JSONInputArchive;

inline auto create_oarchive_associative(std::ostream& stream)
{
    using options_t = oarchive_associative_t::Options;

    // Indented by default: these are the source files, they live in the project under
    // version control, and people read and diff them. The copies the runtime parses are
    // minified by asset_compiler's write_minified_file, so that whitespace costs editor
    // save time and repository size, not load time.
    //
    // A serialization::scoped_output_format(compact) around the save opts out, for
    // documents nobody reads - clone buffers, undo snapshots, editor checkpoints.
    // NoIndent still emits one token per line, so even those stay greppable.
    const bool compact = serialization::get_output_format() == serialization::output_format::compact;
    return oarchive_associative_t(stream, compact ? options_t::NoIndent() : options_t::SmallIndent());
}

inline auto create_iarchive_associative(std::istream& stream)
{
    return iarchive_associative_t(stream);
}

inline auto create_iarchive_associative(const uint8_t* buf, size_t len)
{
    return iarchive_associative_t(buf, len);
}

inline auto create_iarchive_associative(const char* buf, size_t len)
{
    return iarchive_associative_t(buf, len);
}

} // namespace ser20

#elif SER20_ASSOCIATIVE_ARCHIVE == SER20_ASSOCIATIVE_ARCHIVE_RAPIDJSON
#include <ser20/archives/json.hpp>
namespace ser20
{
using oarchive_associative_t = JSONOutputArchive;
using iarchive_associative_t = JSONInputArchive;

inline auto create_oarchive_associative(std::ostream& stream)
{
    using json_writer_t = SER20_RAPIDJSON_NAMESPACE::PrettyWriter<SER20_RAPIDJSON_NAMESPACE::OStreamWrapper>;
    const bool compact = serialization::get_output_format() == serialization::output_format::compact;
    oarchive_associative_t::Options opts(json_writer_t::kDefaultMaxDecimalPlaces,
                                         oarchive_associative_t::Options::IndentChar::space,
                                         compact ? 0u : 2u);
    return oarchive_associative_t(stream, opts);
}

inline auto create_iarchive_associative(std::istream& stream)
{
    return iarchive_associative_t(stream);
}

inline auto create_iarchive_associative(const uint8_t* buf, size_t len)
{
    membuf mbuf(buf, len);
    std::istream stream(&mbuf);
    return create_iarchive_associative(stream);
}

inline auto create_iarchive_associative(const char* buf, size_t len)
{
    membuf mbuf(buf, len);
    std::istream stream(&mbuf);
    return create_iarchive_associative(stream);
}

} // namespace ser20
#elif SER20_ASSOCIATIVE_ARCHIVE == SER20_ASSOCIATIVE_ARCHIVE_YAML
#include "archives/yaml.hpp"
namespace ser20
{
using oarchive_associative_t = YAMLOutputArchive;
using iarchive_associative_t = YAMLInputArchive;

inline auto create_oarchive_associative(std::ostream& stream)
{
    return oarchive_associative_t(stream);
}

inline auto create_iarchive_associative(std::istream& stream)
{
    return iarchive_associative_t(stream);
}

inline auto create_iarchive_associative(const uint8_t* buf, size_t len)
{
    membuf mbuf(buf, len);
    std::istream stream(&mbuf);
    return create_iarchive_associative(stream);
}

inline auto create_iarchive_associative(const char* buf, size_t len)
{
    membuf mbuf(buf, len);
    std::istream stream(&mbuf);
    return create_iarchive_associative(stream);
}
} // namespace ser20
#endif

// The input archive must be able to answer "is this name present" without throwing.
//
// It is not a correctness requirement - try_serialize_direct falls back to catching the
// exception - which is exactly why it needs asserting. Loading an entity probes every
// serializable component type by name and most of those miss, so on an archive without
// hasNextName the fallback costs a full stack unwind per absent component: measured at
// ~2us each, ~30 per entity, and a 6x slowdown on every scene load, play start and script
// recompile. Nothing would fail; it would just quietly get slow.
//
// If this fires after switching SER20_ASSOCIATIVE_ARCHIVE, give the newly selected archive
// a hasNextName(const char*) const -> bool. It is a membership test over the current
// level, not a comparison against getNodeName(): readers skip fields without consuming
// them, so the name being asked for is often ahead of the cursor. See
// simd::JSONInputArchive::hasNextName for the reference implementation.
static_assert(is_loading_archive<ser20::iarchive_associative_t>(),
              "iarchive_associative_t must be an input archive");
static_assert(can_probe_names<ser20::iarchive_associative_t>,
              "The selected associative input archive has no non-throwing hasNextName(). "
              "See the comment above this assertion.");

// The input archive must also be able to leave the nodes a failed load entered.
//
// try_load catches a failed load and carries on, which is only correct if the archive is back
// at the node the failed load started in: otherwise every following name is looked up inside
// the failed child, reads as absent and silently keeps its default. Give a newly selected
// archive getNodeDepth() and restoreNodeDepth(); see simd::JSONInputArchive.
static_assert(can_restore_nodes<ser20::iarchive_associative_t>,
              "The selected associative input archive cannot restore its node depth after a failed load. "
              "See the comment above this assertion.");

namespace serialization
{
/**
 * @brief Whether an element of type @p T is a plain value in an associative document (a number, a string, an enum, a
 *        type saved as one value), not a node of its own: the rule the JSON reader's prologue starts nodes by.
 */
template<typename T>
inline constexpr bool is_plain_array_element = std::is_arithmetic_v<T> || std::is_same_v<T, std::string> ||
                                               ser20::traits::has_minimal_input_serialization<T, ser20::iarchive_associative_t>::value ||
                                               ser20::traits::has_minimal_base_class_serialization<
                                                   T, ser20::traits::has_minimal_input_serialization, ser20::iarchive_associative_t>::value;

/**
 * @brief Steps the reader of @p ar over the array element it is at without keeping any of it: a node is entered and
 *        left, a plain value is read into a scratch value of the element type.
 */
template<typename T>
void skip_array_element(ser20::iarchive_associative_t& ar)
{
    if constexpr(!is_plain_array_element<T>)
    {
        ar.startNode();
        ar.finishNode();
    }
    else
    {
        // Read whole, past any path filter: nothing of it is kept.
        struct path_context_restore
        {
            path_context* previous;
            ~path_context_restore()
            {
                set_path_context(previous);
            }
        } restore{get_path_context()};
        set_path_context(nullptr);
        T scratch{};
        ar(scratch);
    }
}
} // namespace serialization

namespace ser20
{
/**
 * @brief Loads a vector from an associative document as ser20's own load does, except that an element the path filter
 *        in force leaves out is stepped over.
 *
 * The reader takes array elements in order. ser20's load leaves a left-out element unread, so the next element read
 * would take the left-out one's place in the document: a reload that keeps one material slot of a model as it is would
 * give the slot after it the kept slot's instance. Chosen over ser20's load for this archive as the more specialized
 * one; vectors of bool keep ser20's.
 */
template<class T, class A>
inline void SER20_LOAD_FUNCTION_NAME(iarchive_associative_t& ar, std::vector<T, A>& vector)
    requires(!std::is_same_v<T, bool>)
{
    size_type size{};
    ar(make_size_tag(size));
    vector.resize(static_cast<std::size_t>(size));
    auto& context = get_vector_serialization_context();
    size_t index = 0;
    for(auto&& element : vector)
    {
        context.begin_element_serialization(index);
        if(context.should_serialize_element_at(index))
        {
            ar(element);
        }
        else
        {
            serialization::skip_array_element<T>(ar);
        }
        context.end_element_serialization(index);
        ++index;
    }
}
} // namespace ser20
