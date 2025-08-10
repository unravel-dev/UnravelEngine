#pragma once

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/sinks/dist_sink.h>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/chrono.h>
#include <spdlog/fmt/ranges.h>

#include <hpp/source_location.hpp>
namespace unravel
{
using namespace spdlog;


#define APPLOG               "Log"
#define APPLOG_DEBUG(...)    SPDLOG_LOGGER_CALL(spdlog::get(APPLOG), spdlog::level::debug, __VA_ARGS__)
#define APPLOG_TRACE(...)    SPDLOG_LOGGER_CALL(spdlog::get(APPLOG), spdlog::level::trace, __VA_ARGS__)
#define APPLOG_INFO(...)     SPDLOG_LOGGER_CALL(spdlog::get(APPLOG), spdlog::level::info, __VA_ARGS__)
#define APPLOG_WARNING(...)  SPDLOG_LOGGER_CALL(spdlog::get(APPLOG), spdlog::level::warn, __VA_ARGS__)
#define APPLOG_ERROR(...)    SPDLOG_LOGGER_CALL(spdlog::get(APPLOG), spdlog::level::err, __VA_ARGS__)
#define APPLOG_CRITICAL(...) SPDLOG_LOGGER_CALL(spdlog::get(APPLOG), spdlog::level::critical, __VA_ARGS__)


#ifndef SPDLOG_NO_SOURCE_LOC
#define SPDLOG_LOGGER_CALL_LOC(logger, file, line, func, level, ...)                                                   \
    (logger)->log(spdlog::source_loc{file, line, func}, level, __VA_ARGS__)
#else
#define SPDLOG_LOGGER_CALL_LOC(logger, level, ...) (logger)->log(spdlog::source_loc{}, level, __VA_ARGS__)
#endif
#define APPLOG_DEBUG_LOC(FILE_LOC, LINE_LOC, FUNC_LOC, ...)                                                            \
SPDLOG_LOGGER_CALL_LOC(spdlog::get(APPLOG), FILE_LOC, LINE_LOC, FUNC_LOC, spdlog::level::debug, __VA_ARGS__)
#define APPLOG_TRACE_LOC(FILE_LOC, LINE_LOC, FUNC_LOC, ...)                                                            \
    SPDLOG_LOGGER_CALL_LOC(spdlog::get(APPLOG), FILE_LOC, LINE_LOC, FUNC_LOC, spdlog::level::trace, __VA_ARGS__)
#define APPLOG_INFO_LOC(FILE_LOC, LINE_LOC, FUNC_LOC, ...)                                                             \
    SPDLOG_LOGGER_CALL_LOC(spdlog::get(APPLOG), FILE_LOC, LINE_LOC, FUNC_LOC, spdlog::level::info, __VA_ARGS__)
#define APPLOG_WARNING_LOC(FILE_LOC, LINE_LOC, FUNC_LOC, ...)                                                          \
    SPDLOG_LOGGER_CALL_LOC(spdlog::get(APPLOG), FILE_LOC, LINE_LOC, FUNC_LOC, spdlog::level::warn, __VA_ARGS__)
#define APPLOG_ERROR_LOC(FILE_LOC, LINE_LOC, FUNC_LOC, ...)                                                            \
    SPDLOG_LOGGER_CALL_LOC(spdlog::get(APPLOG), FILE_LOC, LINE_LOC, FUNC_LOC, spdlog::level::err, __VA_ARGS__)
#define APPLOG_CRITICAL_LOC(FILE_LOC, LINE_LOC, FUNC_LOC, ...)                                                         \
    SPDLOG_LOGGER_CALL_LOC(spdlog::get(APPLOG), FILE_LOC, LINE_LOC, FUNC_LOC, spdlog::level::critical, __VA_ARGS__)

auto get_mutable_logging_container() -> std::shared_ptr<spdlog::sinks::dist_sink_mt>;

struct logging
{
    logging(const std::string& output_file = "Log.txt");
    ~logging();
};


template<spdlog::level::level_enum lvl = spdlog::level::debug, typename T = std::chrono::microseconds>
struct log_stopwatch
{
    using clock_t = std::chrono::high_resolution_clock;
    using timepoint_t = clock_t::time_point;

    timepoint_t start = clock_t::now();
    const char* func_{};
    hpp::source_location location_;

    log_stopwatch(const char* func, hpp::source_location loc = hpp::source_location::current())
        : func_(func)
        , location_(loc)
    {

    }

    ~log_stopwatch()
    {
        auto end = clock_t::now();
        auto dur = std::chrono::duration_cast<T>(end - start);

        SPDLOG_LOGGER_CALL_LOC(spdlog::get(APPLOG), location_.file_name(), int(location_.line()), func_, lvl, "{} : {}", func_, dur);
    }

};

// Helper macros to concatenate tokens
#define APPLOG_CONCATENATE_DETAIL(x, y) x##y
#define APPLOG_CONCATENATE(x, y) APPLOG_CONCATENATE_DETAIL(x, y)

// Macro to create a unique variable name
#define APPLOG_UNIQUE_VAR(prefix) APPLOG_CONCATENATE(prefix, __LINE__)

#define APPLOG_INFO_PERF(T) log_stopwatch<spdlog::level::info, T> APPLOG_UNIQUE_VAR(_test)(__func__);
#define APPLOG_TRACE_PERF(T) log_stopwatch<spdlog::level::trace, T> APPLOG_UNIQUE_VAR(_test)(__func__);
#define APPLOG_DEBUG_PERF(T) log_stopwatch<spdlog::level::debug, T> APPLOG_UNIQUE_VAR(_test)(__func__);


#define APPLOG_INFO_PERF_NAMED(T, name) log_stopwatch<spdlog::level::info, T> APPLOG_UNIQUE_VAR(_test)(name);
#define APPLOG_TRACE_PERF_NAMED(T, name) log_stopwatch<spdlog::level::trace, T> APPLOG_UNIQUE_VAR(_test)(name);
#define APPLOG_DEBUG_PERF_NAMED(T, name) log_stopwatch<spdlog::level::trace, T> APPLOG_UNIQUE_VAR(_test)(name);


} // namespace unravel
