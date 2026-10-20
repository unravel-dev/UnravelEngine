#include "deploy_android.h"
#include "deploy_preflight.h"

#include <deploy/files.h>
#include <deploy/folders.h>
#include <deploy/run_arguments.h>
#include <editor/system/project_manager.h>
#include <engine/threading/threader.h>
#include <logging/logging.h>
#include <subprocess/subprocess.hpp>
#include <hpp/finally.hpp>
#include <version/version.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>

// The editor's configuration, which the Android player beside it must share (editor/editor/CMakeLists.txt).
#ifndef UNRAVEL_BUILD_CONFIGURATION
#define UNRAVEL_BUILD_CONFIGURATION ""
#endif

namespace unravel::deployment
{
namespace
{
constexpr const char* TEMPLATE_FOLDER = "binary:/players/";
/// Where Gradle builds, per project, under the system's temporary folder: kept between deploys so Gradle
/// rebuilds only what changed, and outside the project so its thousands of files stay out of the editor's
/// watchers.
constexpr const char* GRADLE_STAGING_FOLDER = "unravel-android";
constexpr const char* KEYSTORE_PASSWORD_VARIABLE = "UNRAVEL_KEYSTORE_PASSWORD";
constexpr const char* DEFAULT_VERSION_NAME = "1.0";
constexpr const char* SESSION_THREAD_NAME = "Android Game Session";
/// Gradle output lines kept in a failed step's message.
constexpr std::size_t GRADLE_ERROR_LINES = 25;
constexpr auto PROCESS_POLL_INTERVAL = std::chrono::milliseconds(1000);
constexpr auto PROCESS_START_TIMEOUT = std::chrono::seconds(15);
/// How long the log keeps reading after the game ended, for the crash report that follows it.
constexpr auto CRASH_REPORT_GRACE = std::chrono::milliseconds(2000);
/// The tag of the system's crash reports (tombstones) in the device log.
constexpr const char* CRASH_REPORT_TAG = "DEBUG";
// Deploy and Run's steps on the device, as the Deploy window lists them.
constexpr const char* STEP_INSTALL = "Install on Device";
constexpr const char* STEP_START = "Start on Device";

auto get_project_name(rtti::context& ctx) -> std::string
{
    return ctx.get_cached<project_manager>().get_name();
}

/// The open project's Android build folder: the Gradle project and the data it packages.
auto get_build_dir() -> fs::path
{
    const auto key = std::hash<std::string>{}(get_project_root().generic_string());
    return fs::temp_directory_path() / GRADLE_STAGING_FOLDER / fmt::format("{:016x}", key);
}

/// The folder Gradle builds this project's packages in.
auto get_gradle_project_dir() -> fs::path
{
    return get_build_dir() / "project";
}

/// The data/ an Android deploy wrote beside its package before the data moved to the build folder; only
/// where the folder holds packages and that data, nothing else.
void remove_earlier_data(const fs::path& output_folder)
{
    fs::error_code ec;
    const auto data = output_folder / DATA_FOLDER;
    if(!fs::exists(data, ec) || !is_android_package_folder(output_folder))
    {
        return;
    }
    fs::remove_all(data, ec);
    if(!ec)
    {
        APPLOG_INFO("Deploy: removed {}, which earlier deploys left beside the package.", data.generic_string());
    }
}

/// Copies the template's Gradle project over the build folder's copy: files that changed are replaced,
/// and Gradle's own files in it (.gradle, build outputs) stay for the next build.
void update_gradle_project(const fs::path& source, const fs::path& destination)
{
    fs::error_code ec;
    fs::create_directories(destination, ec);
    fs::copy(source, destination, fs::copy_options::recursive | fs::copy_options::update_existing, ec);
    if(ec)
    {
        throw std::runtime_error(fmt::format("Copying the Android project to {} failed: {}.", destination.generic_string(), ec.message()));
    }
}

void write_text_or_throw(const fs::path& file, const std::string& text)
{
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    stream << text;
    if(!stream.good())
    {
        throw std::runtime_error(fmt::format("Writing {} failed.", file.generic_string()));
    }
}

/// The last `count` lines of `output`.
auto get_last_lines(const std::string& output, std::size_t count) -> std::string
{
    std::size_t begin = output.size();
    for(std::size_t found = 0; begin > 0 && found <= count; --begin)
    {
        if(output[begin - 1] == '\n')
        {
            ++found;
        }
    }
    return output.substr(begin);
}

auto trim(std::string text) -> std::string
{
    const auto begin = text.find_first_not_of(" \t\r\n");
    if(begin == std::string::npos)
    {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

/// adb with the device chosen.
auto run_adb(const android_toolchain& tools, const std::string& device, std::vector<std::string> arguments)
    -> subprocess::call_result
{
    arguments.insert(arguments.begin(), {tools.adb.string(), "-s", device});
    return subprocess::call(arguments);
}

auto get_game_process(const android_toolchain& tools, const std::string& device, const std::string& application_id)
    -> std::string
{
    return trim(run_adb(tools, device, {"shell", "pidof", application_id}).out_output);
}

/// The engine's own log lines carry their time and level again: "[2026-10-18 20:44:29.426] [info] text".
auto strip_engine_prefix(const std::string& message) -> std::string
{
    if(message.size() < 2 || message[0] != '[' || message[1] < '0' || message[1] > '9')
    {
        return message;
    }
    const auto time_end = message.find("] ");
    return time_end == std::string::npos ? message : message.substr(time_end + 2);
}

void log_device_line(const logcat_line& line)
{
    const auto message = strip_engine_prefix(line.message);
    switch(line.level)
    {
        case 'V':
        case 'D':
            APPLOG_TRACE("[Device] {}", message);
            break;
        case 'W':
            APPLOG_WARNING("[Device] {}", message);
            break;
        case 'E':
        case 'F':
            APPLOG_ERROR("[Device] {}", message);
            break;
        default:
            APPLOG_INFO("[Device] {}", message);
            break;
    }
}

/// ndk-stack of an NDK beside the SDK, for symbolizing a crash on the machine that built the player.
auto find_ndk_stack(const fs::path& sdk) -> fs::path
{
#if defined(_WIN32)
    const char* name = "ndk-stack.cmd";
#else
    const char* name = "ndk-stack";
#endif
    fs::error_code ec;
    for(const auto& entry : fs::directory_iterator(sdk / "ndk", ec))
    {
        if(fs::exists(entry.path() / name, ec))
        {
            return entry.path() / name;
        }
    }
    return {};
}

/// Logs the crash report again with source lines, when the unstripped player and ndk-stack are here.
void symbolize_crash(const android_package_plan& plan, const std::vector<std::string>& report)
{
    fs::error_code ec;
    const auto& symbols = plan.package.player.symbols_dir;
    const auto ndk_stack = find_ndk_stack(plan.tools.sdk);
    if(symbols.empty() || !fs::exists(symbols / "libmain.so", ec) || ndk_stack.empty())
    {
        APPLOG_INFO("Deploy: the crash report above has no source lines. They need the unstripped player and the "
                    "Android NDK, which only an editor built from source has.");
        return;
    }
    const auto report_file = fs::temp_directory_path() / "unravel-android-crash.txt";
    {
        std::ofstream stream(report_file, std::ios::binary | std::ios::trunc);
        for(const auto& line : report)
        {
            stream << line << '\n';
        }
    }
    std::vector<std::string> arguments{ndk_stack.string(), "-sym", symbols.string(), "-i", report_file.string()};
#if defined(_WIN32)
    arguments.insert(arguments.begin(), {"cmd", "/c"});
#endif
    const auto result = subprocess::call(arguments);
    APPLOG_ERROR("Deploy: the crash with source lines:\n{}", result.out_output);
}

/// Deploy and Run on a device: install, start, then the game's log until it ends or is stopped.
class game_session
{
public:
    ~game_session()
    {
        end();
    }

    void start(const android_package_plan& plan)
    {
        end();
        clear_conflict();
        leave_requested_ = false;
        stop_requested_ = false;
        running_ = true;
        worker_ = std::thread(
            [this, plan]()
            {
                // The session completes the deploy's device steps, whose futures need a registered thread.
                tpp::this_thread::register_this_thread(SESSION_THREAD_NAME, true);
                run(plan);
                running_ = false;
                tpp::this_thread::unregister_this_thread();
            });
    }

    /// Asks the session to end the game on the device; returns at once.
    void stop()
    {
        stop_requested_ = true;
        leave_requested_ = true;
    }

    /// Stops following the game and waits for the session's thread; the game keeps running on the device.
    void end()
    {
        leave_requested_ = true;
        if(worker_.joinable())
        {
            worker_.join();
        }
    }

    auto is_running() const -> bool
    {
        return running_;
    }

    auto get_conflict_device() -> std::string
    {
        std::lock_guard<std::mutex> lock(conflict_mutex_);
        return conflict_ ? conflict_->device : std::string{};
    }

    auto get_conflict_count() const -> std::uint32_t
    {
        return conflict_count_;
    }

    /// Runs the build a conflict stopped again, uninstalling the game first; its device steps, or none
    /// when there is no conflict.
    auto replace_conflicting() -> deploy_jobs
    {
        std::optional<android_package_plan> plan;
        {
            std::lock_guard<std::mutex> lock(conflict_mutex_);
            plan.swap(conflict_);
        }
        if(!plan)
        {
            return {};
        }
        plan->replace_installed = true;
        plan->uninstall_first = true;
        plan->install_step = std::make_shared<device_step>();
        plan->start_step = std::make_shared<device_step>();
        start(*plan);
        return get_device_jobs(*plan);
    }

private:
    void run(const android_package_plan& plan)
    {
        // Every way out finishes the device steps: a deploy waits on them.
        auto stopped = hpp::finally(
            [&plan]()
            {
                fail_device_steps(plan, "Stopped.");
            });
        const auto& tools = plan.tools;
        const auto& device = plan.device;
        const auto& application_id = plan.package.application_id;
        if(plan.uninstall_first)
        {
            uninstall(plan);
        }
        APPLOG_INFO("Deploy: installing {} on {}.", plan.output.filename().generic_string(), device);
        auto install = install_package(plan);
        if(is_signature_conflict(install.out_output) && plan.replace_installed)
        {
            uninstall(plan);
            install = install_package(plan);
        }
        if(is_signature_conflict(install.out_output))
        {
            fail_steps(plan,
                       "The game on the device was signed with another key (debug and release builds differ). "
                       "Uninstall and Install replaces it.");
            record_conflict(plan);
            return;
        }
        if(install.retcode != 0 || install.out_output.find("Success") == std::string::npos)
        {
            fail_steps(plan, trim(install.out_output));
            APPLOG_ERROR("Deploy: installing on {} failed: {}", device, trim(install.out_output));
            return;
        }
        complete_step(plan.install_step);
        run_adb(tools, device, {"shell", "am", "force-stop", application_id});
        run_adb(tools, device, {"logcat", "-c"});
        const auto started = subprocess::call(make_start_arguments(tools, device, application_id, plan.run_arguments));
        const auto process = wait_for_process(tools, device, application_id);
        if(process.empty())
        {
            fail_device_steps(plan, "The game did not start on the device.");
            APPLOG_ERROR("Deploy: the game did not start on {}. {}", device, trim(started.out_output));
            return;
        }
        complete_step(plan.start_step);
        APPLOG_INFO("Deploy: the game runs on {} (process {}). Its log follows.", device, process);
        follow_log(plan, process);
    }

    static void complete_step(const std::shared_ptr<device_step>& step)
    {
        if(step)
        {
            step->complete();
        }
    }

    /// The install failed with `reason`, so the start is skipped.
    static void fail_steps(const android_package_plan& plan, const std::string& reason)
    {
        if(plan.install_step)
        {
            plan.install_step->fail(reason);
        }
        fail_device_steps(plan, "Skipped: the install failed.");
    }

    static auto install_package(const android_package_plan& plan) -> subprocess::call_result
    {
        return run_adb(plan.tools, plan.device, {"install", "-r", plan.output.string()});
    }

    static void uninstall(const android_package_plan& plan)
    {
        APPLOG_INFO("Deploy: uninstalling the game signed with another key from {}, with its saved data.", plan.device);
        run_adb(plan.tools, plan.device, {"uninstall", plan.package.application_id});
    }

    /// The installed game was signed with another key: debug and release builds differ.
    static auto is_signature_conflict(const std::string& install_output) -> bool
    {
        return install_output.find("INSTALL_FAILED_UPDATE_INCOMPATIBLE") != std::string::npos;
    }

    void record_conflict(const android_package_plan& plan)
    {
        APPLOG_ERROR("Deploy: the game on {} was signed with another key (debug and release builds differ), so this "
                     "build cannot replace it. Uninstall and Install in the Deploy window replaces it, and removes "
                     "its saved data.",
                     plan.device);
        std::lock_guard<std::mutex> lock(conflict_mutex_);
        conflict_ = plan;
        ++conflict_count_;
    }

    void clear_conflict()
    {
        std::lock_guard<std::mutex> lock(conflict_mutex_);
        conflict_.reset();
    }

    auto wait_for_process(const android_toolchain& tools, const std::string& device, const std::string& application_id)
        -> std::string
    {
        const auto deadline = std::chrono::steady_clock::now() + PROCESS_START_TIMEOUT;
        while(!leave_requested_ && std::chrono::steady_clock::now() < deadline)
        {
            auto process = get_game_process(tools, device, application_id);
            if(!process.empty())
            {
                return process;
            }
            std::this_thread::sleep_for(PROCESS_POLL_INTERVAL);
        }
        return {};
    }

    void follow_log(const android_package_plan& plan, const std::string& process)
    {
        const auto& tools = plan.tools;
        std::mutex report_mutex;
        std::vector<std::string> crash_report;
        subprocess::stream log;
        log.start({tools.adb.string(), "-s", plan.device, "logcat", "-v", "threadtime"},
                  [&process, &report_mutex, &crash_report](const std::string& text)
                  {
                      logcat_line line;
                      if(!parse_logcat_line(text, line))
                      {
                          return;
                      }
                      if(line.tag == CRASH_REPORT_TAG)
                      {
                          APPLOG_ERROR("[Device] {}", line.message);
                          std::lock_guard<std::mutex> lock(report_mutex);
                          crash_report.push_back(text);
                          return;
                      }
                      if(line.pid == process)
                      {
                          log_device_line(line);
                      }
                  });
        while(!leave_requested_ && get_game_process(tools, plan.device, plan.package.application_id) == process)
        {
            std::this_thread::sleep_for(PROCESS_POLL_INTERVAL);
        }
        if(stop_requested_)
        {
            run_adb(tools, plan.device, {"shell", "am", "force-stop", plan.package.application_id});
            log.stop();
            APPLOG_INFO("Deploy: stopped the game on {}.", plan.device);
            return;
        }
        if(leave_requested_)
        {
            log.stop();
            return;
        }
        std::this_thread::sleep_for(CRASH_REPORT_GRACE);
        log.stop();
        APPLOG_INFO("Deploy: the game on {} ended.", plan.device);
        std::lock_guard<std::mutex> lock(report_mutex);
        if(!crash_report.empty())
        {
            symbolize_crash(plan, crash_report);
        }
    }

    std::thread worker_;
    /// Stop Game: end the game on the device.
    std::atomic<bool> stop_requested_{false};
    /// Stop following the game: Stop Game, a new Deploy and Run, or the editor closing.
    std::atomic<bool> leave_requested_{false};
    std::atomic<bool> running_{false};
    std::mutex conflict_mutex_;
    /// The build a signature conflict stopped, kept for Uninstall and Install.
    std::optional<android_package_plan> conflict_;
    std::atomic<std::uint32_t> conflict_count_{0};
};

auto get_session() -> game_session&
{
    static game_session session;
    return session;
}

std::mutex tools_mutex;
std::vector<android_device> listed_devices;
android_toolchain found_tools;
bool has_searched_tools{false};
std::atomic<bool> is_listing_devices{false};
std::atomic<bool> has_listed_devices{false};

/// The devices adb lists; wireless debugging devices that paired before are connected first when none is.
auto list_devices(const android_toolchain& tools) -> std::vector<android_device>
{
    if(tools.adb.empty())
    {
        return {};
    }
    const auto adb = tools.adb.string();
    auto devices = parse_android_devices(subprocess::call({adb, "devices", "-l"}).out_output);
    if(!devices.empty())
    {
        return devices;
    }
    std::stringstream services(subprocess::call({adb, "mdns", "services"}).out_output);
    std::string line;
    while(std::getline(services, line))
    {
        std::stringstream words(line);
        std::string name;
        std::string type;
        std::string address;
        if(words >> name >> type >> address && type.find("_adb-tls-connect") != std::string::npos)
        {
            subprocess::call({adb, "connect", address});
        }
    }
    return parse_android_devices(subprocess::call({adb, "devices", "-l"}).out_output);
}
} // namespace

device_step::device_step()
    : job_(promise_.get_future().share())
{
}

device_step::~device_step()
{
    fail("Stopped.");
}

auto device_step::get_job() const -> const tpp::shared_future<void>&
{
    return job_;
}

void device_step::complete()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(!is_finished_)
    {
        is_finished_ = true;
        promise_.set_value();
    }
}

void device_step::fail(const std::string& reason)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(!is_finished_)
    {
        is_finished_ = true;
        promise_.set_exception(std::make_exception_ptr(std::runtime_error(reason)));
    }
}

auto get_device_jobs(const android_package_plan& plan) -> deploy_jobs
{
    deploy_jobs jobs;
    if(plan.install_step)
    {
        jobs.push_back({STEP_INSTALL, plan.install_step->get_job()});
    }
    if(plan.start_step)
    {
        jobs.push_back({STEP_START, plan.start_step->get_job()});
    }
    return jobs;
}

void fail_device_steps(const android_package_plan& plan, const std::string& reason)
{
    for(const auto& step : {plan.install_step, plan.start_step})
    {
        if(step)
        {
            step->fail(reason);
        }
    }
}

auto get_android_template() -> const android_template_lookup&
{
    static const auto lookup = find_android_template(fs::resolve_protocol(std::string(TEMPLATE_FOLDER) + ANDROID_TEMPLATE_NAME),
                                                     version::get_full(),
                                                     UNRAVEL_BUILD_CONFIGURATION);
    return lookup;
}

auto get_android_toolchain() -> android_toolchain
{
    std::lock_guard<std::mutex> lock(tools_mutex);
    if(!has_searched_tools)
    {
        found_tools = find_android_toolchain(get_android_toolchain_search());
        has_searched_tools = true;
    }
    return found_tools;
}

auto get_android_content_dir() -> fs::path
{
    return get_build_dir() / "content";
}

auto get_android_application_id(rtti::context& ctx, const deploy_settings& params) -> std::string
{
    if(!params.android_application_id.empty())
    {
        return params.android_application_id;
    }
    const auto& app = ctx.get_cached<project_manager>().get_settings().app;
    return make_default_application_id(app.company, app.product.empty() ? get_project_name(ctx) : app.product);
}

auto get_android_problems(rtti::context& ctx, const deploy_settings& params) -> std::vector<std::string>
{
    std::vector<std::string> problems;
    const auto& player = get_android_template();
    if(!player.is_found())
    {
        problems.push_back(player.error);
    }
    const auto tools = get_android_toolchain();
    if(!tools.is_complete())
    {
        problems.push_back(tools.error);
    }
    const auto id_problem = check_application_id(get_android_application_id(ctx, params));
    if(!id_problem.empty())
    {
        problems.push_back(id_problem);
    }
    if(params.android_release)
    {
        fs::error_code ec;
        if(params.android_keystore.empty() || !fs::exists(params.android_keystore, ec))
        {
            problems.push_back("A release build needs the keystore that signs it. Choose one, or turn off Release Build.");
        }
        else if(params.android_key_alias.empty())
        {
            problems.push_back("A release build needs the key alias of its keystore.");
        }
        else if(std::getenv(KEYSTORE_PASSWORD_VARIABLE) == nullptr)
        {
            problems.push_back(fmt::format("A release build reads the keystore password from {}. Set it and restart the "
                                           "editor.",
                                           KEYSTORE_PASSWORD_VARIABLE));
        }
    }
    if(params.deploy_and_run)
    {
        const auto devices = get_android_devices();
        const auto device = std::find_if(devices.begin(),
                                         devices.end(),
                                         [&params](const android_device& listed)
                                         {
                                             return listed.serial == params.android_device;
                                         });
        if(params.android_device.empty())
        {
            problems.push_back("Choose the device to run the game on, or turn off Run After Deploy.");
        }
        else if(!has_listed_devices)
        {
            refresh_android_devices(ctx);
            problems.push_back("Looking for the connected Android devices.");
        }
        else if(device == devices.end())
        {
            problems.push_back("The chosen device is not connected. Connect it and refresh the device list.");
        }
        else if(device->state == "unauthorized")
        {
            problems.push_back("The device does not allow this computer yet. Accept the USB debugging prompt on it.");
        }
        else if(!device->is_ready())
        {
            problems.push_back(fmt::format("The device is {}. Reconnect it and refresh the device list.", device->state));
        }
    }
    return problems;
}

auto make_android_package_plan(rtti::context& ctx, const deploy_settings& params) -> android_package_plan
{
    const auto& app = ctx.get_cached<project_manager>().get_settings().app;
    android_package_plan plan;
    plan.tools = get_android_toolchain();
    plan.device = params.android_device;
    plan.run_arguments = split_run_arguments(params.deploy_run_arguments);
    plan.replace_installed = params.android_replace_installed;
    plan.output = get_output_location(params) / (get_project_name(ctx) + ".apk");
    if(params.deploy_and_run)
    {
        plan.install_step = std::make_shared<device_step>();
        plan.start_step = std::make_shared<device_step>();
    }
    auto& package = plan.package;
    package.player = get_android_template().found;
    package.project_dir = get_gradle_project_dir();
    package.content_dir = get_android_content_dir();
    package.application_id = get_android_application_id(ctx, params);
    package.name = app.product.empty() ? get_project_name(ctx) : app.product;
    package.version_name = app.version.empty() ? DEFAULT_VERSION_NAME : app.version;
    package.version_code = std::max<std::uint32_t>(1, params.android_version_code);
    package.orientation = params.android_screen_orientation;
    package.is_release = params.android_release;
    package.keystore = params.android_keystore;
    package.key_alias = params.android_key_alias;
    return plan;
}

void build_android_package(const android_package_plan& plan)
{
    const auto& package = plan.package;
    update_gradle_project(package.player.get_project_dir(), package.project_dir);
    write_text_or_throw(package.project_dir / "local.properties", make_gradle_local_properties(plan.tools.sdk));
    APPLOG_INFO("Deploy: building the Android package with Gradle in {}.", package.project_dir.generic_string());
    const auto result = subprocess::call(make_gradle_arguments(plan.tools, package),
                                         {},
                                         [](const std::string& line)
                                         {
                                             if(!line.empty())
                                             {
                                                 APPLOG_TRACE("[Gradle] {}", line);
                                             }
                                         });
    if(!result.started)
    {
        throw std::runtime_error(fmt::format("Java did not start ({}).", plan.tools.java.generic_string()));
    }
    if(result.retcode != 0)
    {
        throw std::runtime_error(fmt::format("Gradle failed with exit code {}. Its last output:\n{}",
                                             result.retcode,
                                             get_last_lines(result.out_output, GRADLE_ERROR_LINES)));
    }
    const auto built = get_built_package(package.project_dir, package.is_release);
    fs::error_code ec;
    fs::copy_file(built, plan.output, fs::copy_options::overwrite_existing, ec);
    if(ec)
    {
        throw std::runtime_error(fmt::format("Copying {} to {} failed: {}.", built.generic_string(), plan.output.generic_string(), ec.message()));
    }
    APPLOG_INFO("Deploy: built {} ({} MB).", plan.output.generic_string(), fs::file_size(plan.output, ec) / (1024 * 1024));
    remove_earlier_data(plan.output.parent_path());
}

void run_android_game(const android_package_plan& plan)
{
    get_session().start(plan);
}

void stop_android_game()
{
    get_session().stop();
}

auto is_android_game_running() -> bool
{
    return get_session().is_running();
}

void end_android_game_session()
{
    get_session().end();
}

auto get_android_install_conflict() -> std::string
{
    return get_session().get_conflict_device();
}

auto get_android_install_conflict_count() -> std::uint32_t
{
    return get_session().get_conflict_count();
}

auto replace_android_game() -> deploy_jobs
{
    return get_session().replace_conflicting();
}

auto get_android_devices() -> std::vector<android_device>
{
    std::lock_guard<std::mutex> lock(tools_mutex);
    return listed_devices;
}

auto has_listed_android_devices() -> bool
{
    return has_listed_devices;
}

void update_android_devices()
{
    const auto tools = find_android_toolchain(get_android_toolchain_search());
    auto devices = list_devices(tools);
    std::lock_guard<std::mutex> lock(tools_mutex);
    found_tools = tools;
    has_searched_tools = true;
    listed_devices = std::move(devices);
    has_listed_devices = true;
}

void refresh_android_devices(rtti::context& ctx)
{
    if(is_listing_devices.exchange(true))
    {
        return;
    }
    auto& th = ctx.get_cached<threader>();
    th.pool->schedule("Listing Android devices",
                      []()
                      {
                          update_android_devices();
                          is_listing_devices = false;
                      });
}

auto is_refreshing_android_devices() -> bool
{
    return is_listing_devices;
}

} // namespace unravel::deployment
