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

auto get_mutable_logging_container() -> std::shared_ptr<spdlog::sinks::dist_sink_mt>
{
    static auto sink = std::make_shared<spdlog::sinks::dist_sink_mt>();
    return sink;
}

logging::logging(const std::string& output_file)
{
    auto logging_container = get_mutable_logging_container();
    auto console_sink = std::make_shared<spdlog::sinks::platform_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(output_file, true);

    logging_container->add_sink(console_sink);
    logging_container->add_sink(file_sink);
    auto logger = std::make_shared<spdlog::logger>(APPLOG, logging_container);
    logger->flush_on(spdlog::level::err);
    spdlog::initialize_logger(logger);
    spdlog::set_level(spdlog::level::trace);

}

logging::~logging()
{
    spdlog::shutdown();
}
} // namespace unravel
