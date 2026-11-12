/*
 * Validation suite for the game's run-time permission requests (engine/permissions/permission_requests).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "permission requests"
 *
 * Scripts ask for a permission and read the answer by the request's id, frames later; the system answers on a
 * thread of its own. This pins:
 *   - a permission is asked for by its manifest name: a name without a dot gets "android.permission." in front;
 *   - a request is pending until it is answered, then granted or denied, each request on its own;
 *   - an answer given inside the request, and one given on another thread, both arrive;
 *   - a request the platform could not make is denied at once;
 *   - a forgotten request reads as denied, and its late answer is dropped;
 *   - an answer that arrives after the requests are gone does no harm;
 *   - the operating system's requests are granted at once where it has no run-time permissions.
 */

#include "../tests.h"

#include <engine/permissions/permission_requests.h>
#include <ospp/permission.h>

#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace unravel;

namespace
{

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAILED: %s\n", what.c_str());
    }
}

/// A platform that keeps every request until the test answers it.
struct held_platform
{
    auto make_asker() -> permission_requests::asker
    {
        return [this](const std::string& name, permission_requests::answer_callback on_answer)
        {
            names.push_back(name);
            answers.push_back(std::move(on_answer));
            return true;
        };
    }

    std::vector<std::string> names;
    std::vector<permission_requests::answer_callback> answers;
};

void test_names()
{
    std::printf("test_names\n");
    check(get_android_permission_name("CAMERA") == "android.permission.CAMERA", "a short name gets Android's prefix");
    check(get_android_permission_name("com.vendor.permission.READ") == "com.vendor.permission.READ",
          "a name with a dot is taken as written");
}

void test_answers()
{
    std::printf("test_answers\n");
    held_platform platform;
    permission_requests requests(platform.make_asker());
    const auto camera = requests.request("CAMERA");
    const auto microphone = requests.request("android.permission.RECORD_AUDIO");
    check(camera != 0 && microphone != 0 && camera != microphone, "every request has an id of its own, never 0");
    check(platform.names == std::vector<std::string>{"android.permission.CAMERA", "android.permission.RECORD_AUDIO"},
          "the platform is asked by manifest name");
    check(requests.get_state(camera) == permission_state::pending &&
              requests.get_state(microphone) == permission_state::pending,
          "a request is pending until it is answered");
    platform.answers[1](false);
    check(requests.get_state(camera) == permission_state::pending &&
              requests.get_state(microphone) == permission_state::denied,
          "an answer reaches its own request only");
    std::thread system_thread(
        [&platform]()
        {
            platform.answers[0](true);
        });
    system_thread.join();
    check(requests.get_state(camera) == permission_state::granted, "an answer given on another thread arrives");
}

void test_answer_inside_the_request()
{
    std::printf("test_answer_inside_the_request\n");
    permission_requests granting(
        [](const std::string&, permission_requests::answer_callback on_answer)
        {
            on_answer(true);
            return true;
        });
    check(granting.get_state(granting.request("CAMERA")) == permission_state::granted,
          "an answer given inside the request arrives");
    permission_requests failing(
        [](const std::string&, permission_requests::answer_callback)
        {
            return false;
        });
    check(failing.get_state(failing.request("CAMERA")) == permission_state::denied,
          "a request that could not be made is denied");
}

void test_forgotten_requests()
{
    std::printf("test_forgotten_requests\n");
    held_platform platform;
    permission_requests::answer_callback late_answer;
    {
        permission_requests requests(platform.make_asker());
        const auto forgotten = requests.request("CAMERA");
        const auto kept = requests.request("RECORD_AUDIO");
        requests.forget(forgotten);
        platform.answers[0](true);
        check(requests.get_state(forgotten) == permission_state::denied,
              "a forgotten request reads as denied, whatever arrives");
        check(requests.get_state(kept) == permission_state::pending, "and the others stand as they were");
        check(requests.get_state(12345) == permission_state::denied, "an id no request has reads as denied");
        late_answer = platform.answers[1];
    }
    late_answer(true);
    check(true, "an answer that arrives after the requests are gone does no harm");
}

void test_operating_system()
{
    std::printf("test_operating_system\n");
    if(os::permission::is_requested_at_run_time())
    {
        return;
    }
    permission_requests requests;
    check(requests.get_state(requests.request("CAMERA")) == permission_state::granted,
          "a system without run-time permissions grants a request at once");
}

auto run_permission_requests_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_names();
    test_answers();
    test_answer_inside_the_request();
    test_forgotten_requests();
    test_operating_system();
    std::printf("permission requests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("permission requests", run_permission_requests_suite)
