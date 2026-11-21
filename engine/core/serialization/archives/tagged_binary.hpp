#pragma once
#include "../name_index.h"
#include "../packed_value.h"

#include <ser20/ser20.hpp>

#include <hpp/string_view.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <istream>
#include <iterator>
#include <limits>
#include <memory>
#include <ostream>
#include <string>
#include <type_traits>
#include <vector>

/**
 * @file tagged_binary.hpp
 * @brief A binary document with the reading rules of the JSON archive: members are found by name,
 *        so a field may be absent, added, reordered or skipped, and any value is skipped in O(1).
 *
 * Layout, little-endian:
 *
 *     preamble    32 zero bytes
 *     header      u32 magic "UTB1", u32 format version, u32 dictionary offset, u32 key count
 *     root        an object node
 *     dictionary  each key of the document once: u64 name hash, varint length, the name, '\0'
 *
 *     value       [type u8][payload]
 *     member      [key varint][type u8][payload]      inside an object; the key indexes the dictionary
 *     element     [type u8][payload]                  inside an array
 *     node        [object|array][u32 body bytes][u32 count][body]
 *     packed list [packed][u32 body bytes][u32 count][rows]
 *     numbers     [numbers][element type u8][varint count][the numbers]
 *
 * A member is found by comparing the hash of the name asked for with the dictionary's hash of the
 * member's key: a number, never text. Scalars keep the width of the C++ type that wrote them and are
 * converted on read, so a field can change from float to double or from int32 to int64. Every read
 * is checked against the end of the node that holds it, so a broken document fails the load that
 * reads it instead of reading past its end or allocating what a corrupt length says.
 *
 * A packed list holds dense data - a mesh's triangles, an animation's keys - as rows: each element's
 * values one after another in the order its save wrote them, with no keys, type bytes or node headers.
 * A number takes its C++ type's bytes, a bool one byte, a string a varint length and its bytes, and a
 * container inside a row its element count as a varint; a block of raw bytes has the size its reader
 * asks for. Rows are read back by the same code that wrote them, so they are for compiled data whose
 * format version changes with that code. The list as a whole is still a value of its object: found by
 * name, skipped in O(1).
 *
 * A value made of a few numbers - a vector, a quaternion, a color (ser20::is_packed_value) - is one numbers
 * value: its element type, its count and the numbers, read back converted like any number.
 *
 * The preamble keeps an older build's positional reader safe on these files: the sizes and counts it reads
 * first are zero, so it reads nothing instead of a size made of the magic number.
 */
namespace serialization::tagged
{
/// The type byte in front of every value.
enum class value_type : uint8_t
{
    null_value,
    false_value,
    true_value,
    i8,
    u8,
    i16,
    u16,
    i32,
    u32,
    i64,
    u64,
    f32,
    f64,
    string,
    blob,
    object,
    array,
    packed,
    numbers,
};

constexpr uint8_t last_value_type = static_cast<uint8_t>(value_type::numbers);
constexpr uint32_t document_magic = 0x31425455u; // "UTB1"
constexpr uint32_t document_version = 1;
constexpr size_t preamble_size = 32;
constexpr size_t header_size = preamble_size + 16;
constexpr size_t node_header_size = 8;
constexpr uint32_t no_key = 0xFFFFFFFFu;
static_assert(no_key == name_index::none, "A key the dictionary does not hold is the name index's none.");

/// Whether bytes begin as a binary document does: with zero bytes as far as the preamble goes. A file of an older
/// format does not; a binary document cut short or overwritten still does, or has no bytes at all.
inline auto starts_as_document(const void* data, size_t size) noexcept -> bool
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    for(size_t index = 0; index < size && index < preamble_size; ++index)
    {
        if(bytes[index] != 0)
        {
            return false;
        }
    }
    return true;
}

/// Whether a buffer holds a tagged binary document: what a loader checks to choose its archive.
inline auto is_document(const void* data, size_t size) noexcept -> bool
{
    if(data == nullptr || size < header_size)
    {
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    for(size_t index = 0; index < preamble_size; ++index)
    {
        if(bytes[index] != 0)
        {
            return false;
        }
    }
    uint32_t magic = 0;
    std::memcpy(&magic, bytes + preamble_size, sizeof(magic));
    return magic == document_magic;
}

namespace detail
{
/// Bytes of a fixed-size payload by type; -1 for a length-prefixed one, -2 for a node, -3 for numbers.
constexpr int8_t fixed_payload_size[last_value_type + 1] = {0, 0, 0, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8, -1, -1, -2, -2, -2, -3};

/// Whether a type byte names a number type, the element types of a numbers value.
constexpr auto is_number_type(uint8_t type) noexcept -> bool
{
    return type >= static_cast<uint8_t>(value_type::i8) && type <= static_cast<uint8_t>(value_type::f64);
}

template<typename T>
inline auto read_raw(const uint8_t* p) noexcept -> T
{
    T value;
    std::memcpy(&value, p, sizeof(T));
    return value;
}

/// Throws what went wrong, a sentence a log can show as it is.
[[noreturn]] inline void fail(const char* what)
{
    throw ser20::Exception(what);
}
} // namespace detail

/// Thrown for bytes that are not a binary document at all - a file of an older format - rather than a broken one.
class not_a_document : public ser20::Exception
{
public:
    not_a_document() : ser20::Exception("The file is not a binary document.")
    {
    }
};

namespace detail
{

/// A size, length or count as the 32 bits a document stores it in.
inline auto to_u32(uint64_t value) -> uint32_t
{
    if(value > 0xFFFFFFFFull)
    {
        fail("A binary document cannot hold a size of 4 GiB or more.");
    }
    return static_cast<uint32_t>(value);
}

/// A varint that must end before limit.
inline auto read_varint(const uint8_t*& p, const uint8_t* limit) -> uint32_t
{
    if(p >= limit)
    {
        fail("A value in the binary document runs past the node that holds it.");
    }
    uint32_t byte = *p++;
    if(byte < 0x80)
    {
        return byte;
    }
    uint32_t value = byte & 0x7F;
    for(int shift = 7; shift <= 28; shift += 7)
    {
        if(p >= limit)
        {
            fail("A value in the binary document runs past the node that holds it.");
        }
        byte = *p++;
        value |= (byte & 0x7F) << shift;
        if(byte < 0x80)
        {
            return value;
        }
    }
    fail("A length in the binary document is too long.");
}

/// The value type that holds a C++ arithmetic type as it is.
template<typename T>
constexpr auto value_type_of() noexcept -> value_type
{
    static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8,
                  "A binary document stores numbers of 1, 2, 4 or 8 bytes.");
    static_assert(!std::is_floating_point_v<T> || sizeof(T) == 4 || sizeof(T) == 8,
                  "A binary document stores floats and doubles only.");
    if constexpr(std::is_floating_point_v<T>)
    {
        return sizeof(T) == 4 ? value_type::f32 : value_type::f64;
    }
    else if constexpr(std::is_signed_v<T>)
    {
        return sizeof(T) == 1   ? value_type::i8
               : sizeof(T) == 2 ? value_type::i16
               : sizeof(T) == 4 ? value_type::i32
                                : value_type::i64;
    }
    else
    {
        return sizeof(T) == 1   ? value_type::u8
               : sizeof(T) == 2 ? value_type::u16
               : sizeof(T) == 4 ? value_type::u32
                                : value_type::u64;
    }
}

/// Byte buffers kept per thread, so the many small documents written for copies do not each
/// allocate one. Only buffers of small documents are kept, and only a few.
class buffer_pool
{
public:
    static constexpr size_t max_kept_capacity = 256 * 1024;
    static constexpr size_t max_kept_buffers = 2;

    static auto acquire() -> std::vector<uint8_t>
    {
        auto& pool = buffers();
        if(pool.empty())
        {
            return {};
        }
        auto buffer = std::move(pool.back());
        pool.pop_back();
        buffer.clear();
        return buffer;
    }

    static void release(std::vector<uint8_t>&& buffer)
    {
        auto& pool = buffers();
        if(buffer.capacity() != 0 && buffer.capacity() <= max_kept_capacity && pool.size() < max_kept_buffers)
        {
            pool.push_back(std::move(buffer));
        }
    }

private:
    static auto buffers() -> std::vector<std::vector<uint8_t>>&
    {
        static thread_local std::vector<std::vector<uint8_t>> pool;
        return pool;
    }
};
} // namespace detail

/**
 * @brief Writes one document into a byte buffer.
 *
 * Usable on its own, e.g. to convert a JSON document; the ser20 archive below is a thin layer over it.
 */
class writer
{
public:
    writer() : out_(detail::buffer_pool::acquire())
    {
        out_.resize(4096);
        pos_ = header_size;
        put_byte(static_cast<uint8_t>(value_type::object));
        open_node(false);
    }

    ~writer()
    {
        detail::buffer_pool::release(std::move(out_));
    }

    writer(const writer&) = delete;
    auto operator=(const writer&) -> writer& = delete;

    /// The name of the next member, with its hash. Ignored inside an array.
    void key(const char* name, uint64_t hash) noexcept
    {
        next_name_ = name;
        next_hash_ = hash;
    }

    /// The name of the next member, hashed here.
    void key(const char* name) noexcept
    {
        next_name_ = name;
        next_hash_ = name != nullptr ? ser20::name_hash(name) : 0;
    }

    void value_null()
    {
        begin_value(value_type::null_value);
    }

    void value_bool(bool value)
    {
        if(row_value())
        {
            put_byte(value ? 1 : 0);
            return;
        }
        begin_value(value ? value_type::true_value : value_type::false_value);
    }

    template<typename T>
    void value_number(T value)
    {
        begin_value(detail::value_type_of<T>());
        std::memcpy(reserve(sizeof(T)), &value, sizeof(T));
        pos_ += sizeof(T);
    }

    void value_string(const char* data, size_t size)
    {
        begin_value(value_type::string);
        put_bytes(data, size);
    }

    /// A value of count numbers. In a row, the numbers alone.
    template<typename T>
    void value_numbers(const T* data, size_t count)
    {
        if(row_value())
        {
            put_raw(data, count * sizeof(T));
            return;
        }
        begin_value(value_type::numbers);
        put_byte(static_cast<uint8_t>(detail::value_type_of<T>()));
        put_varint(detail::to_u32(count));
        put_raw(data, count * sizeof(T));
    }

    /// Raw bytes. In a row they carry no length: their reader asks for the size it was written with.
    void value_blob(const void* data, size_t size)
    {
        if(row_value())
        {
            put_raw(data, size);
            return;
        }
        begin_value(value_type::blob);
        put_bytes(data, size);
    }

    void begin_object()
    {
        if(row_value())
        {
            row_nodes_.push_back({0, pos_});
            return;
        }
        begin_value(value_type::object);
        open_node(false);
    }

    void begin_array()
    {
        if(row_value())
        {
            row_nodes_.push_back({0, pos_});
            return;
        }
        begin_value(value_type::array);
        open_node(true);
    }

    /// Turns the node just begun into an array: what a ser20 size tag does.
    void make_array() noexcept
    {
        if(in_rows())
        {
            return;
        }
        auto& node = nodes_.back();
        if(!node.is_array)
        {
            out_[node.type_pos] = static_cast<uint8_t>(value_type::array);
            node.is_array = true;
        }
    }

    /// The count a reader is told for the current node: a container's element count. In a row it is
    /// written where the elements start.
    void set_count(uint64_t count)
    {
        if(in_rows())
        {
            put_varint(detail::to_u32(count));
            if(!row_nodes_.empty())
            {
                row_nodes_.back() = {count, pos_};
            }
            return;
        }
        nodes_.back().size_tag = count;
        nodes_.back().has_size_tag = true;
    }

    /**
     * @brief Starts a packed list of count elements: what is written until end_packed goes into its rows.
     *
     * A packed list inside a row is its count followed by its elements' rows.
     */
    void begin_packed(uint64_t count)
    {
        if(row_value())
        {
            put_varint(detail::to_u32(count));
            row_nodes_.push_back({count, pos_});
            return;
        }
        begin_value(value_type::packed);
        open_node(true);
        nodes_.back().size_tag = count;
        nodes_.back().has_size_tag = true;
        rows_ = true;
    }

    void end_packed()
    {
        if(!row_nodes_.empty())
        {
            close_row_node();
            return;
        }
        const auto count = nodes_.back().size_tag;
        const auto start = nodes_.back().header_pos + node_header_size;
        rows_ = false;
        next_name_ = nullptr;
        end_node();
        // Checked once the list is closed, so the document stays whole around a list that is refused.
        check_row_bytes(count, start);
    }

    void end_node()
    {
        if(!row_nodes_.empty())
        {
            close_row_node();
            return;
        }
        const auto node = nodes_.back();
        nodes_.pop_back();
        const auto body = detail::to_u32(pos_ - (node.header_pos + node_header_size));
        const auto count = node.has_size_tag ? detail::to_u32(node.size_tag) : node.count;
        std::memcpy(out_.data() + node.header_pos, &body, sizeof(body));
        std::memcpy(out_.data() + node.header_pos + 4, &count, sizeof(count));
    }

    /// Whether a packed list's rows are being written.
    auto writes_rows() const noexcept -> bool
    {
        return rows_;
    }

    /// The nodes open: the document's, and those open inside the rows being written.
    auto node_depth() const noexcept -> size_t
    {
        return nodes_.size() + row_nodes_.size();
    }

    /**
     * @brief Closes nodes until node_depth() is depth: what a save that failed partway left open, so the
     *        values saved after it go where they belong.
     *
     * A packed list closed this way holds only some of its rows: its count is set to one no reader takes,
     * so the list is refused rather than read askew.
     */
    void restore_node_depth(size_t depth)
    {
        while(node_depth() > depth)
        {
            if(!row_nodes_.empty())
            {
                row_nodes_.pop_back();
                continue;
            }
            if(rows_)
            {
                // The innermost node is the packed list whose rows were being written.
                nodes_.back().size_tag = 0xFFFFFFFFu;
                rows_ = false;
            }
            end_node();
        }
        next_name_ = nullptr;
    }

    /// Closes the open nodes and writes the dictionary and the header. The document is complete after it.
    auto finish() -> const std::vector<uint8_t>&
    {
        if(finished_)
        {
            return out_;
        }
        finished_ = true;
        while(!nodes_.empty())
        {
            end_node();
        }
        const auto dictionary = detail::to_u32(pos_);
        for(const auto& key : keys_)
        {
            std::memcpy(reserve(sizeof(key.hash)), &key.hash, sizeof(key.hash));
            pos_ += sizeof(key.hash);
            put_bytes(names_.data() + key.text_offset, key.text_size);
            put_byte(0);
        }
        out_.resize(pos_);
        const uint32_t header[4] = {document_magic,
                                    document_version,
                                    dictionary,
                                    static_cast<uint32_t>(keys_.size())};
        std::memset(out_.data(), 0, preamble_size);
        std::memcpy(out_.data() + preamble_size, header, sizeof(header));
        return out_;
    }

    /// The finished document's bytes, taken out of the writer.
    auto take() -> std::vector<uint8_t>
    {
        finish();
        return std::move(out_);
    }

private:
    struct node
    {
        size_t type_pos = 0;
        size_t header_pos = 0;
        uint32_t count = 0;
        uint32_t unnamed = 0;
        uint64_t size_tag = 0;
        bool has_size_tag = false;
        bool is_array = false;
    };

    struct dictionary_key
    {
        uint64_t hash = 0;
        const char* last_pointer = nullptr;
        uint32_t text_offset = 0;
        uint32_t text_size = 0;
    };

    auto reserve(size_t size) -> uint8_t*
    {
        if(pos_ + size > out_.size())
        {
            out_.resize(std::max(out_.size() * 2, pos_ + size));
        }
        return out_.data() + pos_;
    }

    void put_byte(uint8_t value)
    {
        *reserve(1) = value;
        ++pos_;
    }

    void put_varint(uint32_t value)
    {
        uint8_t* p = reserve(5);
        uint8_t* start = p;
        while(value >= 0x80)
        {
            *p++ = static_cast<uint8_t>(value | 0x80);
            value >>= 7;
        }
        *p++ = static_cast<uint8_t>(value);
        pos_ += static_cast<size_t>(p - start);
    }

    void put_raw(const void* data, size_t size)
    {
        if(size != 0)
        {
            std::memcpy(reserve(size), data, size);
            pos_ += size;
        }
    }

    void put_bytes(const void* data, size_t size)
    {
        put_varint(detail::to_u32(size));
        put_raw(data, size);
    }

    /// Whether a packed list's rows are being written: values go in without keys or type bytes.
    auto in_rows() const noexcept -> bool
    {
        return rows_;
    }

    /// Whether the value begun goes into rows, which hold no names: the name set for it is dropped there, so it
    /// cannot name a value written after the list.
    auto row_value() noexcept -> bool
    {
        if(!rows_)
        {
            return false;
        }
        next_name_ = nullptr;
        return true;
    }

    /// A list of count elements whose rows start at start must hold a byte per element: a reader refuses a larger
    /// count, which a broken document could otherwise use to size a container.
    void check_row_bytes(uint64_t count, size_t start) const
    {
        if(count > pos_ - start)
        {
            detail::fail("A packed list's elements must each save at least one byte.");
        }
    }

    void close_row_node()
    {
        const auto node = row_nodes_.back();
        row_nodes_.pop_back();
        check_row_bytes(node.count, node.start);
    }

    /// The dictionary index of a key, added when new. A known hash brought by a new pointer has its
    /// text compared once, so two names sharing a hash stop the save instead of merging.
    auto intern(const char* name, uint64_t hash) -> uint32_t
    {
        const auto found = index_.find(hash);
        if(found != name_index::none)
        {
            auto& key = keys_[found];
            if(key.last_pointer != name)
            {
                const auto size = std::strlen(name);
                if(size != key.text_size || std::memcmp(names_.data() + key.text_offset, name, size) != 0)
                {
                    detail::fail("Two member names share a hash, so the binary document is not saved.");
                }
                key.last_pointer = name;
            }
            return found;
        }
        dictionary_key key;
        key.hash = hash;
        key.last_pointer = name;
        key.text_offset = detail::to_u32(names_.size());
        key.text_size = detail::to_u32(std::strlen(name));
        names_.append(name, key.text_size);
        keys_.push_back(key);
        const auto index = detail::to_u32(keys_.size() - 1);
        index_.insert(hash, index);
        return index;
    }

    void begin_value(value_type type)
    {
        if(row_value())
        {
            return;
        }
        auto& current = nodes_.back();
        if(!current.is_array)
        {
            const char* name = next_name_;
            uint64_t hash = next_hash_;
            next_name_ = nullptr;
            if(name == nullptr)
            {
                // The JSON archive names unnamed members value0, value1, ... counting only those.
                unnamed_name_ = "value" + std::to_string(current.unnamed++);
                name = unnamed_name_.c_str();
                hash = ser20::name_hash(unnamed_name_.data(), unnamed_name_.size());
            }
            put_varint(intern(name, hash));
        }
        ++current.count;
        put_byte(static_cast<uint8_t>(type));
    }

    void open_node(bool is_array)
    {
        node current;
        current.type_pos = pos_ - 1;
        current.header_pos = pos_;
        current.is_array = is_array;
        reserve(node_header_size);
        pos_ += node_header_size;
        nodes_.push_back(current);
    }

    std::vector<uint8_t> out_;
    size_t pos_ = 0;
    std::vector<node> nodes_;
    std::vector<dictionary_key> keys_;
    name_index index_;
    std::string names_;
    std::string unnamed_name_;
    const char* next_name_ = nullptr;
    uint64_t next_hash_ = 0;

    /// An object, container or inner packed list open inside the rows being written: the element count it
    /// wrote (0 for an object) and where its elements start.
    struct row_node
    {
        uint64_t count = 0;
        size_t start = 0;
    };
    std::vector<row_node> row_nodes_;
    bool rows_ = false;
    bool finished_ = false;
};

/// The rest of a stream's bytes: in one read when its size is known, in blocks otherwise.
inline auto read_rest(std::istream& stream) -> std::vector<uint8_t>
{
    std::vector<uint8_t> bytes;
    auto* buffer = stream.rdbuf();
    if(buffer == nullptr)
    {
        return bytes;
    }
    const auto start = buffer->pubseekoff(0, std::ios::cur, std::ios::in);
    const auto end = start == std::streampos(-1) ? start : buffer->pubseekoff(0, std::ios::end, std::ios::in);
    if(start != std::streampos(-1) && end != std::streampos(-1) && end >= start &&
       buffer->pubseekpos(start, std::ios::in) == start)
    {
        bytes.resize(static_cast<size_t>(end - start));
        const auto read =
            buffer->sgetn(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        bytes.resize(static_cast<size_t>(std::max<std::streamsize>(read, 0)));
        return bytes;
    }
    constexpr size_t block = 64 * 1024;
    for(;;)
    {
        const auto at = bytes.size();
        bytes.resize(at + block);
        const auto read = buffer->sgetn(reinterpret_cast<char*>(bytes.data() + at), block);
        bytes.resize(at + static_cast<size_t>(std::max<std::streamsize>(read, 0)));
        if(read < static_cast<std::streamsize>(block))
        {
            return bytes;
        }
    }
}

/**
 * @brief Reads a document in place: nothing is copied but the values asked for.
 */
class reader
{
public:
    /// One value: its member key (no_key in an array), type, payload and where it ends.
    struct entry
    {
        uint32_t key = no_key;
        value_type type = value_type::null_value;
        const uint8_t* payload = nullptr;
        const uint8_t* end = nullptr;
    };

    /// An open object, array or packed list: its body, the read position and the count it was written with.
    struct frame
    {
        const uint8_t* begin = nullptr;
        const uint8_t* end = nullptr;
        const uint8_t* cursor = nullptr;
        uint32_t count = 0;
        /// Objects and inner lists the element being read has opened inside a packed list's rows.
        uint32_t row_nodes = 0;
        /// An array or a packed list: its values have no keys.
        bool is_array = false;
        /// A packed list: its values have no type bytes either.
        bool is_rows = false;
    };

    reader(const uint8_t* data, size_t size)
    {
        open(data, size);
    }

    /// Reads a document out of a stream, keeping its bytes.
    explicit reader(std::istream& stream) : owned_(read_rest(stream))
    {
        open(owned_.data(), owned_.size());
    }

    /**
     * @brief Reads the start of the document in the rest of a seekable stream: its header, its dictionary and at
     *        most head_size bytes of its values, for a check that needs its first values and not its bulk.
     *
     * A node the bytes read cut short can still be entered for its count, and it ends where they end, so what
     * lies past them reads as absent; a value they cut short fails to read.
     */
    reader(std::istream& stream, size_t head_size)
    {
        auto* buffer = stream.rdbuf();
        const auto start = buffer != nullptr ? buffer->pubseekoff(0, std::ios::cur, std::ios::in) : std::streampos(-1);
        const auto end = start != std::streampos(-1) ? buffer->pubseekoff(0, std::ios::end, std::ios::in)
                                                     : std::streampos(-1);
        if(end == std::streampos(-1) || end < start)
        {
            detail::fail("The start of a binary document is read only from a stream that can seek.");
        }
        const auto size = static_cast<size_t>(end - start);
        uint8_t header[header_size]{};
        const size_t header_read = std::min<size_t>(size, sizeof(header));
        if(!read_at(*buffer, start, 0, header, header_read) || !is_document(header, header_read))
        {
            refuse(header, header_read);
        }
        const auto dictionary = check_header(header, size);
        dictionary_.resize(size - dictionary);
        owned_.resize(std::min<size_t>(dictionary, header_size + 1 + node_header_size + head_size));
        if(!read_at(*buffer, start, dictionary, dictionary_.data(), dictionary_.size()) ||
           !read_at(*buffer, start, 0, owned_.data(), owned_.size()))
        {
            detail::fail("The binary document is shorter than its header says.");
        }
        read_dictionary(dictionary_.data(), dictionary_.data() + dictionary_.size(), header);
        open_root(owned_.data(), owned_.size(), dictionary);
    }

    auto top() const noexcept -> const frame&
    {
        return frames_.back();
    }

    auto depth() const noexcept -> size_t
    {
        return frames_.size();
    }

    auto key_count() const noexcept -> size_t
    {
        return hashes_.size();
    }

    auto name_of(uint32_t key) const noexcept -> const char*
    {
        return names_[key];
    }

    auto hash_of(uint32_t key) const noexcept -> uint64_t
    {
        return hashes_[key];
    }

    /// The entry at the read position of the current node, which stays there.
    auto peek() const -> entry
    {
        const auto& current = top();
        if(current.cursor >= current.end)
        {
            detail::fail("A value was read past the end of a node in the binary document.");
        }
        return read_entry(current.cursor, current.end, !current.is_array);
    }

    /// The entry at the read position; the position moves past it.
    auto take() -> entry
    {
        auto entry = peek();
        frames_.back().cursor = entry.end;
        found_from_ = nullptr;
        return entry;
    }

    /// Whether the read position is at the end of the current node.
    auto at_end() const noexcept -> bool
    {
        return top().cursor >= top().end;
    }

    /// The key at the read position, or no_key at the end of a node or inside an array.
    auto key_at_cursor() const -> uint32_t
    {
        const auto& current = top();
        if(current.is_array || current.cursor >= current.end)
        {
            return no_key;
        }
        const uint8_t* p = current.cursor;
        return read_key(p, current.end);
    }

    /**
     * @brief Whether the current object has a member with this name hash. The read position stays where it
     *        is, as the JSON archive's probe leaves it; the member found is remembered, so the seek that
     *        follows takes it at once.
     */
    auto contains(uint64_t hash) -> bool
    {
        const uint8_t* found = locate(hash);
        if(found == nullptr)
        {
            return false;
        }
        remember(hash, frames_.back().cursor, found);
        return true;
    }

    /// Moves the read position of the current object onto the member with this name hash; false when the
    /// object has no such member.
    auto seek(uint64_t hash) -> bool
    {
        const uint8_t* found = locate(hash);
        if(found == nullptr)
        {
            return false;
        }
        frames_.back().cursor = found;
        remember(hash, found, found);
        return true;
    }

    /// Opens the node at the read position.
    void enter()
    {
        const auto entry = peek();
        if(entry.type != value_type::object && entry.type != value_type::array)
        {
            detail::fail("A value read as an object in the binary document is not an object.");
        }
        found_from_ = nullptr;
        push(entry.payload, entry.type == value_type::array);
    }

    /// Closes the current node; the read position of the node around it moves past it.
    void leave()
    {
        found_from_ = nullptr;
        const auto end = frames_.back().end;
        frames_.pop_back();
        if(!frames_.empty())
        {
            frames_.back().cursor = end;
        }
    }

    /// Opens the packed list or array at the read position; its count is top().count.
    void enter_list()
    {
        const auto entry = peek();
        if(entry.type != value_type::packed && entry.type != value_type::array)
        {
            detail::fail("A value read as a list in the binary document is not a list.");
        }
        found_from_ = nullptr;
        push(entry.payload, true);
        frames_.back().is_rows = entry.type == value_type::packed;
    }

    /// Whether the current node is a packed list: values are read as rows.
    auto in_rows() const noexcept -> bool
    {
        return frames_.back().is_rows;
    }

    /// The next bytes of the rows being read.
    auto row_bytes(size_t size) -> const uint8_t*
    {
        auto& current = frames_.back();
        if(static_cast<size_t>(current.end - current.cursor) < size)
        {
            detail::fail("A packed list in the binary document ends inside a row, so its elements read more than was saved.");
        }
        const uint8_t* at = current.cursor;
        current.cursor += size;
        return at;
    }

    /// A count or length inside the rows being read. No larger than the bytes left, which every counted
    /// value needs at least one of.
    auto row_count() -> uint32_t
    {
        auto& current = frames_.back();
        const auto count = detail::read_varint(current.cursor, current.end);
        if(count > static_cast<size_t>(current.end - current.cursor))
        {
            detail::fail("A packed list in the binary document counts more values than it holds.");
        }
        return count;
    }

    /// An object or inner list opened inside a row: rows have no node headers, so only its depth is kept.
    void row_enter() noexcept
    {
        ++frames_.back().row_nodes;
    }

    void row_leave()
    {
        auto& current = frames_.back();
        if(current.row_nodes == 0)
        {
            detail::fail("A packed list in the binary document closes a node it never opened.");
        }
        --current.row_nodes;
    }

    /// The nodes open: the frames, and the nodes open inside the rows of the innermost one.
    auto node_depth() const noexcept -> size_t
    {
        return frames_.size() + frames_.back().row_nodes;
    }

    /// Closes nodes until node_depth() is depth.
    void restore_node_depth(size_t depth)
    {
        while(!frames_.empty() && node_depth() > depth)
        {
            if(frames_.back().row_nodes > 0)
            {
                --frames_.back().row_nodes;
            }
            else
            {
                leave();
            }
        }
    }

    /// Calls fn with the name hash of every member of the current object, in document order, without
    /// moving the read position. Nothing inside an array or a packed list.
    template<typename F>
    void for_each_member_hash(F&& fn) const
    {
        const auto& current = frames_.back();
        if(current.is_array)
        {
            return;
        }
        const uint8_t* p = current.begin;
        while(p < current.end)
        {
            const auto value = read_entry(p, current.end, true);
            fn(hashes_[value.key]);
            p = value.end;
        }
    }

    /// A number of any stored width or kind, converted.
    template<typename T>
    static auto to_number(const entry& value) -> T
    {
        const uint8_t* p = value.payload;
        switch(value.type)
        {
            case value_type::i8:
                return from_signed<T>(detail::read_raw<int8_t>(p));
            case value_type::u8:
                return from_unsigned<T>(detail::read_raw<uint8_t>(p));
            case value_type::i16:
                return from_signed<T>(detail::read_raw<int16_t>(p));
            case value_type::u16:
                return from_unsigned<T>(detail::read_raw<uint16_t>(p));
            case value_type::i32:
                return from_signed<T>(detail::read_raw<int32_t>(p));
            case value_type::u32:
                return from_unsigned<T>(detail::read_raw<uint32_t>(p));
            case value_type::i64:
                return from_signed<T>(detail::read_raw<int64_t>(p));
            case value_type::u64:
                return from_unsigned<T>(detail::read_raw<uint64_t>(p));
            case value_type::f32:
                return from_fraction<T>(detail::read_raw<float>(p));
            case value_type::f64:
                return from_fraction<T>(detail::read_raw<double>(p));
            default:
                detail::fail("A value read as a number in the binary document is not a number.");
        }
    }

    // A number converts as the JSON archive converts what it reads, so a document converted from JSON loads as
    // the JSON does: any number into a float or a double; into an integer field, a whole number of a sign the
    // field holds (a narrower field keeps its low bits, as with JSON). Anything else is refused.

    template<typename T>
    static auto from_signed(int64_t value) -> T
    {
        if constexpr(std::is_unsigned_v<T>)
        {
            if(value < 0)
            {
                detail::fail("A negative number in the binary document is read into a field that holds no sign.");
            }
        }
        return static_cast<T>(value);
    }

    template<typename T>
    static auto from_unsigned(uint64_t value) -> T
    {
        if constexpr(std::is_signed_v<T> && std::is_integral_v<T>)
        {
            if(value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
            {
                detail::fail("A number in the binary document does not fit the field it is read into.");
            }
        }
        return static_cast<T>(value);
    }

    template<typename T>
    static auto from_fraction(double value) -> T
    {
        if constexpr(std::is_integral_v<T>)
        {
            detail::fail("A fraction in the binary document is read into a field that holds whole numbers.");
        }
        else
        {
            return static_cast<T>(value);
        }
    }

    /// A numbers value: its element type, its count and where its numbers start.
    struct numbers
    {
        value_type element = value_type::f32;
        uint32_t count = 0;
        const uint8_t* data = nullptr;
    };

    static auto numbers_of(const entry& value) -> numbers
    {
        if(value.type != value_type::numbers)
        {
            detail::fail("A value read as numbers in the binary document is not a list of numbers.");
        }
        numbers result;
        const uint8_t* p = value.payload;
        result.element = static_cast<value_type>(*p++);
        result.count = detail::read_varint(p, value.end);
        result.data = p;
        return result;
    }

    /// The bytes of a string or blob, in place.
    static auto bytes_of(const entry& value) -> hpp::string_view
    {
        if(value.type != value_type::string && value.type != value_type::blob)
        {
            detail::fail("A value read as text in the binary document is not text.");
        }
        const uint8_t* p = value.payload;
        const auto size = detail::read_varint(p, value.end);
        return {reinterpret_cast<const char*>(p), size};
    }

private:
    /// Reads size bytes at offset from start; false when the stream holds fewer.
    static auto read_at(std::streambuf& buffer, std::streampos start, size_t offset, uint8_t* out, size_t size) -> bool
    {
        if(size == 0)
        {
            return true;
        }
        const auto at = start + static_cast<std::streamoff>(offset);
        if(buffer.pubseekpos(at, std::ios::in) != at)
        {
            return false;
        }
        return buffer.sgetn(reinterpret_cast<char*>(out), static_cast<std::streamsize>(size)) ==
               static_cast<std::streamsize>(size);
    }

    /// Refuses bytes that are no binary document: quietly as a file of an older format, or as a broken document.
    [[noreturn]] static void refuse(const uint8_t* data, size_t size)
    {
        if(starts_as_document(data, size))
        {
            detail::fail("The binary document's header is broken.");
        }
        throw not_a_document();
    }

    void open(const uint8_t* data, size_t size)
    {
        if(!is_document(data, size))
        {
            refuse(data, size);
        }
        const auto dictionary = check_header(data, size);
        read_dictionary(data + dictionary, data + size, data);
        open_root(data, dictionary, dictionary);
    }

    /// The dictionary's offset that the header at header names, checked against the document's size.
    static auto check_header(const uint8_t* header, size_t size) -> size_t
    {
        const auto version = detail::read_raw<uint32_t>(header + preamble_size + 4);
        if(version != document_version)
        {
            detail::fail("The binary document was written in a newer format version.");
        }
        const auto dictionary = detail::read_raw<uint32_t>(header + preamble_size + 8);
        const auto key_count = detail::read_raw<uint32_t>(header + preamble_size + 12);
        if(dictionary > size || dictionary < header_size + 1 + node_header_size ||
           key_count > (size - dictionary) / 10)
        {
            detail::fail("The binary document's header is broken.");
        }
        return dictionary;
    }

    /// Reads the dictionary at [p, end), of the key count the header at header names.
    void read_dictionary(const uint8_t* p, const uint8_t* end, const uint8_t* header)
    {
        const auto key_count = detail::read_raw<uint32_t>(header + preamble_size + 12);
        hashes_.reserve(key_count);
        names_.reserve(key_count);
        for(uint32_t index = 0; index < key_count; ++index)
        {
            if(end - p < 8)
            {
                detail::fail("The binary document's dictionary of names is broken.");
            }
            hashes_.push_back(detail::read_raw<uint64_t>(p));
            p += 8;
            const auto text_size = detail::read_varint(p, end);
            if(static_cast<size_t>(end - p) < size_t(text_size) + 1 || p[text_size] != 0)
            {
                detail::fail("The binary document's dictionary of names is broken.");
            }
            names_.push_back(reinterpret_cast<const char*>(p));
            p += text_size + 1;
        }

        index_.reserve(key_count);
        for(uint32_t index = 0; index < key_count; ++index)
        {
            index_.insert(hashes_[index], index);
        }
    }

    /**
     * @brief Opens the root object of the document whose first `available` bytes are at data; its values end at the
     *        dictionary's offset. When fewer bytes than that are there, every node ends where they do.
     */
    void open_root(const uint8_t* data, size_t available, size_t dictionary)
    {
        if(available < header_size + 1 + node_header_size)
        {
            detail::fail("The binary document's header is broken.");
        }
        available_end_ = available < dictionary ? data + available : nullptr;
        const uint8_t* root = data + header_size;
        if(static_cast<value_type>(*root) != value_type::object)
        {
            detail::fail("The binary document's root is not an object.");
        }
        const auto body = detail::read_raw<uint32_t>(root + 1);
        if(size_t(body) > dictionary - (header_size + 1 + node_header_size))
        {
            detail::fail("A node in the binary document runs past the node that holds it.");
        }
        push(root + 1, false);
    }

    void push(const uint8_t* header, bool is_array)
    {
        frame next;
        const auto body = detail::read_raw<uint32_t>(header);
        next.begin = header + node_header_size;
        // Only part of the document was read: the node ends where the bytes read do.
        const auto readable = available_end_ != nullptr ? static_cast<size_t>(available_end_ - next.begin) : size_t(body);
        next.end = next.begin + std::min<size_t>(body, readable);
        next.cursor = next.begin;
        next.count = detail::read_raw<uint32_t>(header + 4);
        next.is_array = is_array;
        // Every element or member takes at least one byte, so a larger count is a broken document,
        // and a load that sized a container by it would allocate what the corruption says.
        if(next.count > body)
        {
            detail::fail("A node in the binary document counts more values than it holds.");
        }
        frames_.push_back(next);
    }

    auto read_key(const uint8_t*& p, const uint8_t* limit) const -> uint32_t
    {
        const auto key = detail::read_varint(p, limit);
        if(key >= hashes_.size())
        {
            detail::fail("A member of the binary document names a key its dictionary does not hold.");
        }
        return key;
    }

    /// The entry at p, checked against the end of the node holding it.
    auto read_entry(const uint8_t* p, const uint8_t* limit, bool keyed) const -> entry
    {
        entry value;
        if(keyed)
        {
            value.key = read_key(p, limit);
        }
        if(p >= limit)
        {
            detail::fail("A value in the binary document runs past the node that holds it.");
        }
        const uint8_t type = *p++;
        if(type > last_value_type)
        {
            detail::fail("A value in the binary document has an unknown type.");
        }
        value.type = static_cast<value_type>(type);
        value.payload = p;
        const int8_t fixed = detail::fixed_payload_size[type];
        if(fixed >= 0)
        {
            if(limit - p < fixed)
            {
                detail::fail("A value in the binary document runs past the node that holds it.");
            }
            value.end = p + fixed;
        }
        else if(fixed == -1)
        {
            const auto size = detail::read_varint(p, limit);
            if(static_cast<size_t>(limit - p) < size)
            {
                detail::fail("A value in the binary document runs past the node that holds it.");
            }
            value.end = p + size;
        }
        else if(fixed == -3)
        {
            if(p >= limit || !detail::is_number_type(*p))
            {
                detail::fail("A list of numbers in the binary document names no number type.");
            }
            const auto element = detail::fixed_payload_size[*p++];
            const auto count = detail::read_varint(p, limit);
            if(static_cast<size_t>(limit - p) / static_cast<size_t>(element) < count)
            {
                detail::fail("A value in the binary document runs past the node that holds it.");
            }
            value.end = p + size_t(count) * static_cast<size_t>(element);
        }
        else
        {
            if(limit - p < static_cast<ptrdiff_t>(node_header_size))
            {
                detail::fail("A node in the binary document runs past the node that holds it.");
            }
            const auto body = detail::read_raw<uint32_t>(p);
            if(static_cast<size_t>(limit - (p + node_header_size)) >= body)
            {
                value.end = p + node_header_size + body;
            }
            else if(limit == available_end_)
            {
                // Cut short by the end of the bytes read, not broken: it ends there too.
                value.end = limit;
            }
            else
            {
                detail::fail("A node in the binary document runs past the node that holds it.");
            }
        }
        return value;
    }

    auto find_key(uint64_t hash) const noexcept -> uint32_t
    {
        return index_.find(hash);
    }

    auto scan(const uint8_t* p, const uint8_t* limit, uint32_t key) const -> const uint8_t*
    {
        while(p < limit)
        {
            const auto value = read_entry(p, limit, true);
            if(value.key == key)
            {
                return p;
            }
            p = value.end;
        }
        return nullptr;
    }

    /**
     * @brief Where the member with this name hash is in the current object, or null.
     *
     * In order is one compare: the member at the read position. Otherwise the object is searched from
     * its first member, as the JSON archive searches, so of members that share a name the same one is
     * found in both formats. A name the document never uses is answered by the dictionary without a
     * search.
     */
    auto locate(uint64_t hash) const -> const uint8_t*
    {
        const auto& current = frames_.back();
        if(found_from_ != nullptr && found_from_ == current.cursor && found_hash_ == hash)
        {
            return found_at_;
        }
        if(current.cursor < current.end)
        {
            const uint8_t* p = current.cursor;
            if(hashes_[read_key(p, current.end)] == hash)
            {
                return current.cursor;
            }
        }
        const auto key = find_key(hash);
        if(key == no_key)
        {
            return nullptr;
        }
        return scan(current.begin, current.end, key);
    }

    /// The member last found for a hash, asked for with the read position at from.
    void remember(uint64_t hash, const uint8_t* from, const uint8_t* at) noexcept
    {
        found_hash_ = hash;
        found_from_ = from;
        found_at_ = at;
    }

    std::vector<uint8_t> owned_;
    /// The dictionary's bytes when only the start of a document was read; names_ point into them.
    std::vector<uint8_t> dictionary_;
    /// Where the bytes read end when only the start of a document was read; null when all of it was.
    const uint8_t* available_end_ = nullptr;
    std::vector<uint64_t> hashes_;
    std::vector<const char*> names_;
    name_index index_;
    std::vector<frame> frames_;
    uint64_t found_hash_ = 0;
    const uint8_t* found_from_ = nullptr;
    const uint8_t* found_at_ = nullptr;
};

/**
 * @brief Visits every value inside the current node, depth first in document order, for code that
 *        looks through a document without loading it (a dependency scan).
 *
 * fn(name, value, depth) gets the member's name (null inside an array), the value, and how many nodes
 * below the walked one it sits (0 for the walked node's own values); a value of an object or array is
 * visited before what it holds. A false return from fn ends the walk, and walk_values returns false.
 */
template<typename F>
auto walk_values(reader& document, F&& fn, size_t depth = 0) -> bool
{
    while(!document.at_end())
    {
        const auto key = document.key_at_cursor();
        const auto value = document.peek();
        const char* name = key == no_key ? nullptr : document.name_of(key);
        if(!fn(name, value, depth))
        {
            return false;
        }
        if(value.type == value_type::object || value.type == value_type::array)
        {
            document.enter();
            const bool walking = walk_values(document, fn, depth + 1);
            document.leave();
            if(!walking)
            {
                return false;
            }
        }
        else
        {
            document.take();
        }
    }
    return true;
}
} // namespace serialization::tagged

namespace ser20
{
/**
 * @brief Writes a tagged binary document: the JSON archive's structure, in binary.
 *
 * The document goes to the stream, or into the vector, when the archive is destroyed.
 */
class TaggedBinaryOutputArchive : public OutputArchive<TaggedBinaryOutputArchive>
{
public:
    /// Vectors, quaternions and colors are one value each (serialization::serialize_components).
    static constexpr bool stores_packed_values = true;

    explicit TaggedBinaryOutputArchive(std::ostream& stream)
        : OutputArchive<TaggedBinaryOutputArchive>(this)
        , stream_(&stream)
    {
    }

    explicit TaggedBinaryOutputArchive(std::vector<uint8_t>& out)
        : OutputArchive<TaggedBinaryOutputArchive>(this)
        , out_(&out)
    {
    }

    /// Writes the finished document, unless an exception thrown during the save is unwinding past the archive: a
    /// document cut short there would read as whole, so nothing is written and the stream reports the failure.
    ~TaggedBinaryOutputArchive() noexcept
    {
        if(std::uncaught_exceptions() > uncaught_exceptions_)
        {
            if(stream_ != nullptr)
            {
                stream_->setstate(std::ios::badbit);
            }
            return;
        }
        try
        {
            if(stream_ != nullptr)
            {
                const auto& bytes = writer_.finish();
                const auto size = static_cast<std::streamsize>(bytes.size());
                if(stream_->rdbuf()->sputn(reinterpret_cast<const char*>(bytes.data()), size) != size)
                {
                    stream_->setstate(std::ios::badbit);
                }
            }
            else if(out_ != nullptr)
            {
                *out_ = writer_.take();
            }
        }
        catch(...)
        {
            // A document that could not be finished (a size past 32 bits) is not left looking written.
            if(stream_ != nullptr)
            {
                stream_->setstate(std::ios::badbit);
            }
        }
    }

    TaggedBinaryOutputArchive(const TaggedBinaryOutputArchive&) = delete;
    auto operator=(const TaggedBinaryOutputArchive&) -> TaggedBinaryOutputArchive& = delete;

    void setNextName(const char* name)
    {
        writer_.key(name);
    }

    void setNextKey(const char* name, std::uint64_t hash)
    {
        writer_.key(name, hash_of_name(name, hash));
    }

    void startNode()
    {
        writer_.begin_object();
    }

    void finishNode()
    {
        writer_.end_node();
    }

    void makeArray()
    {
        writer_.make_array();
    }

    auto getNodeDepth() const -> std::size_t
    {
        return writer_.node_depth();
    }

    /// Closes the nodes a failed save opened and left open.
    void restoreNodeDepth(std::size_t depth)
    {
        writer_.restore_node_depth(depth);
    }

    /// Whether values are written into a packed list's rows, where one that fails leaves the rest out of step.
    auto isInRows() const -> bool
    {
        return writer_.writes_rows();
    }

    void saveSize(size_type size)
    {
        writer_.set_count(size);
    }

    void saveValue(bool value)
    {
        writer_.value_bool(value);
    }

    template<class T>
    void saveValue(T value)
        requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
    {
        writer_.value_number(value);
    }

    void saveValue(const std::string& value)
    {
        writer_.value_string(value.data(), value.size());
    }

    void saveValue(const char* value)
    {
        writer_.value_string(value, std::strlen(value));
    }

    void saveValue(std::nullptr_t)
    {
        writer_.value_null();
    }

    void saveBinaryValue(const void* data, size_t size)
    {
        writer_.value_blob(data, size);
    }

    /// A value made of count numbers, under the name set before it.
    template<class T>
    void savePackedValue(const T* data, size_t count)
    {
        writer_.value_numbers(data, count);
    }

    /**
     * @brief Saves count elements as a packed list: write_elements saves each of them, and their values
     *        go into rows (see the file's notes).
     */
    template<class F>
    void savePacked(size_t count, F&& write_elements)
    {
        writer_.begin_packed(count);
        write_elements();
        writer_.end_packed();
    }

private:
    serialization::tagged::writer writer_;
    std::ostream* stream_ = nullptr;
    std::vector<uint8_t>* out_ = nullptr;
    /// Exceptions in flight when the archive was made: more at its end means one is unwinding past it.
    int uncaught_exceptions_ = std::uncaught_exceptions();
};

/**
 * @brief Reads a tagged binary document with the JSON archive's rules: members by name, absent ones
 *        reported without a throw, any value skippable.
 */
class TaggedBinaryInputArchive : public InputArchive<TaggedBinaryInputArchive>
{
public:
    /// Vectors, quaternions and colors are one value each (serialization::serialize_components).
    static constexpr bool stores_packed_values = true;

    TaggedBinaryInputArchive(const uint8_t* data, size_t size)
        : InputArchive<TaggedBinaryInputArchive>(this)
        , reader_(data, size)
    {
    }

    TaggedBinaryInputArchive(const char* data, size_t size)
        : InputArchive<TaggedBinaryInputArchive>(this)
        , reader_(reinterpret_cast<const uint8_t*>(data), size)
    {
    }

    explicit TaggedBinaryInputArchive(std::istream& stream)
        : InputArchive<TaggedBinaryInputArchive>(this)
        , reader_(stream)
    {
    }

    /// Reads only the start of the document in a seekable stream: at most head_size bytes of its values
    /// (serialization::tagged::reader).
    TaggedBinaryInputArchive(std::istream& stream, size_t head_size)
        : InputArchive<TaggedBinaryInputArchive>(this)
        , reader_(stream, head_size)
    {
    }

    TaggedBinaryInputArchive(const TaggedBinaryInputArchive&) = delete;
    auto operator=(const TaggedBinaryInputArchive&) -> TaggedBinaryInputArchive& = delete;

    void setNextName(const char* name)
    {
        next_name_ = name;
        next_hash_ = name != nullptr ? name_hash(name) : 0;
    }

    void setNextKey(const char* name, std::uint64_t hash)
    {
        next_name_ = name;
        next_hash_ = hash_of_name(name, hash);
    }

    void startNode()
    {
        if(row_value())
        {
            reader_.row_enter();
            return;
        }
        search();
        reader_.enter();
    }

    void finishNode()
    {
        if(reader_.in_rows())
        {
            reader_.row_leave();
            return;
        }
        reader_.leave();
    }

    /// Where the read position is in the document: changes when a value is read or stepped over.
    auto getReadPosition() const -> const void*
    {
        return reader_.top().cursor;
    }

    /// The name of the member at the read position, or null at the end of an object or in an array.
    auto getNodeName() const -> const char*
    {
        const auto key = reader_.key_at_cursor();
        return key == serialization::tagged::no_key ? nullptr : reader_.name_of(key);
    }

    auto getNodeDepth() const -> std::size_t
    {
        return reader_.node_depth();
    }

    /// Closes the nodes a failed load opened and left open.
    void restoreNodeDepth(std::size_t depth)
    {
        reader_.restore_node_depth(depth);
    }

    /// The bytes left in the node or rows being read: what bounds the elements a count there can name.
    auto remainingBytes() const -> std::size_t
    {
        const auto& current = reader_.top();
        return static_cast<std::size_t>(current.end - current.cursor);
    }

    /// Whether values are read from a packed list's rows, where one that fails leaves the rest out of step.
    auto isInRows() const -> bool
    {
        return reader_.in_rows();
    }

    /**
     * @brief Whether a value that will not load is logged (try_load).
     *
     * On for a compiled file read through serialization::read_binary: only the engine writes those, from the
     * types that read them, so a value that will not load means the data and the code disagree. Off for a
     * document converted from JSON and for copies made inside the process, read as quietly as JSON.
     */
    auto reportsFailedLoads() const -> bool
    {
        return reports_failed_loads_;
    }

    void reportFailedLoads(bool report)
    {
        reports_failed_loads_ = report;
    }

    /**
     * @brief Calls fn with the name hash of each member of the current object, in document order,
     *        without moving the read position. Nothing inside an array.
     *
     * For a loader that would otherwise ask for many names one by one: it reads which ones the object
     * has in one pass.
     */
    template<class F>
    void forEachMemberKey(F&& fn) const
    {
        reader_.for_each_member_hash(std::forward<F>(fn));
    }

    /// Whether the current object has a member with this name. True inside an array, where
    /// members are not named. The read position stays where it is (reader::contains).
    auto hasNextName(const char* name) const -> bool
    {
        return name == nullptr || hasNextKey(name, name_hash(name));
    }

    auto hasNextKey(const char* name, std::uint64_t hash) const -> bool
    {
        if(name == nullptr || reader_.top().is_array)
        {
            return true;
        }
        return reader_.contains(hash_of_name(name, hash));
    }

    void loadSize(size_type& size)
    {
        if(reader_.in_rows())
        {
            size = reader_.row_count();
            return;
        }
        size = reader_.top().count;
    }

    void loadValue(bool& value)
    {
        if(row_value())
        {
            value = *reader_.row_bytes(1) != 0;
            return;
        }
        search();
        const auto entry = reader_.take();
        if(entry.type != serialization::tagged::value_type::true_value &&
           entry.type != serialization::tagged::value_type::false_value)
        {
            // As the JSON archive: a number is not read as a bool.
            serialization::tagged::detail::fail("A value read as a bool in the binary document is not one.");
        }
        value = entry.type == serialization::tagged::value_type::true_value;
    }

    template<class T>
    void loadValue(T& value)
        requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
    {
        if(row_value())
        {
            std::memcpy(&value, reader_.row_bytes(sizeof(T)), sizeof(T));
            return;
        }
        search();
        const auto entry = reader_.take();
        if(entry.type == serialization::tagged::detail::value_type_of<T>())
        {
            std::memcpy(&value, entry.payload, sizeof(T));
            return;
        }
        value = serialization::tagged::reader::to_number<T>(entry);
    }

    void loadValue(std::string& value)
    {
        if(row_value())
        {
            const auto size = reader_.row_count();
            value.assign(reinterpret_cast<const char*>(reader_.row_bytes(size)), size);
            return;
        }
        search();
        const auto bytes = serialization::tagged::reader::bytes_of(reader_.take());
        value.assign(bytes.data(), bytes.size());
    }

    void loadValue(std::nullptr_t&)
    {
        if(row_value())
        {
            return;
        }
        search();
        reader_.take();
    }

    /// Raw bytes saved as one blob.
    void loadBinaryValue(void* data, size_t size)
    {
        if(row_value())
        {
            if(size != 0)
            {
                std::memcpy(data, reader_.row_bytes(size), size);
            }
            return;
        }
        search();
        const auto bytes = serialization::tagged::reader::bytes_of(reader_.take());
        if(bytes.size() != size)
        {
            serialization::tagged::detail::fail("A block of bytes in the binary document does not have the size it is read with.");
        }
        if(size != 0)
        {
            std::memcpy(data, bytes.data(), size);
        }
    }

    /// Numbers saved as one blob, or as one number each, as a document converted from JSON holds them.
    template<class T>
    void loadNumbers(T* data, size_t count)
    {
        if(row_value())
        {
            loadBinaryValue(data, count * sizeof(T));
            return;
        }
        search();
        if(reader_.peek().type == serialization::tagged::value_type::blob)
        {
            loadBinaryValue(data, count * sizeof(T));
            return;
        }
        for(size_t index = 0; index < count; ++index)
        {
            loadValue(data[index]);
        }
    }

    /// Whether the value named before it is a value of numbers rather than an object of named ones (a
    /// document converted from JSON). Inside a row it always is.
    auto isPackedValueNext() -> bool
    {
        if(reader_.in_rows())
        {
            return true;
        }
        search();
        return !reader_.at_end() && reader_.peek().type == serialization::tagged::value_type::numbers;
    }

    /// The count numbers of the value named before it, converted to T from the type they were saved as.
    template<class T>
    void loadPackedValue(T* data, size_t count)
    {
        if(row_value())
        {
            std::memcpy(data, reader_.row_bytes(count * sizeof(T)), count * sizeof(T));
            return;
        }
        search();
        const auto values = serialization::tagged::reader::numbers_of(reader_.take());
        if(values.count != count)
        {
            serialization::tagged::detail::fail("A value in the binary document holds another number of components than the one it is read into.");
        }
        if(values.element == serialization::tagged::detail::value_type_of<T>())
        {
            std::memcpy(data, values.data, count * sizeof(T));
            return;
        }
        const auto element_size = static_cast<size_t>(
            serialization::tagged::detail::fixed_payload_size[static_cast<uint8_t>(values.element)]);
        for(size_t index = 0; index < count; ++index)
        {
            serialization::tagged::reader::entry number;
            number.type = values.element;
            number.payload = values.data + index * element_size;
            data[index] = serialization::tagged::reader::to_number<T>(number);
        }
    }

    /// Whether the value at the read position is a blob. In a row, numbers saved together always are.
    auto isBlobNext() const -> bool
    {
        if(reader_.in_rows())
        {
            return true;
        }
        return !reader_.at_end() && reader_.peek().type == serialization::tagged::value_type::blob;
    }

    /// Steps over the value at the read position without reading it.
    void skipValue()
    {
        if(row_value())
        {
            serialization::tagged::detail::fail("A value inside a packed list cannot be stepped over.");
        }
        search();
        reader_.take();
    }

    /**
     * @brief Loads the list savePacked saved: read_elements(count) loads each element, which reads its
     *        values from the rows. An array in its place (a document converted from JSON) is read the
     *        same way, element by element.
     *
     * The rows must be read to their end: a load that reads less or more than was saved fails, so a
     * list whose saving code changed is refused rather than read askew. A value in the rows that fails
     * fails the whole list too (try_load passes it on, serialization::can_hold_rows).
     */
    template<class F>
    void loadPacked(F&& read_elements)
    {
        if(row_value())
        {
            const auto count = reader_.row_count();
            reader_.row_enter();
            read_elements(static_cast<size_t>(count));
            reader_.row_leave();
            return;
        }
        search();
        reader_.enter_list();
        read_elements(static_cast<size_t>(reader_.top().count));
        if(reader_.in_rows() && !reader_.at_end())
        {
            serialization::tagged::detail::fail("A packed list in the binary document holds more than its elements read, so they read less than was saved.");
        }
        reader_.leave();
    }

private:
    /// Whether the value asked for is in the rows being read, which hold no names: the name set for it is
    /// dropped there, so it cannot be looked for after the list.
    auto row_value() -> bool
    {
        if(!reader_.in_rows())
        {
            return false;
        }
        next_name_ = nullptr;
        return true;
    }

    void search()
    {
        const char* name = next_name_;
        next_name_ = nullptr;
        if(name == nullptr || reader_.top().is_array)
        {
            return;
        }
        if(!reader_.seek(next_hash_))
        {
            throw Exception(std::string("The binary document has no member named ") + name + ".");
        }
    }

    mutable serialization::tagged::reader reader_;
    const char* next_name_ = nullptr;
    std::uint64_t next_hash_ = 0;
    bool reports_failed_loads_ = false;
};

// ######################################################################
// Node rules: the JSON archive's. Names and size tags set up the next node, values of class type
// are nodes, arithmetic values and strings are values.

template<class T>
inline void prologue(TaggedBinaryOutputArchive&, NameValuePair<T> const&)
{
}
template<class T>
inline void prologue(TaggedBinaryInputArchive&, NameValuePair<T> const&)
{
}
template<class T>
inline void epilogue(TaggedBinaryOutputArchive&, NameValuePair<T> const&)
{
}
template<class T>
inline void epilogue(TaggedBinaryInputArchive&, NameValuePair<T> const&)
{
}

template<class T>
inline void prologue(TaggedBinaryOutputArchive&, DeferredData<T> const&)
{
}
template<class T>
inline void prologue(TaggedBinaryInputArchive&, DeferredData<T> const&)
{
}
template<class T>
inline void epilogue(TaggedBinaryOutputArchive&, DeferredData<T> const&)
{
}
template<class T>
inline void epilogue(TaggedBinaryInputArchive&, DeferredData<T> const&)
{
}

template<class T>
inline void prologue(TaggedBinaryOutputArchive& ar, SizeTag<T> const&)
{
    ar.makeArray();
}
template<class T>
inline void prologue(TaggedBinaryInputArchive&, SizeTag<T> const&)
{
}
template<class T>
inline void epilogue(TaggedBinaryOutputArchive&, SizeTag<T> const&)
{
}
template<class T>
inline void epilogue(TaggedBinaryInputArchive&, SizeTag<T> const&)
{
}

template<class T>
inline void prologue(TaggedBinaryOutputArchive&, BinaryData<T> const&)
{
}
template<class T>
inline void prologue(TaggedBinaryInputArchive&, BinaryData<T> const&)
{
}
template<class T>
inline void epilogue(TaggedBinaryOutputArchive&, BinaryData<T> const&)
{
}
template<class T>
inline void epilogue(TaggedBinaryInputArchive&, BinaryData<T> const&)
{
}

template<class T>
inline void prologue(TaggedBinaryOutputArchive& ar, T const&)
    requires(!std::is_arithmetic_v<T> &&
             !traits::has_minimal_base_class_serialization<T,
                                                           traits::has_minimal_output_serialization,
                                                           TaggedBinaryOutputArchive>::value &&
             !traits::has_minimal_output_serialization<T, TaggedBinaryOutputArchive>::value &&
             !is_packed_value_v<T>)
{
    ar.startNode();
}
template<class T>
inline void prologue(TaggedBinaryInputArchive& ar, T const&)
    requires(!std::is_arithmetic_v<T> &&
             !traits::has_minimal_base_class_serialization<T,
                                                           traits::has_minimal_input_serialization,
                                                           TaggedBinaryInputArchive>::value &&
             !traits::has_minimal_input_serialization<T, TaggedBinaryInputArchive>::value &&
             !is_packed_value_v<T>)
{
    ar.startNode();
}
template<class T>
inline void epilogue(TaggedBinaryOutputArchive& ar, T const&)
    requires(!std::is_arithmetic_v<T> &&
             !traits::has_minimal_base_class_serialization<T,
                                                           traits::has_minimal_output_serialization,
                                                           TaggedBinaryOutputArchive>::value &&
             !traits::has_minimal_output_serialization<T, TaggedBinaryOutputArchive>::value &&
             !is_packed_value_v<T>)
{
    ar.finishNode();
}
template<class T>
inline void epilogue(TaggedBinaryInputArchive& ar, T const&)
    requires(!std::is_arithmetic_v<T> &&
             !traits::has_minimal_base_class_serialization<T,
                                                           traits::has_minimal_input_serialization,
                                                           TaggedBinaryInputArchive>::value &&
             !traits::has_minimal_input_serialization<T, TaggedBinaryInputArchive>::value &&
             !is_packed_value_v<T>)
{
    ar.finishNode();
}

inline void prologue(TaggedBinaryOutputArchive&, std::nullptr_t const&)
{
}

// A packed value is one value: its serialization writes it under the name before it, with no node around it.
template<class T>
inline void prologue(TaggedBinaryOutputArchive&, T const&)
    requires(is_packed_value_v<T>)
{
}
template<class T>
inline void prologue(TaggedBinaryInputArchive&, T const&)
    requires(is_packed_value_v<T>)
{
}
template<class T>
inline void epilogue(TaggedBinaryOutputArchive&, T const&)
    requires(is_packed_value_v<T>)
{
}
template<class T>
inline void epilogue(TaggedBinaryInputArchive&, T const&)
    requires(is_packed_value_v<T>)
{
}
inline void prologue(TaggedBinaryInputArchive&, std::nullptr_t const&)
{
}
inline void epilogue(TaggedBinaryOutputArchive&, std::nullptr_t const&)
{
}
inline void epilogue(TaggedBinaryInputArchive&, std::nullptr_t const&)
{
}

template<class T>
inline void prologue(TaggedBinaryOutputArchive&, T const&)
    requires(std::is_arithmetic_v<T>)
{
}
template<class T>
inline void prologue(TaggedBinaryInputArchive&, T const&)
    requires(std::is_arithmetic_v<T>)
{
}
template<class T>
inline void epilogue(TaggedBinaryOutputArchive&, T const&)
    requires(std::is_arithmetic_v<T>)
{
}
template<class T>
inline void epilogue(TaggedBinaryInputArchive&, T const&)
    requires(std::is_arithmetic_v<T>)
{
}

template<class CharT, class Traits, class Alloc>
inline void prologue(TaggedBinaryOutputArchive&, std::basic_string<CharT, Traits, Alloc> const&)
{
}
template<class CharT, class Traits, class Alloc>
inline void prologue(TaggedBinaryInputArchive&, std::basic_string<CharT, Traits, Alloc> const&)
{
}
template<class CharT, class Traits, class Alloc>
inline void epilogue(TaggedBinaryOutputArchive&, std::basic_string<CharT, Traits, Alloc> const&)
{
}
template<class CharT, class Traits, class Alloc>
inline void epilogue(TaggedBinaryInputArchive&, std::basic_string<CharT, Traits, Alloc> const&)
{
}

// ######################################################################
// Save and load

template<class T>
inline void SER20_SAVE_FUNCTION_NAME(TaggedBinaryOutputArchive& ar, NameValuePair<T> const& pair)
{
    ar.setNextKey(pair.name, pair.hash);
    ar(pair.value);
}
template<class T>
inline void SER20_LOAD_FUNCTION_NAME(TaggedBinaryInputArchive& ar, NameValuePair<T>& pair)
{
    ar.setNextKey(pair.name, pair.hash);
    ar(pair.value);
}

inline void SER20_SAVE_FUNCTION_NAME(TaggedBinaryOutputArchive& ar, std::nullptr_t const& value)
{
    ar.saveValue(value);
}
inline void SER20_LOAD_FUNCTION_NAME(TaggedBinaryInputArchive& ar, std::nullptr_t& value)
{
    ar.loadValue(value);
}

template<class T>
inline void SER20_SAVE_FUNCTION_NAME(TaggedBinaryOutputArchive& ar, T const& value)
    requires(std::is_arithmetic_v<T>)
{
    ar.saveValue(value);
}
template<class T>
inline void SER20_LOAD_FUNCTION_NAME(TaggedBinaryInputArchive& ar, T& value)
    requires(std::is_arithmetic_v<T>)
{
    ar.loadValue(value);
}

template<class CharT, class Traits, class Alloc>
inline void SER20_SAVE_FUNCTION_NAME(TaggedBinaryOutputArchive& ar, std::basic_string<CharT, Traits, Alloc> const& value)
{
    ar.saveValue(value);
}
template<class CharT, class Traits, class Alloc>
inline void SER20_LOAD_FUNCTION_NAME(TaggedBinaryInputArchive& ar, std::basic_string<CharT, Traits, Alloc>& value)
{
    ar.loadValue(value);
}

template<class T>
inline void SER20_SAVE_FUNCTION_NAME(TaggedBinaryOutputArchive& ar, SizeTag<T> const& tag)
{
    ar.saveSize(tag.size);
}
template<class T>
inline void SER20_LOAD_FUNCTION_NAME(TaggedBinaryInputArchive& ar, SizeTag<T>& tag)
{
    ar.loadSize(tag.size);
}

template<class T>
inline void SER20_SAVE_FUNCTION_NAME(TaggedBinaryOutputArchive& ar, BinaryData<T> const& data)
{
    ar.saveBinaryValue(data.data, static_cast<size_t>(data.size));
}
template<class T>
inline void SER20_LOAD_FUNCTION_NAME(TaggedBinaryInputArchive& ar, BinaryData<T>& data)
{
    using element = std::remove_cv_t<std::remove_pointer_t<std::decay_t<T>>>;
    if constexpr(std::is_arithmetic_v<element> && !std::is_same_v<element, bool>)
    {
        ar.loadNumbers(static_cast<element*>(data.data), static_cast<size_t>(data.size) / sizeof(element));
    }
    else
    {
        ar.loadBinaryValue(data.data, static_cast<size_t>(data.size));
    }
}
} // namespace ser20

SER20_REGISTER_ARCHIVE(ser20::TaggedBinaryOutputArchive)
SER20_REGISTER_ARCHIVE(ser20::TaggedBinaryInputArchive)
SER20_SETUP_ARCHIVE_TRAITS(ser20::TaggedBinaryInputArchive, ser20::TaggedBinaryOutputArchive)
