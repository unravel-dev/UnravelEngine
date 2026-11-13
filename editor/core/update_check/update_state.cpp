#include "update_state.h"

#include <engine/assets/impl/asset_writer.h>
#include <serialization/associative_archive.h>
#include <serialization/serialization.h>
#include <serialization/types/string.hpp>

#include <fstream>
#include <sstream>

namespace unravel::update_check
{

SAVE(update_state)
{
    try_save(ar, ser20::make_nvp("last_attempt", obj.last_attempt));
    try_save(ar, ser20::make_nvp("last_answer", obj.last_answer));
    try_save(ar, ser20::make_nvp("newest_version", obj.newest_version));
    try_save(ar, ser20::make_nvp("newest_tag", obj.newest_tag));
    try_save(ar, ser20::make_nvp("dismissed_version", obj.dismissed_version));
}

LOAD(update_state)
{
    try_load(ar, ser20::make_nvp("last_attempt", obj.last_attempt));
    try_load(ar, ser20::make_nvp("last_answer", obj.last_answer));
    try_load(ar, ser20::make_nvp("newest_version", obj.newest_version));
    try_load(ar, ser20::make_nvp("newest_tag", obj.newest_tag));
    try_load(ar, ser20::make_nvp("dismissed_version", obj.dismissed_version));
}

namespace
{

constexpr const char* STATE_NODE_NAME = "update_check";
constexpr const char* STATE_FILE_PREFIX = "update_check_";
constexpr const char* STATE_FILE_EXTENSION = ".cfg";

} // namespace

auto get_update_state_path(hpp::string_view platform) -> fs::path
{
    return fs::persistent_path() / "unravel" / (STATE_FILE_PREFIX + std::string(platform) + STATE_FILE_EXTENSION);
}

auto write_update_state(const update_state& state) -> std::string
{
    std::ostringstream stream;
    {
        auto ar = ser20::create_oarchive_associative(stream);
        try_save(ar, ser20::make_nvp(STATE_NODE_NAME, state));
    }
    return stream.str();
}

auto read_update_state(const std::string& text) -> update_state
{
    update_state state;
    try
    {
        std::istringstream stream(text);
        auto ar = ser20::create_iarchive_associative(stream);
        if(!try_load(ar, ser20::make_nvp(STATE_NODE_NAME, state)))
        {
            return {};
        }
    }
    catch(const std::exception&)
    {
        return {};
    }
    return state;
}

auto load_update_state(const fs::path& path) -> update_state
{
    std::ifstream stream(path, std::ios::binary);
    if(!stream.good())
    {
        return {};
    }
    return read_update_state(fs::read_stream_str(stream));
}

auto save_update_state(const fs::path& path, const update_state& state) -> bool
{
    fs::error_code err;
    fs::create_directories(path.parent_path(), err);
    const auto text = write_update_state(state);
    bool is_written = false;
    asset_writer::atomic_write_file(
        path,
        [&](const fs::path& temp)
        {
            std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
            stream.write(text.data(), std::streamsize(text.size()));
            stream.close();
            is_written = stream.good();
        },
        err);
    return is_written && !err;
}

} // namespace unravel::update_check
