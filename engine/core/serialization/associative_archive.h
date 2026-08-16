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
