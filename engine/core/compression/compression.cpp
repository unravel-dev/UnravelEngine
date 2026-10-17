#include "compression.h"

#include <zstd.h>

namespace compression
{
namespace
{

/// Identifies zstd in data written with it. Another codec gets another id.
constexpr std::uint8_t ZSTD_CODEC_ID = 1;

constexpr int ZSTD_FAST_LEVEL = 1;
constexpr int ZSTD_BALANCED_LEVEL = 3;
constexpr int ZSTD_BEST_LEVEL = 19;

auto to_zstd_level(level compression_level) -> int
{
    switch(compression_level)
    {
        case level::fast:
            return ZSTD_FAST_LEVEL;
        case level::best:
            return ZSTD_BEST_LEVEL;
        case level::balanced:
        default:
            return ZSTD_BALANCED_LEVEL;
    }
}

} // namespace

auto get_codec_id() -> std::uint8_t
{
    return ZSTD_CODEC_ID;
}

auto get_codec_name() -> const char*
{
    return "zstd";
}

struct compressor::impl
{
    ZSTD_CCtx* context = ZSTD_createCCtx();
    int zstd_level{};

    ~impl()
    {
        ZSTD_freeCCtx(context);
    }
};

compressor::compressor(level compression_level) : impl_(std::make_unique<impl>())
{
    impl_->zstd_level = to_zstd_level(compression_level);
}

compressor::~compressor() = default;

auto compressor::get_bound(std::size_t raw_size) const -> std::size_t
{
    return ZSTD_compressBound(raw_size);
}

auto compressor::compress(const std::uint8_t* source,
                          std::size_t source_size,
                          std::uint8_t* destination,
                          std::size_t capacity,
                          std::string& err) -> std::size_t
{
    if(impl_->context == nullptr)
    {
        err = "cannot create a zstd compression context";
        return 0;
    }
    const std::size_t size =
        ZSTD_compressCCtx(impl_->context, destination, capacity, source, source_size, impl_->zstd_level);
    if(ZSTD_isError(size))
    {
        err = std::string("zstd: ") + ZSTD_getErrorName(size);
        return 0;
    }
    return size;
}

struct decompressor::impl
{
    ZSTD_DCtx* context = ZSTD_createDCtx();

    ~impl()
    {
        ZSTD_freeDCtx(context);
    }
};

decompressor::decompressor() : impl_(std::make_unique<impl>())
{
}

decompressor::~decompressor() = default;

auto decompressor::decompress(const std::uint8_t* source,
                              std::size_t source_size,
                              std::uint8_t* destination,
                              std::size_t raw_size) -> bool
{
    if(impl_->context == nullptr)
    {
        return false;
    }
    const std::size_t size = ZSTD_decompressDCtx(impl_->context, destination, raw_size, source, source_size);
    return !ZSTD_isError(size) && size == raw_size;
}

} // namespace compression
