#pragma once
#include "cereal_optional_nvp.h"


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
    return oarchive_associative_t(stream, oarchive_associative_t::Options::SmallIndent());
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
    oarchive_associative_t::Options opts(json_writer_t::kDefaultMaxDecimalPlaces, oarchive_associative_t::Options::IndentChar::space, 2);
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
