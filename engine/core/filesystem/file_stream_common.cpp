#include "file_stream_common.h"

namespace fs::file_stream_detail
{

#if defined(_WIN32)
auto stdio_tell64(FILE* file) -> long long
{
    return static_cast<long long>(_ftelli64(file));
}

auto stdio_seek64(FILE* file, long long offset, int origin) -> int
{
    return _fseeki64(file, offset, origin);
}
#else
auto stdio_tell64(FILE* file) -> long long
{
    return static_cast<long long>(ftello(file));
}

auto stdio_seek64(FILE* file, long long offset, int origin) -> int
{
    return fseeko(file, static_cast<off_t>(offset), origin);
}
#endif

auto stdio_file_size(FILE* file) -> long long
{
    if(file == nullptr)
    {
        return -1;
    }
    const long long current = stdio_tell64(file);
    if(current < 0)
    {
        return -1;
    }
    if(stdio_seek64(file, 0, SEEK_END) != 0)
    {
        return -1;
    }
    const long long size = stdio_tell64(file);
    if(size < 0)
    {
        return -1;
    }
    if(stdio_seek64(file, current, SEEK_SET) != 0)
    {
        return -1;
    }
    return size;
}

auto openmode_to_stdio_mode(std::ios_base::openmode mode, char (&out)[4]) -> bool
{
    const bool append = (mode & std::ios_base::app) != 0;
    const bool binary = (mode & std::ios_base::binary) != 0;
    const bool input = (mode & std::ios_base::in) != 0;
    const bool output = (mode & std::ios_base::out) != 0;
    const bool truncate = (mode & std::ios_base::trunc) != 0;

    char* cursor = out;
    if(input && !output)
    {
        *cursor++ = 'r';
    }
    else if(output && !input)
    {
        *cursor++ = append ? 'a' : 'w';
    }
    else if(input && output)
    {
        if(append)
        {
            *cursor++ = 'a';
        }
        else if(truncate)
        {
            *cursor++ = 'w';
        }
        else
        {
            *cursor++ = 'r';
        }
        *cursor++ = '+';
    }
    else
    {
        out[0] = '\0';
        return false;
    }

    if(binary)
    {
        *cursor++ = 'b';
    }
    *cursor = '\0';
    return true;
}

} // namespace fs::file_stream_detail
