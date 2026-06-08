#include "file_istream.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace fs
{
namespace
{

class file_istreambuf : public std::streambuf
{
public:
    file_istreambuf() = default;

    file_istreambuf(FILE* file, file_stream_ownership ownership)
        : file_(file)
        , owns_file_(ownership == file_stream_ownership::take_ownership)
    {
    }

    ~file_istreambuf() override
    {
        close_file();
    }

    file_istreambuf(const file_istreambuf&) = delete;
    auto operator=(const file_istreambuf&) -> file_istreambuf& = delete;

    file_istreambuf(file_istreambuf&& other) noexcept
        : file_(other.file_)
        , owns_file_(other.owns_file_)
        , cached_size_(other.cached_size_)
        , underflow_char_(other.underflow_char_)
    {
        other.file_ = nullptr;
        other.owns_file_ = false;
        other.cached_size_ = -1;
        other.invalidate_get_area();
        invalidate_get_area();
    }

    auto operator=(file_istreambuf&& other) noexcept -> file_istreambuf&
    {
        if(this != &other)
        {
            close_file();
            file_ = other.file_;
            owns_file_ = other.owns_file_;
            cached_size_ = other.cached_size_;
            underflow_char_ = other.underflow_char_;
            other.file_ = nullptr;
            other.owns_file_ = false;
            other.cached_size_ = -1;
            other.invalidate_get_area();
            invalidate_get_area();
        }
        return *this;
    }

    auto open(const std::string& absolute_path, std::ios_base::openmode mode) -> bool
    {
        close_file();
        mode |= std::ios_base::in;
        char stdio_mode[4]{};
        if(!file_stream_detail::openmode_to_stdio_mode(mode, stdio_mode))
        {
            return false;
        }
        file_ = std::fopen(absolute_path.c_str(), stdio_mode);
        owns_file_ = file_ != nullptr;
        cached_size_ = -1;
        invalidate_get_area();
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
        cached_size_ = -1;
        invalidate_get_area();
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
        close_file();
        cached_size_ = -1;
        invalidate_get_area();
    }

    auto tell_file() const -> long long
    {
        if(file_ == nullptr)
        {
            return -1;
        }
        if(gptr() != nullptr && egptr() >= gptr())
        {
            return file_stream_detail::stdio_tell64(file_) -
                   static_cast<long long>(egptr() - gptr());
        }
        return file_stream_detail::stdio_tell64(file_);
    }

    auto seek_file(long long offset, int whence) -> bool
    {
        if(file_ == nullptr)
        {
            return false;
        }
        if(whence == SEEK_END)
        {
            const long long size = cached_file_size();
            if(size < 0)
            {
                return false;
            }
            offset = size + offset;
            whence = SEEK_SET;
        }
        if(whence == SEEK_CUR && gptr() != nullptr && egptr() >= gptr())
        {
            offset -= static_cast<long long>(egptr() - gptr());
        }
        invalidate_get_area();
        return file_stream_detail::stdio_seek64(file_, offset, whence) == 0;
    }

    auto query_file_size() -> long long
    {
        return cached_file_size();
    }

protected:
    auto underflow() -> int_type override
    {
        if(gptr() != nullptr && gptr() < egptr())
        {
            return traits_type::to_int_type(*gptr());
        }
        if(file_ == nullptr)
        {
            return traits_type::eof();
        }
        if(std::fread(&underflow_char_, 1, 1, file_) != 1)
        {
            invalidate_get_area();
            return traits_type::eof();
        }
        setg(&underflow_char_, &underflow_char_, &underflow_char_ + 1);
        return traits_type::to_int_type(underflow_char_);
    }

    auto xsgetn(char* dest, std::streamsize count) -> std::streamsize override
    {
        if(count <= 0 || dest == nullptr || file_ == nullptr)
        {
            return 0;
        }
        std::streamsize copied = 0;
        if(gptr() != egptr())
        {
            const std::streamsize available = static_cast<std::streamsize>(egptr() - gptr());
            const std::streamsize chunk = std::min(count, available);
            traits_type::copy(dest, gptr(), static_cast<std::size_t>(chunk));
            gbump(static_cast<int>(chunk));
            copied += chunk;
        }
        while(copied < count)
        {
            const std::size_t bytes_read =
                std::fread(dest + copied, 1, static_cast<std::size_t>(count - copied), file_);
            if(bytes_read == 0)
            {
                break;
            }
            copied += static_cast<std::streamsize>(bytes_read);
        }
        return copied;
    }

    auto seekoff(std::streamoff off, std::ios_base::seekdir way, std::ios_base::openmode which)
        -> std::streampos override
    {
        if((which & std::ios_base::in) == 0 || file_ == nullptr)
        {
            return std::streampos(std::streamoff(-1));
        }
        const long long file_size = cached_file_size();
        if(file_size < 0)
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
            next_pos = file_size + static_cast<long long>(off);
            break;
        default:
            return std::streampos(std::streamoff(-1));
        }
        if(next_pos < 0 || next_pos > file_size)
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
    auto cached_file_size() -> long long
    {
        if(cached_size_ >= 0)
        {
            return cached_size_;
        }
        cached_size_ = file_stream_detail::stdio_file_size(file_);
        return cached_size_;
    }

    void invalidate_get_area()
    {
        setg(nullptr, nullptr, nullptr);
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
    long long cached_size_ = -1;
    char underflow_char_ = '\0';
};

auto open_read_buffer(const std::string& absolute_path, std::ios_base::openmode mode)
    -> std::unique_ptr<file_istreambuf>
{
    auto buffer = std::make_unique<file_istreambuf>();
    if(!buffer->open(absolute_path, mode))
    {
        return nullptr;
    }
    return buffer;
}

} // namespace

struct file_istream::impl
{
    std::unique_ptr<file_istreambuf> buffer;

    explicit impl(std::unique_ptr<file_istreambuf> stream_buffer)
        : buffer(std::move(stream_buffer))
    {
    }
};

file_istream::file_istream()
    : std::istream(nullptr)
    , impl_(std::make_unique<impl>(std::make_unique<file_istreambuf>()))
{
    rdbuf(impl_->buffer.get());
}

file_istream::file_istream(const path& file_path, std::ios_base::openmode mode)
    : file_istream(file_path.string(), mode)
{
}

file_istream::file_istream(const std::string& absolute_path, std::ios_base::openmode mode)
    : std::istream(nullptr)
{
    auto buffer = open_read_buffer(absolute_path, mode);
    if(!buffer)
    {
        buffer = std::make_unique<file_istreambuf>();
    }
    impl_ = std::make_unique<impl>(std::move(buffer));
    rdbuf(impl_->buffer.get());
    if(!is_open())
    {
        setstate(std::ios::failbit);
    }
}

void file_istream::open(const path& file_path, std::ios_base::openmode mode)
{
    open(file_path.string(), mode);
}

void file_istream::open(const std::string& absolute_path, std::ios_base::openmode mode)
{
    if(!impl_)
    {
        impl_ = std::make_unique<impl>(std::make_unique<file_istreambuf>());
        rdbuf(impl_->buffer.get());
    }
    if(!impl_->buffer->open(absolute_path, mode))
    {
        setstate(std::ios::failbit);
        return;
    }
    clear();
}

file_istream::file_istream(FILE* file, file_stream_ownership ownership)
    : std::istream(nullptr)
    , impl_(std::make_unique<impl>(std::make_unique<file_istreambuf>()))
{
    impl_->buffer->attach(file, ownership);
    rdbuf(impl_->buffer.get());
}

file_istream::file_istream(file_istream&& other) noexcept
    : std::istream(nullptr)
    , impl_(std::move(other.impl_))
{
    rdbuf(impl_ ? impl_->buffer.get() : nullptr);
    other.rdbuf(nullptr);
}

auto file_istream::operator=(file_istream&& other) noexcept -> file_istream&
{
    if(this != &other)
    {
        impl_ = std::move(other.impl_);
        rdbuf(impl_ ? impl_->buffer.get() : nullptr);
        other.rdbuf(nullptr);
    }
    return *this;
}

file_istream::~file_istream() = default;

auto file_istream::is_open() const -> bool
{
    return impl_ && impl_->buffer && impl_->buffer->is_open();
}

auto file_istream::native_handle() const -> FILE*
{
    return impl_ && impl_->buffer ? impl_->buffer->native_handle() : nullptr;
}

auto file_istream::tell() const -> long long
{
    return impl_ && impl_->buffer ? impl_->buffer->tell_file() : -1;
}

auto file_istream::file_size() const -> long long
{
    return impl_ && impl_->buffer ? impl_->buffer->query_file_size() : -1;
}

auto file_istream::seek(long long offset, int whence) -> bool
{
    return impl_ && impl_->buffer && impl_->buffer->seek_file(offset, whence);
}

void file_istream::close()
{
    if(impl_ && impl_->buffer)
    {
        impl_->buffer->close();
    }
    clear();
}

} // namespace fs
