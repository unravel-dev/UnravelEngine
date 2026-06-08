#include "file_ostream.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>

namespace fs
{
namespace
{

class file_ostreambuf : public std::streambuf
{
public:
    file_ostreambuf() = default;

    file_ostreambuf(FILE* file, file_stream_ownership ownership)
        : file_(file)
        , owns_file_(ownership == file_stream_ownership::take_ownership)
    {
        reset_put_area();
    }

    ~file_ostreambuf() override
    {
        close_file();
    }

    file_ostreambuf(const file_ostreambuf&) = delete;
    auto operator=(const file_ostreambuf&) -> file_ostreambuf& = delete;

    file_ostreambuf(file_ostreambuf&& other) noexcept
        : file_(other.file_)
        , owns_file_(other.owns_file_)
        , buffer_(other.buffer_)
    {
        other.file_ = nullptr;
        other.owns_file_ = false;
        other.setp(nullptr, nullptr);
        reset_put_area();
    }

    auto open(const std::string& absolute_path, std::ios_base::openmode mode) -> bool
    {
        close_file();
        mode |= std::ios_base::out;
        char stdio_mode[4]{};
        if(!file_stream_detail::openmode_to_stdio_mode(mode, stdio_mode))
        {
            return false;
        }
        file_ = std::fopen(absolute_path.c_str(), stdio_mode);
        owns_file_ = file_ != nullptr;
        reset_put_area();
        return file_ != nullptr;
    }

    auto attach(FILE* file, file_stream_ownership ownership) -> bool
    {
        close_file();
        if(file == nullptr)
        {
            return false;
        }
        file_ = file;
        owns_file_ = ownership == file_stream_ownership::take_ownership;
        reset_put_area();
        return true;
    }

    auto is_open() const -> bool
    {
        return file_ != nullptr;
    }

    auto native_handle() const -> FILE*
    {
        return file_;
    }

    void close()
    {
        sync_put_area();
        close_file();
        setp(nullptr, nullptr);
    }

    auto tell_file() const -> long long
    {
        if(file_ == nullptr)
        {
            return -1;
        }
        const long long file_pos = file_stream_detail::stdio_tell64(file_);
        if(file_pos < 0)
        {
            return -1;
        }
        if(pptr() != nullptr && pbase() != nullptr && pptr() >= pbase())
        {
            return file_pos + static_cast<long long>(pptr() - pbase());
        }
        return file_pos;
    }

    auto seek_file(long long offset, int whence) -> bool
    {
        if(file_ == nullptr)
        {
            return false;
        }
        if(!sync_put_area())
        {
            return false;
        }
        if(whence == SEEK_END)
        {
            const long long size = file_stream_detail::stdio_file_size(file_);
            if(size < 0)
            {
                return false;
            }
            offset = size + offset;
            whence = SEEK_SET;
        }
        if(file_stream_detail::stdio_seek64(file_, offset, whence) != 0)
        {
            return false;
        }
        reset_put_area();
        return true;
    }

    auto query_file_size() -> long long
    {
        if(file_ == nullptr)
        {
            return -1;
        }
        if(!sync_put_area())
        {
            return -1;
        }
        return file_stream_detail::stdio_file_size(file_);
    }

    auto flush_file() -> bool
    {
        return sync_put_area() && file_ != nullptr && std::fflush(file_) == 0;
    }

protected:
    auto overflow(int_type ch) -> int_type override
    {
        if(!sync_put_area())
        {
            return traits_type::eof();
        }
        if(ch != traits_type::eof())
        {
            const char value = traits_type::to_char_type(ch);
            if(std::fwrite(&value, 1, 1, file_) != 1)
            {
                return traits_type::eof();
            }
        }
        reset_put_area();
        return ch;
    }

    auto xsputn(const char* src, std::streamsize count) -> std::streamsize override
    {
        if(count <= 0 || src == nullptr)
        {
            return 0;
        }
        std::streamsize written = 0;
        while(written < count)
        {
            if(pptr() == epptr() && overflow(traits_type::to_int_type(src[written])) == traits_type::eof())
            {
                break;
            }
            const std::streamsize available = epptr() - pptr();
            const std::streamsize chunk = std::min(count - written, available);
            traits_type::copy(pptr(), src + written, static_cast<std::size_t>(chunk));
            pbump(static_cast<int>(chunk));
            written += chunk;
        }
        return written;
    }

    auto sync() -> int override
    {
        return sync_put_area() ? 0 : -1;
    }

    auto seekoff(std::streamoff off, std::ios_base::seekdir way, std::ios_base::openmode which)
        -> std::streampos override
    {
        if((which & std::ios_base::out) == 0 || file_ == nullptr)
        {
            return std::streampos(std::streamoff(-1));
        }
        long long next_pos = 0;
        switch(way)
        {
        case std::ios_base::beg:
            next_pos = static_cast<long long>(off);
            break;
        case std::ios_base::cur:
            next_pos = tell_file() + static_cast<long long>(off);
            break;
        case std::ios_base::end:
        {
            const long long size = query_file_size();
            if(size < 0)
            {
                return std::streampos(std::streamoff(-1));
            }
            next_pos = size + static_cast<long long>(off);
            break;
        }
        default:
            return std::streampos(std::streamoff(-1));
        }
        if(next_pos < 0)
        {
            return std::streampos(std::streamoff(-1));
        }
        if(!seek_file(next_pos, SEEK_SET))
        {
            return std::streampos(std::streamoff(-1));
        }
        return std::streampos(static_cast<std::streamoff>(next_pos));
    }

    auto seekpos(std::streampos sp, std::ios_base::openmode which) -> std::streampos override
    {
        return seekoff(static_cast<std::streamoff>(sp), std::ios_base::beg, which);
    }

private:
    void reset_put_area()
    {
        setp(buffer_.data(), buffer_.data() + static_cast<std::ptrdiff_t>(buffer_.size()));
    }

    auto sync_put_area() -> bool
    {
        if(file_ == nullptr)
        {
            return false;
        }
        if(pptr() == nullptr || pbase() == nullptr)
        {
            return true;
        }
        const std::size_t pending = static_cast<std::size_t>(pptr() - pbase());
        if(pending == 0)
        {
            return true;
        }
        const std::size_t written = std::fwrite(pbase(), 1, pending, file_);
        reset_put_area();
        return written == pending;
    }

    void close_file()
    {
        if(file_ != nullptr && owns_file_)
        {
            std::fclose(file_);
        }
        file_ = nullptr;
        owns_file_ = false;
    }

    FILE* file_ = nullptr;
    bool owns_file_ = false;
    std::array<char, file_stream_detail::default_buffer_size> buffer_{};
};

auto open_write_buffer(const std::string& absolute_path, std::ios_base::openmode mode)
    -> std::unique_ptr<file_ostreambuf>
{
    auto buffer = std::make_unique<file_ostreambuf>();
    if(!buffer->open(absolute_path, mode))
    {
        return nullptr;
    }
    return buffer;
}

} // namespace

struct file_ostream::impl
{
    std::unique_ptr<file_ostreambuf> buffer;

    explicit impl(std::unique_ptr<file_ostreambuf> stream_buffer)
        : buffer(std::move(stream_buffer))
    {
    }
};

file_ostream::file_ostream()
    : std::ostream(nullptr)
    , impl_(std::make_unique<impl>(std::make_unique<file_ostreambuf>()))
{
    rdbuf(impl_->buffer.get());
}

file_ostream::file_ostream(const path& file_path, std::ios_base::openmode mode)
    : file_ostream(file_path.string(), mode)
{
}

file_ostream::file_ostream(const std::string& absolute_path, std::ios_base::openmode mode)
    : std::ostream(nullptr)
{
    auto buffer = open_write_buffer(absolute_path, mode);
    if(!buffer)
    {
        buffer = std::make_unique<file_ostreambuf>();
    }
    impl_ = std::make_unique<impl>(std::move(buffer));
    rdbuf(impl_->buffer.get());
    if(!is_open())
    {
        setstate(std::ios::failbit);
    }
}

void file_ostream::open(const path& file_path, std::ios_base::openmode mode)
{
    open(file_path.string(), mode);
}

void file_ostream::open(const std::string& absolute_path, std::ios_base::openmode mode)
{
    if(!impl_)
    {
        impl_ = std::make_unique<impl>(std::make_unique<file_ostreambuf>());
        rdbuf(impl_->buffer.get());
    }
    if(!impl_->buffer->open(absolute_path, mode))
    {
        setstate(std::ios::failbit);
        return;
    }
    clear();
}

file_ostream::file_ostream(FILE* file, file_stream_ownership ownership)
    : std::ostream(nullptr)
    , impl_(std::make_unique<impl>(std::make_unique<file_ostreambuf>()))
{
    impl_->buffer->attach(file, ownership);
    rdbuf(impl_->buffer.get());
}

file_ostream::file_ostream(file_ostream&& other) noexcept
    : std::ostream(nullptr)
    , impl_(std::move(other.impl_))
{
    rdbuf(impl_ ? impl_->buffer.get() : nullptr);
    other.rdbuf(nullptr);
}

auto file_ostream::operator=(file_ostream&& other) noexcept -> file_ostream&
{
    if(this != &other)
    {
        impl_ = std::move(other.impl_);
        rdbuf(impl_ ? impl_->buffer.get() : nullptr);
        other.rdbuf(nullptr);
    }
    return *this;
}

file_ostream::~file_ostream() = default;

auto file_ostream::is_open() const -> bool
{
    return impl_ && impl_->buffer && impl_->buffer->is_open();
}

auto file_ostream::native_handle() const -> FILE*
{
    return impl_ && impl_->buffer ? impl_->buffer->native_handle() : nullptr;
}

auto file_ostream::tell() const -> long long
{
    return impl_ && impl_->buffer ? impl_->buffer->tell_file() : -1;
}

auto file_ostream::file_size() const -> long long
{
    return impl_ && impl_->buffer ? impl_->buffer->query_file_size() : -1;
}

auto file_ostream::seek(long long offset, int whence) -> bool
{
    return impl_ && impl_->buffer && impl_->buffer->seek_file(offset, whence);
}

auto file_ostream::flush_file() -> bool
{
    flush();
    return impl_ && impl_->buffer && impl_->buffer->flush_file();
}

void file_ostream::close()
{
    if(impl_ && impl_->buffer)
    {
        impl_->buffer->close();
    }
    clear();
}

} // namespace fs
