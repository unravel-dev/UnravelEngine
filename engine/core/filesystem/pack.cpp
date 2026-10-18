#include "pack.h"
#include "file_istream.h"
#include "pack_image.h"

#include <logging/logging.h>

#include <algorithm>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <utility>

namespace fs::pack_detail
{
namespace
{
/// A pack mounted for a protocol.
struct pack_mount
{
    std::string protocol;
    std::shared_ptr<const pack_image> image;
};

/// The mounted packs, the latest last.
std::shared_mutex mounts_mutex;
std::vector<pack_mount> mounts;

struct entry_lookup
{
    std::shared_ptr<const pack_image> image;
    const pack_record* record{};
};

/// The protocol and entry name `file` maps to; false when it is under no protocol. A path spelled from
/// a protocol root matches without touching the disk; any other spelling is made canonical first.
auto split_into_protocol_and_name(const path& file, std::string& protocol, std::string& name) -> bool
{
    path key = has_known_protocol(file) ? file : convert_to_protocol_lexically(file);
    if(!has_known_protocol(key))
    {
        key = convert_to_protocol(file);
    }
    const auto text = key.generic_string();
    const auto separator = text.find(":/");
    if(separator == std::string::npos || separator == 0)
    {
        return false;
    }
    protocol = text.substr(0, separator);
    name = text.substr(separator + 2);
    return !name.empty();
}

auto has_mounts() -> bool
{
    std::shared_lock lock(mounts_mutex);
    return !mounts.empty();
}

auto find_entry(const path& file) -> entry_lookup
{
    std::string protocol;
    std::string name;
    if(!has_mounts() || !split_into_protocol_and_name(file, protocol, name))
    {
        return {};
    }
    const auto key = make_path_key(name);
    std::shared_lock lock(mounts_mutex);
    for(auto it = mounts.rbegin(); it != mounts.rend(); ++it)
    {
        if(it->protocol != protocol)
        {
            continue;
        }
        const auto found = it->image->entries.find(key);
        if(found != it->image->entries.end())
        {
            return {it->image, &found->second};
        }
    }
    return {};
}

/// The protocol's topmost mount; the caller holds mounts_mutex.
auto find_top_mount_unlocked(const std::string& protocol) -> const pack_mount*
{
    const auto found = std::find_if(mounts.rbegin(),
                                    mounts.rend(),
                                    [&protocol](const pack_mount& mount)
                                    {
                                        return mount.protocol == protocol;
                                    });
    return found == mounts.rend() ? nullptr : &*found;
}

/// The bytes of a found entry: a view of the mapping, or the entry decoded.
auto read_found_entry(const entry_lookup& found, const path& file) -> packed_lookup
{
    const auto& record = *found.record;
    if(record.is_deleted())
    {
        return {packed_status::deleted, {}};
    }
    if(record.codec == PACK_STORED_CODEC)
    {
        // The aliasing pointer keeps the pack mapped while the view is used.
        const auto* data = found.image->file.data() + record.offset;
        return {packed_status::found, {data, std::size_t(record.raw_size), std::shared_ptr<const void>(found.image, data)}};
    }
    try
    {
        auto decoded = decode_entry(*found.image, record);
        if(decoded)
        {
            return {packed_status::found, {decoded->data(), decoded->size(), decoded}};
        }
    }
    catch(const std::bad_alloc&)
    {
        // A damaged size: reported below like any other entry that does not decode.
    }
    APPLOG_ERROR("{} does not decode from its pack (the pack is damaged); deploy again", file.generic_string());
    return {packed_status::damaged, {}};
}

/// Where `file` is on disk: a protocol path resolved, any other path as it is.
auto get_disk_path(const path& file) -> path
{
    return has_known_protocol(file) ? resolve_protocol(file) : file;
}

/// A read-only stream over bytes it does not own, seekable both ways.
class view_streambuf : public std::streambuf
{
public:
    view_streambuf(const std::uint8_t* data, std::size_t size)
    {
        auto* begin = reinterpret_cast<char*>(const_cast<std::uint8_t*>(data));
        setg(begin, begin, begin + size);
    }

protected:
    auto seekoff(std::streamoff offset, std::ios_base::seekdir direction, std::ios_base::openmode which)
        -> std::streampos override
    {
        if((which & std::ios_base::in) == 0)
        {
            return std::streampos(std::streamoff(-1));
        }
        std::streamoff base = 0;
        if(direction == std::ios_base::cur)
        {
            base = gptr() - eback();
        }
        else if(direction == std::ios_base::end)
        {
            base = egptr() - eback();
        }
        const std::streamoff target = base + offset;
        if(target < 0 || target > egptr() - eback())
        {
            return std::streampos(std::streamoff(-1));
        }
        setg(eback(), eback() + target, egptr());
        return std::streampos(target);
    }

    auto seekpos(std::streampos position, std::ios_base::openmode which) -> std::streampos override
    {
        return seekoff(std::streamoff(position), std::ios_base::beg, which);
    }
};

/// A stream over a packed file that keeps the file's storage alive.
class packed_istream : public std::istream
{
public:
    explicit packed_istream(packed_file file)
        : std::istream(nullptr)
        , file_(std::move(file))
        , buffer_(file_.data, file_.size)
    {
        rdbuf(&buffer_);
    }

private:
    packed_file file_;
    view_streambuf buffer_;
};
} // namespace
} // namespace fs::pack_detail

namespace fs
{
namespace
{
/// Puts an opened pack on top of the protocol's mounts; `label` names it in errors.
auto mount_image(const std::string& protocol,
                 std::shared_ptr<pack_detail::pack_image> image,
                 const std::string& label,
                 std::string& err) -> bool
{
    std::unique_lock lock(pack_detail::mounts_mutex);
    // Over any other pack its additions and deletions would mix two builds.
    const auto* top = pack_detail::find_top_mount_unlocked(protocol);
    if(image->is_patch() && (top == nullptr || top->image->toc_hash != image->base_hash))
    {
        err = label + ": a patch for another build (it mounts only right over the pack it was made against)";
        return false;
    }
    pack_detail::mounts.push_back({protocol, std::move(image)});
    return true;
}
} // namespace

auto mount_pack(const std::string& protocol, const path& pack_file, std::string& err) -> bool
{
    auto image = std::make_shared<pack_detail::pack_image>();
    if(!pack_detail::open_pack_image(pack_file, *image, err))
    {
        err = pack_file.generic_string() + ": " + err;
        return false;
    }
    return mount_image(protocol, std::move(image), pack_file.generic_string(), err);
}

auto mount_pack(const std::string& protocol, file_region pack_region, std::string& err) -> bool
{
    const std::string label = pack_region.name;
    auto image = std::make_shared<pack_detail::pack_image>();
    if(!pack_detail::open_pack_image(std::move(pack_region), *image, err))
    {
        err = label + ": " + err;
        return false;
    }
    return mount_image(protocol, std::move(image), label, err);
}

void unmount_packs()
{
    std::unique_lock lock(pack_detail::mounts_mutex);
    pack_detail::mounts.clear();
}

auto has_mounted_packs() -> bool
{
    return pack_detail::has_mounts();
}

auto lookup_packed_file(const path& file) -> packed_lookup
{
    const auto found = pack_detail::find_entry(file);
    if(found.record == nullptr)
    {
        return {};
    }
    return pack_detail::read_found_entry(found, file);
}

auto find_packed_file(const path& file) -> packed_file
{
    return lookup_packed_file(file).file;
}

auto is_readable_file(const path& file) -> bool
{
    const auto found = pack_detail::find_entry(file);
    if(found.record != nullptr)
    {
        return !found.record->is_deleted();
    }
    error_code ec;
    const auto disk_path = pack_detail::get_disk_path(file);
    return exists(disk_path, ec) && !is_directory(disk_path, ec);
}

auto open_readable_stream(const path& file) -> std::unique_ptr<std::istream>
{
    auto lookup = lookup_packed_file(file);
    if(lookup.status == packed_status::found)
    {
        return std::make_unique<pack_detail::packed_istream>(std::move(lookup.file));
    }
    if(lookup.status != packed_status::absent)
    {
        // A deleted entry hides the disk as well as the packs under it; a damaged one was logged.
        return nullptr;
    }
    auto stream = std::make_unique<file_istream>(pack_detail::get_disk_path(file), std::ios::binary);
    if(!stream->is_open())
    {
        return nullptr;
    }
    return stream;
}

} // namespace fs
