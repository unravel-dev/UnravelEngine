/*
 * The compile gate (editor/core/asset_compiler/compile_gate.h), which keeps the compiles of one compiled file from
 * running together:
 *   - a request compiles, and so does the next once the first is done;
 *   - a request that arrives while the file compiles starts nothing, and the running compile compiles once more
 *     for all of them, so the file ends up with the settings of the last request and not with those the first
 *     compile started with;
 *   - other files compile meanwhile;
 *   - a compile that ends by an exception lets its file go.
 */

#include "../tests.h"

#include <asset_compiler/compile_gate.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

using namespace unravel::test;
using namespace std::chrono_literals;

namespace
{
using unravel::asset_compiler::compile_gate;

void test_requests_one_after_another()
{
    compile_gate gate;
    int runs = 0;
    const auto compile = [&runs]
    {
        ++runs;
    };
    check(gate.run("wall.png.asset", compile) && runs == 1, "a request compiles the file");
    check(gate.run("wall.png.asset", compile) && runs == 2, "and the next request compiles it again once the first is done");
}

void test_request_during_a_compile()
{
    // The settings a compile reads when it starts, and what the last compile to finish wrote from them.
    std::atomic<int> settings{1};
    std::atomic<int> compiled{0};
    std::atomic<int> runs{0};
    std::atomic<bool> is_first_running{false};
    std::atomic<bool> may_first_finish{false};
    compile_gate gate;
    const auto compile = [&]
    {
        const int read = settings.load();
        if(++runs == 1)
        {
            is_first_running = true;
            while(!may_first_finish.load())
            {
                std::this_thread::sleep_for(1ms);
            }
        }
        compiled = read;
    };

    std::thread first(
        [&]
        {
            gate.run("map.png.asset", compile);
        });
    require(wait_until(
                [&]
                {
                    return is_first_running.load();
                },
                5s,
                "the first compile starts"),
            "the first compile runs");

    // The settings change while the first compile runs, and each change asks for a compile.
    settings = 2;
    const bool did_second_compile = gate.run("map.png.asset", compile);
    settings = 3;
    const bool did_third_compile = gate.run("map.png.asset", compile);
    check(!did_second_compile && !did_third_compile && runs.load() == 1, "a request during a compile starts no compile of its own");

    int other_runs = 0;
    check(gate.run("other.png.asset",
                   [&other_runs]
                   {
                       ++other_runs;
                   }) &&
              other_runs == 1,
          "another file compiles meanwhile");

    may_first_finish = true;
    first.join();
    check_eq(std::size_t(runs.load()), 2, "the running compile compiles once more for the requests that came meanwhile");
    check_eq(std::size_t(compiled.load()), 3, "so the file holds the last settings, not those the first compile started with");
    check(gate.run("map.png.asset", compile) && runs.load() == 3, "and the file is free for the next request");
}

void test_compile_that_throws()
{
    compile_gate gate;
    bool is_thrown = false;
    try
    {
        gate.run("broken.png.asset",
                 []
                 {
                     throw std::runtime_error("the compile failed");
                 });
    }
    catch(const std::runtime_error&)
    {
        is_thrown = true;
    }
    int runs = 0;
    check(is_thrown && gate.run("broken.png.asset",
                                [&runs]
                                {
                                    ++runs;
                                }) &&
              runs == 1,
          "a compile that ends by an exception lets its file go, and the next request compiles it");
}

void run_compile_gate_suite(rtti::context&)
{
    RUN_CASE(test_requests_one_after_another());
    RUN_CASE(test_request_during_a_compile());
    RUN_CASE(test_compile_that_throws());
}

} // namespace

REGISTER_TEST_SUITE("compile gate", run_compile_gate_suite)
