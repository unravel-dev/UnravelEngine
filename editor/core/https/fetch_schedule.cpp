#include "fetch_schedule.h"

#include <engine/assets/impl/asset_writer.h>

#include <fstream>

namespace unravel::https
{

auto get_epoch_seconds(std::chrono::system_clock::time_point time) -> std::int64_t
{
    return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
}

auto is_fetch_due(std::int64_t last_attempt,
                  std::int64_t last_answer,
                  std::chrono::system_clock::time_point now,
                  std::chrono::seconds interval,
                  std::chrono::seconds retry_interval) -> bool
{
    const std::int64_t now_seconds = get_epoch_seconds(now);
    if(last_attempt == 0 || last_attempt > now_seconds)
    {
        return true;
    }
    const bool was_answered = last_answer >= last_attempt;
    return std::chrono::seconds(now_seconds - last_attempt) >= (was_answered ? interval : retry_interval);
}

auto write_file_atomically(const fs::path& path, hpp::string_view data) -> bool
{
    fs::error_code error;
    fs::create_directories(path.parent_path(), error);
    bool is_written = false;
    asset_writer::atomic_write_file(
        path,
        [&](const fs::path& temp)
        {
            std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
            stream.write(data.data(), std::streamsize(data.size()));
            stream.close();
            is_written = stream.good();
        },
        error);
    return is_written && !error;
}

} // namespace unravel::https
