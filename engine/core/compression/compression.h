#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

/// General-purpose lossless compression of byte blocks. The codec behind it is an implementation
/// detail of this library: callers keep get_codec_id() next to what they compress, so data written
/// by a build with another codec is recognized instead of misread.
namespace compression
{

/// How hard the compressor works: fast for development builds, best for release builds. Every
/// level decodes at the same speed.
enum class level : std::uint8_t
{
    fast,
    balanced,
    best,
};

/// The codec this build compresses with and decodes; never 0, which callers may use for "stored".
auto get_codec_id() -> std::uint8_t;

/// The codec's name, for logs and errors.
auto get_codec_name() -> const char*;

/// Compresses blocks. Reusing one compressor for many blocks avoids reallocating its state; one
/// compressor is not safe to use from two threads at once.
class compressor
{
public:
    explicit compressor(level compression_level = level::balanced);
    ~compressor();
    compressor(const compressor&) = delete;
    auto operator=(const compressor&) -> compressor& = delete;

    /// The largest compressed size of `raw_size` bytes: the capacity compress needs.
    auto get_bound(std::size_t raw_size) const -> std::size_t;

    /**
     * @brief Compresses `source` into `destination`.
     * @return The compressed size, or 0 with `err` set when compression failed (for example
     * `capacity` below get_bound).
     */
    auto compress(const std::uint8_t* source,
                  std::size_t source_size,
                  std::uint8_t* destination,
                  std::size_t capacity,
                  std::string& err) -> std::size_t;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

/// Decodes blocks a compressor wrote. Not safe to use from two threads at once.
class decompressor
{
public:
    decompressor();
    ~decompressor();
    decompressor(const decompressor&) = delete;
    auto operator=(const decompressor&) -> decompressor& = delete;

    /**
     * @brief Decodes `source` into exactly `raw_size` bytes at `destination`.
     * @return False when `source` is damaged or does not decode to `raw_size` bytes.
     */
    auto decompress(const std::uint8_t* source,
                    std::size_t source_size,
                    std::uint8_t* destination,
                    std::size_t raw_size) -> bool;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace compression
