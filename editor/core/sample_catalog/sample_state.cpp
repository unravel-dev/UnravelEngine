#include "sample_state.h"

#include <https/fetch_schedule.h>
#include <serialization/associative_archive.h>
#include <serialization/serialization.h>

#include <fstream>
#include <sstream>

namespace unravel::sample_catalog
{

SAVE(fetch_state)
{
    try_save(ar, ser20::make_nvp("last_attempt", obj.last_attempt));
    try_save(ar, ser20::make_nvp("last_answer", obj.last_answer));
}

LOAD(fetch_state)
{
    try_load(ar, ser20::make_nvp("last_attempt", obj.last_attempt));
    try_load(ar, ser20::make_nvp("last_answer", obj.last_answer));
}

namespace
{

constexpr const char* FETCH_STATE_NODE_NAME = "sample_fetch";

} // namespace

auto is_fetch_due(const fetch_state& state, std::chrono::system_clock::time_point now) -> bool
{
    return https::is_fetch_due(state.last_attempt, state.last_answer, now, FETCH_INTERVAL, RETRY_INTERVAL);
}

void record_fetch(fetch_state& state, bool has_answer, std::chrono::system_clock::time_point now)
{
    const std::int64_t now_seconds = https::get_epoch_seconds(now);
    state.last_attempt = now_seconds;
    if(has_answer)
    {
        state.last_answer = now_seconds;
    }
}

auto write_fetch_state(const fetch_state& state) -> std::string
{
    std::ostringstream stream;
    {
        auto ar = ser20::create_oarchive_associative(stream);
        try_save(ar, ser20::make_nvp(FETCH_STATE_NODE_NAME, state));
    }
    return stream.str();
}

auto read_fetch_state(const std::string& text) -> fetch_state
{
    fetch_state state;
    try
    {
        std::istringstream stream(text);
        auto ar = ser20::create_iarchive_associative(stream);
        if(!try_load(ar, ser20::make_nvp(FETCH_STATE_NODE_NAME, state)))
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

auto load_fetch_state(const fs::path& path) -> fetch_state
{
    std::ifstream stream(path, std::ios::binary);
    if(!stream.good())
    {
        return {};
    }
    return read_fetch_state(fs::read_stream_str(stream));
}

auto save_fetch_state(const fs::path& path, const fetch_state& state) -> bool
{
    return https::write_file_atomically(path, write_fetch_state(state));
}

} // namespace unravel::sample_catalog
