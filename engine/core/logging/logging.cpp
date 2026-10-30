#include "logging.h"

#include <base/platform/config.hpp>
#include <spdlog/details/circular_q.h>
#include <spdlog/details/file_helper.h>
#include <spdlog/details/log_msg_buffer.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <memory>
#include <mutex>

#if UNRAVEL_PLATFORM_ANDROID
#include <spdlog/sinks/android_sink.h>
namespace spdlog
{
namespace sinks
{
using platform_sink_mt = android_sink_mt;
using platform_sink_st = android_sink_st;
} // namespace sinks
} // namespace spdlog
#else
namespace spdlog
{
namespace sinks
{
using platform_sink_mt = stdout_color_sink_mt;
using platform_sink_st = stdout_color_sink_st;
} // namespace sinks
} // namespace spdlog
#endif

namespace unravel
{
namespace
{
constexpr const char* LOG_PATTERN = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v"; // no %s/%#/%!
/// The lines kept until the log file opens; startup logs far fewer before a host names its file.
constexpr std::size_t EARLY_LINES_CAPACITY = 4096;
} // namespace

/// The log file: the lines are kept in memory until open() names the file, which then begins with them. One mutex
/// guards the lines and the file, so a line logged while the file takes over is neither lost nor written twice.
class log_file_sink final : public spdlog::sinks::base_sink<std::mutex>
{
public:
    log_file_sink() : early_lines_(EARLY_LINES_CAPACITY)
    {
    }

    auto open(const std::string& output_file) -> bool
    {
        auto file = std::make_unique<spdlog::details::file_helper>();
        try
        {
            file->open(output_file, true);
        }
        catch(const spdlog::spdlog_ex&)
        {
            return false;
        }
        const std::lock_guard<std::mutex> lock(mutex_);
        for(std::size_t i = 0; i < early_lines_.size(); ++i)
        {
            write(*file, early_lines_.at(i));
        }
        // Kept for the first file only: an empty queue keeps nothing.
        early_lines_ = {};
        file->flush();
        file_ = std::move(file);
        return true;
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        if(file_)
        {
            write(*file_, msg);
            return;
        }
        early_lines_.push_back(spdlog::details::log_msg_buffer{msg});
    }

    void flush_() override
    {
        if(file_)
        {
            file_->flush();
        }
    }

private:
    void write(spdlog::details::file_helper& file, const spdlog::details::log_msg& msg)
    {
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        file.write(formatted);
    }

    spdlog::details::circular_q<spdlog::details::log_msg_buffer> early_lines_;
    std::unique_ptr<spdlog::details::file_helper> file_;
};

auto get_mutable_logging_container() -> std::shared_ptr<spdlog::sinks::dist_sink_mt>
{
    static auto sink = std::make_shared<spdlog::sinks::dist_sink_mt>();
    return sink;
}

logging::logging()
    : file_(std::make_shared<log_file_sink>())
{
    spdlog::set_pattern(LOG_PATTERN);

    auto logging_container = get_mutable_logging_container();
    auto console_sink = std::make_shared<spdlog::sinks::platform_sink_mt>();

    logging_container->add_sink(console_sink);
    logging_container->add_sink(file_);
    auto logger = std::make_shared<spdlog::logger>(APPLOG, logging_container);
    logger->flush_on(spdlog::level::err);
    spdlog::initialize_logger(logger);
    spdlog::set_level(spdlog::level::trace);
}

auto logging::open_file(const std::string& output_file) -> bool
{
    return file_->open(output_file);
}

logging::~logging()
{
    spdlog::shutdown();
}
} // namespace unravel
