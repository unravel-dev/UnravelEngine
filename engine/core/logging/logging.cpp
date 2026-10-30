#include "logging.h"

#include <base/platform/config.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

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

auto get_mutable_logging_container() -> std::shared_ptr<spdlog::sinks::dist_sink_mt>
{
    static auto sink = std::make_shared<spdlog::sinks::dist_sink_mt>();
    return sink;
}

logging::logging()
    : early_lines_(std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(EARLY_LINES_CAPACITY))
{
    spdlog::set_pattern(LOG_PATTERN);

    auto logging_container = get_mutable_logging_container();
    auto console_sink = std::make_shared<spdlog::sinks::platform_sink_mt>();

    logging_container->add_sink(console_sink);
    logging_container->add_sink(early_lines_);
    auto logger = std::make_shared<spdlog::logger>(APPLOG, logging_container);
    logger->flush_on(spdlog::level::err);
    spdlog::initialize_logger(logger);
    spdlog::set_level(spdlog::level::trace);
}

auto logging::open_file(const std::string& output_file) -> bool
{
    std::shared_ptr<spdlog::sinks::basic_file_sink_mt> file_sink;
    try
    {
        file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(output_file, true);
    }
    catch(const spdlog::spdlog_ex&)
    {
        return false;
    }
    file_sink->set_pattern(LOG_PATTERN);
    auto logging_container = get_mutable_logging_container();
    if(early_lines_)
    {
        for(const auto& line : early_lines_->last_raw())
        {
            file_sink->log(line);
        }
        file_sink->flush();
    }
    // Added before the previous sink goes, so that no line misses both.
    logging_container->add_sink(file_sink);
    if(early_lines_)
    {
        logging_container->remove_sink(early_lines_);
        early_lines_.reset();
    }
    if(file_)
    {
        logging_container->remove_sink(file_);
    }
    file_ = std::move(file_sink);
    return true;
}

logging::~logging()
{
    spdlog::shutdown();
}
} // namespace unravel
