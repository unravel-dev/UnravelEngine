#include "mapped_file_reader.h"
#include "filesystem.h"
#include "mio.hpp"


#include <chrono>
#include <fstream>
#include <utility>
#include <vector>

#define USE_MIO 1
//#define TEST_MIO 1
#if TEST_MIO
#define XXH_INLINE_ALL
#include "xxhash.h"
#include <logging/logging.h>
#endif

namespace fs
{
namespace
{
class mapped_streambuf : public std::streambuf
{
public:
    mapped_streambuf(const char* data, size_t size)
    {
        begin_ = const_cast<char*>(data);
        end_ = begin_ + static_cast<std::streamsize>(size);
        setg(begin_, begin_, end_);
    }

protected:
    auto seekoff(std::streamoff off, std::ios_base::seekdir way, std::ios_base::openmode which)
        -> std::streampos override
    {
        if((which & std::ios_base::in) == 0)
        {
            return std::streampos(std::streamoff(-1));
        }

        char* next = gptr();
        switch(way)
        {
        case std::ios_base::beg:
            next = begin_ + off;
            break;
        case std::ios_base::cur:
            next = gptr() + off;
            break;
        case std::ios_base::end:
            next = end_ + off;
            break;
        default:
            return std::streampos(std::streamoff(-1));
        }

        if(next < begin_ || next > end_)
        {
            return std::streampos(std::streamoff(-1));
        }

        setg(begin_, next, end_);
        return static_cast<std::streampos>(next - begin_);
    }

    auto seekpos(std::streampos sp, std::ios_base::openmode which) -> std::streampos override
    {
        return seekoff(static_cast<std::streamoff>(sp), std::ios_base::beg, which);
    }

private:
    char* begin_ = nullptr;
    char* end_ = nullptr;
};

#if TEST_MIO
auto checksum_bytes(const void* data, size_t size) -> std::uint64_t
{
    return XXH3_64bits(data, size);
}

void benchmark_mapped_vs_stream(const std::string& absolute_path)
{
    using clock_t = std::chrono::high_resolution_clock;

    const auto mmap_create_start = clock_t::now();
    std::error_code error;
    mio::mmap_source mapped = mio::make_mmap_source(absolute_path, error);
    const auto mmap_create_us =
        std::chrono::duration_cast<std::chrono::microseconds>(clock_t::now() - mmap_create_start).count();

    if(error || !mapped.is_mapped() || mapped.size() == 0)
    {
        APPLOG_WARNING("mapped_file_reader benchmark '{}': mmap create failed ({})", absolute_path, error.message());
        return;
    }

    const auto mapped_size = mapped.size();

    // Direct mmap consume (fingerprint-style: hash mapped bytes in place).
    const auto mmap_hash_start = clock_t::now();
    const auto mapped_checksum = checksum_bytes(mapped.data(), mapped_size);
    const auto mmap_hash_us =
        std::chrono::duration_cast<std::chrono::microseconds>(clock_t::now() - mmap_hash_start).count();
    const auto mmap_total_us = mmap_create_us + mmap_hash_us;

    // mmap -> istream -> read_stream (shader/meta binary loaders).
    const auto mmap_stream_create_start = clock_t::now();
    mapped_streambuf mapped_buffer(mapped.data(), mapped_size);
    std::istream mmap_stream(&mapped_buffer);
    const auto mmap_stream_create_us =
        std::chrono::duration_cast<std::chrono::microseconds>(clock_t::now() - mmap_stream_create_start).count();

    const auto mmap_read_stream_start = clock_t::now();
    auto mmap_stream_buffer = fs::read_stream(mmap_stream);
    const auto mmap_read_stream_us =
        std::chrono::duration_cast<std::chrono::microseconds>(clock_t::now() - mmap_read_stream_start).count();
    const auto mmap_stream_total_us = mmap_stream_create_us + mmap_read_stream_us;

    // ifstream -> read_stream.
    const auto stream_total_start = clock_t::now();
    std::ifstream stream(absolute_path, std::ios::binary);
    if(!stream)
    {
        APPLOG_WARNING("mapped_file_reader benchmark '{}': ifstream open failed", absolute_path);
        return;
    }

    auto buffer = fs::read_stream(stream);
    const auto stream_total_us =
        std::chrono::duration_cast<std::chrono::microseconds>(clock_t::now() - stream_total_start).count();
    const auto stream_size = buffer.size();

    const auto stream_checksum = checksum_bytes(buffer.data(), buffer.size());
    const auto mmap_stream_checksum = checksum_bytes(mmap_stream_buffer.data(), mmap_stream_buffer.size());
    const bool checksum_match = mapped_checksum == stream_checksum && mapped_checksum == mmap_stream_checksum
                                && mapped_size == stream_size && mapped_size == mmap_stream_buffer.size();

    APPLOG_INFO(
        "mapped_file_reader benchmark '{}': "
        "mmap_create={} us, mmap_hash={} us, mmap_total={} us | "
        "mmap_stream_create={} us, mmap_read_stream={} us, mmap_stream_total={} us | "
        "ifstream_total={} us | "
        "bytes={}, ifstream/mmap_hash={:.2f}x, ifstream/mmap_stream={:.2f}x, checksum_match={}",
        absolute_path,
        mmap_create_us,
        mmap_hash_us,
        mmap_total_us,
        mmap_stream_create_us,
        mmap_read_stream_us,
        mmap_stream_total_us,
        stream_total_us,
        stream_size,
        mmap_total_us > 0 ? static_cast<double>(stream_total_us) / static_cast<double>(mmap_total_us) : 0.0,
        mmap_stream_total_us > 0 ? static_cast<double>(stream_total_us) / static_cast<double>(mmap_stream_total_us)
                                 : 0.0,
        checksum_match);
}
#endif
} // namespace

struct mapped_file_reader::impl
{
    mio::mmap_source mapped{};
    std::ifstream fallback{};
    std::unique_ptr<mapped_streambuf> mapped_buffer{};
    std::unique_ptr<std::istream> mapped_stream{};
    std::istream* active_stream = nullptr;
    bool mapped_io = false;

    void open(const std::string& absolute_path)
    {
#if USE_MIO
        std::error_code error;
        mapped = mio::make_mmap_source(absolute_path, error);
        if(!error && mapped.is_mapped() && mapped.size() > 0)
        {
            mapped_buffer = std::make_unique<mapped_streambuf>(mapped.data(), mapped.size());
            mapped_stream = std::make_unique<std::istream>(mapped_buffer.get());
            active_stream = mapped_stream.get();
            mapped_io = true;
#if TEST_MIO
            benchmark_mapped_vs_stream(absolute_path);
#endif
            return;
        }
#endif
        fallback.open(absolute_path, std::ios::binary);
        if(fallback.is_open())
        {
            active_stream = &fallback;
            mapped_io = false;
        }
    }
};

mapped_file_reader::~mapped_file_reader() = default;

mapped_file_reader::mapped_file_reader(const path& file_path)
    : impl_(std::make_unique<impl>())
{
    impl_->open(file_path.string());
}

mapped_file_reader::mapped_file_reader(const std::string& absolute_path)
    : impl_(std::make_unique<impl>())
{
    impl_->open(absolute_path);
}

auto mapped_file_reader::is_open() const -> bool
{
    return impl_ && impl_->active_stream != nullptr;
}

auto mapped_file_reader::is_mapped() const -> bool
{
    return impl_ && impl_->mapped_io;
}

auto mapped_file_reader::data() const -> const char*
{
    if(!impl_ || !impl_->mapped_io)
    {
        return nullptr;
    }
    return impl_->mapped.data();
}

auto mapped_file_reader::size() const -> size_t
{
    if(!impl_ || !impl_->mapped_io)
    {
        return 0;
    }
    return impl_->mapped.size();
}

auto mapped_file_reader::stream() -> std::istream&
{
    return *impl_->active_stream;
}

} // namespace fs
