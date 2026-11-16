#include "runner.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

namespace unravel::runner
{
namespace
{
auto is_problem(const suite_result& result) -> bool
{
    return result.result == outcome::failed || result.result == outcome::timed_out || result.result == outcome::crashed;
}

/// The time a report shows for a suite: the worker's, start-up included, when it ran in one.
auto get_shown_seconds(const suite_result& result) -> double
{
    return result.wall_seconds > 0.0 ? result.wall_seconds : result.seconds;
}

/// Escapes text for an XML attribute or element; drops control characters XML 1.0 forbids.
auto escape_xml(const std::string& text) -> std::string
{
    std::string escaped;
    escaped.reserve(text.size());
    for(const char c : text)
    {
        switch(c)
        {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            case '\'':
                escaped += "&apos;";
                break;
            default:
                if(static_cast<unsigned char>(c) >= 0x20 || c == '\n' || c == '\t')
                {
                    escaped += c;
                }
                break;
        }
    }
    return escaped;
}

auto join_lines(const std::vector<std::string>& lines) -> std::string
{
    std::string joined;
    for(const auto& line : lines)
    {
        joined += line;
        joined += '\n';
    }
    return joined;
}
} // namespace

auto get_results_folder() -> fs::path
{
    return fs::resolve_protocol("binary:/test_results");
}

void print_report(const std::vector<suite_result>& results, double wall_seconds, int jobs)
{
    auto sorted = results;
    std::stable_sort(sorted.begin(),
                     sorted.end(),
                     [](const suite_result& lhs, const suite_result& rhs)
                     {
                         return get_shown_seconds(lhs) > get_shown_seconds(rhs);
                     });
    std::printf("\nSuites, the slowest first:\n");
    std::printf("  %-7s %9s %7s %5s %5s %5s %5s  %s\n", "result", "seconds", "checks", "fail", "xfail", "xpass", "skip", "suite");
    int counts[5]{};
    long long checks = 0;
    long long failures = 0;
    long long xfails = 0;
    long long xpasses = 0;
    long long skips = 0;
    double suite_seconds = 0.0;
    for(const auto& result : sorted)
    {
        std::printf("  %-7s %9.1f %7d %5d %5d %5d %5d  %s\n",
                    get_outcome_name(result.result),
                    get_shown_seconds(result),
                    result.checks,
                    result.failures,
                    result.xfails,
                    result.xpasses,
                    result.skips,
                    result.name.c_str());
        ++counts[static_cast<int>(result.result)];
        checks += result.checks;
        failures += result.failures;
        xfails += result.xfails;
        xpasses += result.xpasses;
        skips += result.skips;
        suite_seconds += get_shown_seconds(result);
    }
    std::printf("\n%zu suites: %d passed, %d failed, %d skipped, %d timed out, %d crashed.\n",
                results.size(),
                counts[static_cast<int>(outcome::passed)],
                counts[static_cast<int>(outcome::failed)],
                counts[static_cast<int>(outcome::skipped)],
                counts[static_cast<int>(outcome::timed_out)],
                counts[static_cast<int>(outcome::crashed)]);
    std::printf("%lld checks, %lld failures, %lld expected failures, %lld unexpected passes, %lld skipped parts.\n",
                checks,
                failures,
                xfails,
                xpasses,
                skips);
    std::printf("%.1f s on the clock with %d job(s); the suites' times, a worker's start-up included, add up to %.1f s.\n",
                wall_seconds,
                jobs,
                suite_seconds);

    std::set<std::string> passes;
    std::set<std::string> skipped;
    for(const auto& result : results)
    {
        for(const auto& note : result.notes)
        {
            (note.rfind("XPASS", 0) == 0 ? passes : skipped).insert(result.name + ": " + note);
        }
    }
    if(!passes.empty())
    {
        std::printf("\nExpected failures that pass now; make them checks:\n");
        for(const auto& note : passes)
        {
            std::printf("  %s\n", note.c_str());
        }
    }
    if(!skipped.empty())
    {
        std::printf("\nSkipped:\n");
        for(const auto& note : skipped)
        {
            std::printf("  %s\n", note.c_str());
        }
    }
    bool has_problem = false;
    for(const auto& result : results)
    {
        if(!is_problem(result))
        {
            continue;
        }
        if(!has_problem)
        {
            std::printf("\nNot passing:\n");
            has_problem = true;
        }
        std::printf("  %-7s %s%s%s\n",
                    get_outcome_name(result.result),
                    result.name.c_str(),
                    result.reason.empty() ? "" : ": ",
                    result.reason.c_str());
        for(const auto& test_case : result.cases)
        {
            if(test_case.result == outcome::failed)
            {
                std::printf("          case %s: %d of %d checks failed\n",
                            test_case.name.c_str(),
                            test_case.failures,
                            test_case.checks);
            }
            else if(test_case.result == outcome::timed_out || test_case.result == outcome::crashed)
            {
                std::printf("          case %s: %s\n", test_case.name.c_str(), get_outcome_name(test_case.result));
            }
        }
    }
}

auto read_durations(const fs::path& file) -> std::map<std::string, double>
{
    std::map<std::string, double> durations;
    std::ifstream in(file);
    std::string line;
    while(std::getline(in, line))
    {
        const auto tab = line.find('\t');
        if(tab == std::string::npos)
        {
            continue;
        }
        try
        {
            durations[line.substr(tab + 1)] = std::stod(line.substr(0, tab));
        }
        catch(...)
        {
        }
    }
    return durations;
}

void write_durations(const fs::path& file, std::map<std::string, double> durations, const std::vector<suite_result>& results)
{
    for(const auto& result : results)
    {
        if(result.result == outcome::passed || result.result == outcome::failed)
        {
            durations[result.name] = get_shown_seconds(result);
        }
    }
    fs::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::trunc);
    for(const auto& [name, seconds] : durations)
    {
        char value[32];
        std::snprintf(value, sizeof(value), "%.3f", seconds);
        out << value << '\t' << name << '\n';
    }
}

auto write_junit(const fs::path& file, const std::vector<suite_result>& results) -> bool
{
    std::ostringstream xml;
    int tests = 0;
    int failures = 0;
    int skipped = 0;
    double seconds = 0.0;
    std::ostringstream suites;
    for(const auto& result : results)
    {
        int suite_tests = 0;
        int suite_failures = 0;
        int suite_skipped = 0;
        std::ostringstream cases;
        for(const auto& test_case : result.cases)
        {
            ++suite_tests;
            cases << "    <testcase classname=\"" << escape_xml(result.name) << "\" name=\"" << escape_xml(test_case.name)
                  << "\" time=\"" << test_case.seconds << "\">\n";
            if(test_case.result == outcome::skipped)
            {
                ++suite_skipped;
                cases << "      <skipped message=\"" << escape_xml(test_case.skip_reason) << "\"/>\n";
            }
            else if(test_case.result != outcome::passed)
            {
                ++suite_failures;
                const auto first = test_case.failure_messages.empty() ? std::string(get_outcome_name(test_case.result))
                                                                      : test_case.failure_messages.front();
                cases << "      <failure message=\"" << escape_xml(first) << "\">"
                      << escape_xml(join_lines(test_case.failure_messages)) << "</failure>\n";
                cases << "      <system-out>" << escape_xml(test_case.output) << "</system-out>\n";
            }
            cases << "    </testcase>\n";
        }
        // What a case cannot hold: checks outside the cases, a timeout or crash, a --strict failure.
        const bool has_suite_problem =
            is_problem(result) && (suite_failures == 0 || !result.failure_messages.empty() || !result.reason.empty());
        if(has_suite_problem)
        {
            ++suite_tests;
            ++suite_failures;
            auto messages = result.failure_messages;
            if(!result.reason.empty())
            {
                messages.insert(messages.begin(), result.reason);
            }
            const auto first = messages.empty() ? std::string(get_outcome_name(result.result)) : messages.front();
            cases << "    <testcase classname=\"" << escape_xml(result.name) << "\" name=\"(suite)\" time=\"0\">\n"
                  << "      <failure message=\"" << escape_xml(first) << "\">" << escape_xml(join_lines(messages))
                  << "</failure>\n"
                  << "      <system-out>" << escape_xml(result.output) << "</system-out>\n"
                  << "    </testcase>\n";
        }
        suites << "  <testsuite name=\"" << escape_xml(result.name) << "\" tests=\"" << suite_tests << "\" failures=\""
               << suite_failures << "\" skipped=\"" << suite_skipped << "\" time=\"" << get_shown_seconds(result) << "\">\n"
               << cases.str() << "  </testsuite>\n";
        tests += suite_tests;
        failures += suite_failures;
        skipped += suite_skipped;
        seconds += get_shown_seconds(result);
    }
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<testsuites name=\"unravel-tests\" tests=\"" << tests << "\" failures=\"" << failures << "\" skipped=\""
        << skipped << "\" time=\"" << seconds << "\">\n"
        << suites.str() << "</testsuites>\n";
    fs::error_code ec;
    if(file.has_parent_path())
    {
        fs::create_directories(file.parent_path(), ec);
    }
    std::ofstream out(file, std::ios::trunc | std::ios::binary);
    out << xml.str();
    return static_cast<bool>(out);
}

} // namespace unravel::runner
