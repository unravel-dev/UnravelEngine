#include "deploy_android.h"
#include "deploy_preflight.h"

#include <deploy/android_icon.h>
#include <deploy/cancellation.h>
#include <deploy/files.h>
#include <deploy/folders.h>
#include <deploy/run_arguments.h>
#include <editor/system/project_manager.h>
#include <engine/threading/threader.h>
#include <logging/logging.h>
#include <string_utils/utils.h>
#include <subprocess/subprocess.hpp>
#include <hpp/finally.hpp>
#include <version/version.h>

#include <algorithm>
#include <array>
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
/// Where Gradle builds, per project, under the system's temporary folder: kept between deploys so Gradle
/// rebuilds only what changed, and outside the project so its thousands of files stay out of the editor's
/// watchers.
constexpr const char* GRADLE_STAGING_FOLDER = "unravel-android";
/// A build folder no deploy used for this long is deleted; a later deploy of its profile builds it anew.
constexpr int UNUSED_BUILD_DAYS = 14;
constexpr int HOURS_PER_DAY = 24;
constexpr auto UNUSED_BUILD_LIFETIME = std::chrono::hours(HOURS_PER_DAY * UNUSED_BUILD_DAYS);
/// The names Gradle and IDEs give what they make in a project; the template has none of them.
constexpr std::array<const char*, 5> GRADLE_GENERATED_NAMES = {"build", ".gradle", ".kotlin", ".idea", "local.properties"};
constexpr const char* IDE_MODULE_EXTENSION = ".iml";
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
/// The engine's logo, the icon of a game that chooses none.
constexpr const char* DEFAULT_ICON_KEY = "editor:/data/icons/unravel_logo.png";

/// The image the game's icon is made from: the chosen texture's source image, or the engine's logo.
auto get_icon_image(const deploy_settings& params) -> fs::path
{
    return fs::resolve_protocol(params.android.icon ? params.android.icon.id() : std::string(DEFAULT_ICON_KEY));
}

auto get_project_name(rtti::context& ctx) -> std::string
{
    return ctx.get_cached<project_manager>().get_name();
}

/// A profile's Android build folder: the Gradle project and the data it packages. Each profile has its own,
/// so its packs update in place between its deploys.
auto get_build_dir(const std::string& profile) -> fs::path
{
    const auto key = std::hash<std::string>{}(get_project_root().generic_string() + "|" + profile);
    return fs::temp_directory_path() / GRADLE_STAGING_FOLDER / fmt::format("{:016x}", key);
}

/// Removes a build folder with everything in it. Gradle's outputs in it have paths longer than the 260 characters
/// Windows takes by default, where a plain removal stops and leaves the folder half removed.
void remove_build_dir(const fs::path& build_dir, fs::error_code& ec)
{
    fs::remove_all(fs::make_extended_length_path(build_dir), ec);
}

} // namespace

void remove_android_build_dir(const std::string& profile)
{
    fs::error_code ec;
    remove_build_dir(get_build_dir(profile), ec);
}

namespace
{
/// The folder Gradle builds a profile's packages in.
auto get_gradle_project_dir(const std::string& profile) -> fs::path
{
    return get_build_dir(profile) / "project";
}

/// The data/ an Android deploy wrote beside its package before the data moved to the build folder; only
/// where the folder holds packages and that data, nothing else.
void remove_earlier_data(const fs::path& output_folder)
{
    fs::error_code ec;
    const auto data = output_folder / DATA_FOLDER;
    if(!is_present(data) || !is_android_package_folder(output_folder))
    {
        return;
    }
    fs::remove_all(data, ec);
    if(!ec)
    {
        APPLOG_INFO("Deploy: removed {}, which earlier deploys left beside the package.", data.generic_string());
    }
}

auto is_gradle_generated(const fs::path& entry) -> bool
{
    const auto name = entry.filename().generic_string();
    return entry.extension() == IDE_MODULE_EXTENSION ||
           std::find(GRADLE_GENERATED_NAMES.begin(), GRADLE_GENERATED_NAMES.end(), name) != GRADLE_GENERATED_NAMES.end();
}

/// The files of the build folder's Gradle project that the template does not have, outside what Gradle makes there.
auto find_dropped_project_files(const fs::path& source, const fs::path& destination) -> std::vector<fs::path>
{
    std::vector<fs::path> dropped;
    fs::error_code ec;
    for(fs::recursive_directory_iterator it(destination, ec), end; !ec && it != end; it.increment(ec))
    {
        if(is_gradle_generated(it->path()))
        {
            it.disable_recursion_pending();
            continue;
        }
        if(it->is_regular_file(ec) && !is_present(source / it->path().lexically_relative(destination)))
        {
            dropped.push_back(it->path());
        }
    }
    return dropped;
}

/// Makes the build folder's Gradle project the template's: its files are copied where they differ, whichever is the
/// newer, and the files a template no longer has are removed, so a folder that another editor version built in
/// builds this one's project. What Gradle makes in it (.gradle, build outputs) stays for the next build.
void update_gradle_project(const fs::path& source, const fs::path& destination)
{
    fs::error_code ec;
    for(fs::recursive_directory_iterator it(source, ec), end; !ec && it != end; it.increment(ec))
    {
        if(it->is_regular_file(ec))
        {
            copy_file_or_throw(it->path(), destination / it->path().lexically_relative(source));
        }
    }
    if(ec)
    {
        throw std::runtime_error(fmt::format("Listing the Android project {} failed: {}.", source.generic_string(), ec.message()));
    }
    for(const auto& file : find_dropped_project_files(source, destination))
    {
        if(!fs::remove(file, ec))
        {
            throw std::runtime_error(fmt::format("Removing {} failed: {}.", file.generic_string(), ec.message()));
        }
    }
}

/// adb with the device chosen.
auto run_adb(const android_toolchain& tools, const std::string& device, std::vector<std::string> arguments)
    -> subprocess::call_result
{
    return subprocess::call(make_adb_command(tools.adb, device, std::move(arguments)));
}

auto get_game_process(const android_toolchain& tools, const std::string& device, const std::string& application_id)
    -> std::string
{
    return string_utils::trim(run_adb(tools, device, {"shell", "pidof", application_id}).out_output);
}

/// Logs a line of the game's process: the game's own as [Device], the device system's as [Device System],
/// which the Console hides until asked to show them. The engine's lines keep their level, which its
/// Android sink wrote as the line's priority.
void log_device_line(const logcat_line& line)
{
    const char* prefix = is_game_log_line(line) ? DEVICE_LOG_PREFIX : DEVICE_SYSTEM_LOG_PREFIX;
    const auto message = get_device_log_message(line);
    switch(line.level)
    {
        case 'V':
            APPLOG_TRACE("{}{}", prefix, message);
            break;
        case 'D':
            APPLOG_DEBUG("{}{}", prefix, message);
            break;
        case 'W':
            APPLOG_WARNING("{}{}", prefix, message);
            break;
        case 'E':
        case 'F':
            APPLOG_ERROR("{}{}", prefix, message);
            break;
        default:
            APPLOG_INFO("{}{}", prefix, message);
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
        if(is_present(entry.path() / name))
        {
            return entry.path() / name;
        }
    }
    return {};
}

/// Logs the crash report again with source lines, when the unstripped player and ndk-stack are here.
void symbolize_crash(const android_package_plan& plan, const std::vector<std::string>& report)
{
    const auto& symbols = plan.package.player.symbols_dir;
    const auto ndk_stack = find_ndk_stack(plan.tools.sdk);
    if(symbols.empty() || !is_present(symbols / "libmain.so") || ndk_stack.empty())
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

    /// Installs and starts the game, then follows it.
    void start(const android_package_plan& plan)
    {
        clear_conflict();
        launch(plan, &game_session::run);
    }

    /// Follows the game already running on the plan's device; logs a warning when it does not run.
    void follow(const android_package_plan& plan)
    {
        launch(plan, &game_session::follow_running);
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
    using session_body = void (game_session::*)(const android_package_plan& plan);

    void launch(const android_package_plan& plan, session_body body)
    {
        end();
        leave_requested_ = false;
        stop_requested_ = false;
        running_ = true;
        worker_ = std::thread(
            [this, plan, body]()
            {
                // The session completes the deploy's device steps, whose futures need a registered thread.
                tpp::this_thread::register_this_thread(SESSION_THREAD_NAME, true);
                (this->*body)(plan);
                running_ = false;
                tpp::this_thread::unregister_this_thread();
            });
    }

    void follow_running(const android_package_plan& plan)
    {
        const auto process = get_game_process(plan.tools, plan.device, plan.package.application_id);
        if(process.empty())
        {
            APPLOG_WARNING("Deploy: {} does not run on {}. Run Last Build or Deploy and Run starts it.",
                           plan.package.application_id,
                           plan.device);
            return;
        }
        APPLOG_INFO("Deploy: following the game on {} (process {}). Its log follows.", plan.device, process);
        follow_log(plan, process);
    }

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
            fail_steps(plan, string_utils::trim(install.out_output));
            APPLOG_ERROR("Deploy: installing on {} failed: {}", device, string_utils::trim(install.out_output));
            return;
        }
        complete_step(plan.install_step);
        run_adb(tools, device, {"shell", "am", "force-stop", application_id});
        run_adb(tools, device, {"logcat", "-c"});
        const auto started = run_adb(tools, device, make_start_arguments(application_id, plan.run_arguments));
        const auto process = wait_for_process(tools, device, application_id);
        if(process.empty())
        {
            fail_device_steps(plan, "The game did not start on the device.");
            APPLOG_ERROR("Deploy: the game did not start on {}. {}", device, string_utils::trim(started.out_output));
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
        log.start(make_adb_command(tools.adb, plan.device, {"logcat", "-v", "threadtime"}),
                  [&process, &report_mutex, &crash_report](const std::string& text)
                  {
                      logcat_line line;
                      if(!parse_logcat_line(text, line))
                      {
                          return;
                      }
                      if(line.tag == CRASH_REPORT_TAG)
                      {
                          APPLOG_ERROR("{}{}", DEVICE_LOG_PREFIX, line.message);
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

std::mutex signing_mutex;
android_signing_passwords signing_passwords;

std::mutex tools_mutex;
std::vector<android_device> listed_devices;
android_toolchain found_tools;
bool has_searched_tools{false};
std::atomic<bool> is_listing_devices{false};
std::atomic<bool> has_listed_devices{false};

/// How long the connects to the discovered services may take together. adb keeps services it no longer
/// hears, and an address nothing answers at holds `adb connect` for the system's TCP timeout (21 s on
/// Windows), while a phone answers within a second.
constexpr auto WIRELESS_CONNECT_WAIT = std::chrono::seconds(4);
constexpr auto WIRELESS_CONNECTED_GRACE = std::chrono::milliseconds(500);
/// What `adb connect` prints when it reached the phone: "connected to <address>" or "already connected to
/// <address>"; a failure prints "failed to connect to" or "cannot connect to".
constexpr const char* ADB_CONNECTED = "connected to";
/// How long a listing waits for an adb server it started to discover the phones around.
constexpr auto ADB_DISCOVERY_WAIT = std::chrono::seconds(3);
constexpr auto ADB_POLL_INTERVAL = std::chrono::milliseconds(250);
/// What adb prints when the call had to start its server.
constexpr const char* ADB_SERVER_STARTED = "daemon started successfully";

auto has_ready_device(const std::vector<android_device>& devices) -> bool
{
    return std::any_of(devices.begin(),
                       devices.end(),
                       [](const android_device& device)
                       {
                           return device.is_ready();
                       });
}

auto read_devices(const std::string& adb) -> std::vector<android_device>
{
    return parse_android_devices(subprocess::call({adb, "devices", "-l"}).out_output);
}

/**
 * @brief Connects adb to every discovered service at once. Waits for them no longer than WIRELESS_CONNECT_WAIT,
 * and no longer than WIRELESS_CONNECTED_GRACE after the first one connected: the other phones answer as fast,
 * and the rest are services nothing answers at anymore.
 */
void connect_wireless_services(const std::string& adb, const std::vector<wireless_debugging_service>& services)
{
    // Shared with the streams' reader threads, which may outlive this call's loop by a line.
    auto has_connected = std::make_shared<std::atomic<bool>>(false);
    std::vector<std::unique_ptr<subprocess::stream>> connects;
    for(const auto& service : services)
    {
        auto connect = std::make_unique<subprocess::stream>();
        const auto on_line = [has_connected](const std::string& line)
        {
            if(line.find(ADB_CONNECTED) != std::string::npos)
            {
                *has_connected = true;
            }
        };
        if(connect->start({adb, "connect", service.address}, on_line))
        {
            connects.push_back(std::move(connect));
        }
    }
    auto deadline = std::chrono::steady_clock::now() + WIRELESS_CONNECT_WAIT;
    bool is_grace_set = false;
    const auto is_waiting = [&connects]()
    {
        return std::any_of(connects.begin(),
                           connects.end(),
                           [](const std::unique_ptr<subprocess::stream>& connect)
                           {
                               return connect->is_running();
                           });
    };
    while(is_waiting() && std::chrono::steady_clock::now() < deadline)
    {
        if(*has_connected && !is_grace_set)
        {
            deadline = std::min(deadline, std::chrono::steady_clock::now() + WIRELESS_CONNECTED_GRACE);
            is_grace_set = true;
        }
        std::this_thread::sleep_for(ADB_POLL_INTERVAL);
    }
}

/// The devices once an adb server that was just started has discovered a ready one, or ADB_DISCOVERY_WAIT passed.
auto wait_for_discovery(const std::string& adb) -> std::vector<android_device>
{
    const auto deadline = std::chrono::steady_clock::now() + ADB_DISCOVERY_WAIT;
    std::vector<android_device> devices;
    do
    {
        std::this_thread::sleep_for(ADB_POLL_INTERVAL);
        devices = read_devices(adb);
    } while(!has_ready_device(devices) && std::chrono::steady_clock::now() < deadline);
    return devices;
}

/// One entry per phone: each ready device's own serial number tells the connections to one phone apart
/// from other phones. The same call reads what the device list shows of each: its name, model and Android
/// version.
auto merge_connections(const std::string& adb, std::vector<android_device> devices) -> std::vector<android_device>
{
    const auto command = get_android_device_properties_command();
    for(auto& device : devices)
    {
        if(device.is_ready())
        {
            read_android_device_properties(subprocess::call(make_adb_command(adb, device.serial, {"shell", command})).out_output,
                                           device);
        }
    }
    return merge_android_devices(std::move(devices));
}

/**
 * @brief The devices adb lists. When none is ready, the wireless debugging services adb discovered are
 * connected first: adb does not connect a service again once it lost the connection, until the phone announces
 * it anew. When this call started adb's server, the listing also waits for that server's discovery.
 */
auto list_devices(const android_toolchain& tools) -> std::vector<android_device>
{
    if(tools.adb.empty())
    {
        return {};
    }
    const auto adb = tools.adb.string();
    const auto first = subprocess::call({adb, "devices", "-l"});
    auto devices = parse_android_devices(first.out_output);
    if(!has_ready_device(devices))
    {
        connect_wireless_services(adb, parse_wireless_debugging_services(subprocess::call({adb, "mdns", "services"}).out_output));
        devices = read_devices(adb);
    }
    if(!has_ready_device(devices) && first.out_output.find(ADB_SERVER_STARTED) != std::string::npos)
    {
        devices = wait_for_discovery(adb);
    }
    return merge_connections(adb, std::move(devices));
}
/// A device command runs in the background: Clear Data or Uninstall.
std::atomic<bool> is_running_device_command{false};

/// The chosen device and the game on it, without the deploy's steps: what the device actions read.
auto make_device_plan(rtti::context& ctx, const deploy_settings& params) -> android_package_plan
{
    auto plan = make_android_package_plan(ctx, params);
    plan.install_step.reset();
    plan.start_step.reset();
    return plan;
}

/// Runs adb with `arguments` on the plan's device in the background; logs `done` when adb answers
/// Success, and adb's answer otherwise. One command at a time.
void schedule_device_command(rtti::context& ctx, const android_package_plan& plan, std::vector<std::string> arguments, std::string done)
{
    if(is_running_device_command.exchange(true))
    {
        return;
    }
    auto& th = ctx.get_cached<threader>();
    th.pool->schedule("Android device command",
                      [plan, arguments = std::move(arguments), done = std::move(done)]()
                      {
                          const auto result = run_adb(plan.tools, plan.device, arguments);
                          const auto answer = string_utils::trim(result.out_output);
                          if(result.retcode == 0 && answer.find("Success") != std::string::npos)
                          {
                              APPLOG_INFO("Deploy: {}", done);
                          }
                          else
                          {
                              APPLOG_ERROR("Deploy: adb {} on {} failed: {}", fmt::join(arguments, " "), plan.device, answer);
                          }
                          is_running_device_command = false;
                      });
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
    static const auto lookup = find_android_template(fs::resolve_protocol(std::string(PLAYER_TEMPLATES_FOLDER) + ANDROID_TEMPLATE_NAME),
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

auto get_android_content_dir(const std::string& profile) -> fs::path
{
    return get_build_dir(profile) / "content";
}

auto get_generated_android_application_id(rtti::context& ctx, const std::string& profile) -> std::string
{
    const auto app = ctx.get_cached<project_manager>().get_profile_settings(profile).app;
    return make_default_application_id(app.company, app.product.empty() ? get_project_name(ctx) : app.product);
}

auto get_android_application_id(rtti::context& ctx, const deploy_settings& params) -> std::string
{
    return params.android.application_id.empty() ? get_generated_android_application_id(ctx, params.profile)
                                                 : params.android.application_id;
}

auto get_android_signing_passwords() -> android_signing_passwords
{
    std::lock_guard<std::mutex> lock(signing_mutex);
    return signing_passwords;
}

void set_android_signing_passwords(const android_signing_passwords& passwords)
{
    std::lock_guard<std::mutex> lock(signing_mutex);
    signing_passwords = passwords;
}

void clear_android_signing_passwords()
{
    set_android_signing_passwords({});
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
    for(const auto& name : params.android.permissions.other)
    {
        const auto permission_problem = check_android_permission_name(name);
        if(!permission_problem.empty())
        {
            problems.push_back(permission_problem);
        }
    }
    if(!is_present(get_icon_image(params)))
    {
        problems.push_back(fmt::format("The icon image {} is missing. Choose another one, or none for the engine's logo.",
                                       get_icon_image(params).generic_string()));
    }
    if(params.android.release)
    {
        if(params.android.keystore.empty() || !is_present(params.android.keystore))
        {
            problems.push_back("A release build needs the keystore that signs it. Choose one, or turn off Release Build.");
        }
        else if(params.android.key_alias.empty())
        {
            problems.push_back("A release build needs the key alias of its keystore.");
        }
        else if(get_android_signing_passwords().keystore.empty() && std::getenv(KEYSTORE_PASSWORD_VARIABLE) == nullptr)
        {
            problems.push_back(fmt::format("A release build needs the keystore's password. Type it in Keystore Password "
                                           "in the Deploy window, or set {} and restart the editor.",
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
                                             return listed.is_named(params.android_device);
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

auto get_android_package_path(rtti::context& ctx, const deploy_settings& params) -> fs::path
{
    return get_output_location(params) / (get_project_name(ctx) + ".apk");
}

auto has_android_package(rtti::context& ctx, const deploy_settings& params) -> bool
{
    return is_present(get_android_package_path(ctx, params));
}

auto make_android_package_plan(rtti::context& ctx, const deploy_settings& params) -> android_package_plan
{
    const auto app = ctx.get_cached<project_manager>().get_profile_settings(params.profile).app;
    android_package_plan plan;
    plan.tools = get_android_toolchain();
    plan.device = params.android_device;
    plan.run_arguments = split_run_arguments(params.deploy_run_arguments);
    plan.replace_installed = params.android_replace_installed;
    plan.output = get_android_package_path(ctx, params);
    if(params.deploy_and_run)
    {
        plan.install_step = std::make_shared<device_step>();
        plan.start_step = std::make_shared<device_step>();
    }
    auto& package = plan.package;
    plan.build_dir = get_build_dir(params.profile);
    plan.build_owner = {get_project_root(), params.profile};
    package.player = get_android_template().found;
    package.project_dir = get_gradle_project_dir(params.profile);
    package.content_dir = get_android_content_dir(params.profile);
    package.application_id = get_android_application_id(ctx, params);
    package.name = app.product.empty() ? get_project_name(ctx) : app.product;
    package.version_name = app.version.empty() ? DEFAULT_VERSION_NAME : app.version;
    package.version_code = std::max<std::uint32_t>(1, params.android.version_code);
    package.orientation = params.android.screen_orientation;
    package.icon_image = get_icon_image(params);
    package.icon_background = params.android.icon_background;
    package.icon_dir = get_build_dir(params.profile) / "icon";
    package.permissions = get_android_permission_names(params.android.permissions);
    package.permissions_manifest = get_build_dir(params.profile) / "permissions" / "AndroidManifest.xml";
    package.is_release = params.android.release;
    package.keystore = params.android.keystore;
    package.key_alias = params.android.key_alias;
    return plan;
}

void remove_unused_android_build_dirs(const android_package_plan& plan)
{
    std::size_t removed = 0;
    for(const auto& folder : find_unused_android_build_dirs(plan.build_dir.parent_path(), plan.build_dir, UNUSED_BUILD_LIFETIME))
    {
        fs::error_code ec;
        remove_build_dir(folder, ec);
        if(ec)
        {
            APPLOG_TRACE("Deploy: the unused Android build folder {} stays for now: {}", folder.generic_string(), ec.message());
            continue;
        }
        ++removed;
    }
    if(removed > 0)
    {
        APPLOG_INFO("Deploy: removed {} Android build folder(s) in {} that no deploy used for {} days, or whose project is "
                    "gone.",
                    removed,
                    plan.build_dir.parent_path().generic_string(),
                    UNUSED_BUILD_DAYS);
    }
}

void build_android_package(const android_package_plan& plan)
{
    const auto& package = plan.package;
    if(!write_android_build_owner(plan.build_dir, plan.build_owner))
    {
        throw std::runtime_error(fmt::format("The Android build folder {} cannot be written.", plan.build_dir.generic_string()));
    }
    update_gradle_project(package.player.get_project_dir(), package.project_dir);
    write_text_or_throw(package.project_dir / "local.properties", make_gradle_local_properties(plan.tools.sdk));
    const auto icon = write_android_icon(package.icon_image, package.icon_background, package.icon_dir);
    if(!icon.is_ok())
    {
        throw std::runtime_error(icon.error);
    }
    fs::error_code ec;
    fs::create_directories(package.permissions_manifest.parent_path(), ec);
    write_text_or_throw(package.permissions_manifest, make_android_permissions_manifest(package.permissions));
    APPLOG_INFO("Deploy: building the Android package with Gradle in {}.", package.project_dir.generic_string());
    const auto environment = package.is_release ? make_signing_environment(get_android_signing_passwords()) : std::vector<std::string>{};
    const auto result = subprocess::call(make_gradle_arguments(plan.tools, package),
                                         environment,
                                         [](const std::string& line)
                                         {
                                             if(!line.empty())
                                             {
                                                 APPLOG_TRACE("[Gradle] {}", line);
                                             }
                                         },
                                         &is_deploy_cancelled);
    throw_if_deploy_cancelled();
    if(!result.started)
    {
        throw std::runtime_error(fmt::format("Java did not start ({}).", plan.tools.java.generic_string()));
    }
    if(result.retcode != 0)
    {
        throw std::runtime_error(fmt::format("Gradle failed with exit code {}. Its last output:\n{}",
                                             result.retcode,
                                             string_utils::get_last_lines(result.out_output, GRADLE_ERROR_LINES)));
    }
    copy_file_or_throw(get_built_package(package.project_dir, package.is_release), plan.output);
    APPLOG_INFO("Deploy: built {} ({} MB).", plan.output.generic_string(), fs::file_size(plan.output, ec) / (1024 * 1024));
    const auto bundle = get_bundle_path(plan.output);
    const auto symbols = get_native_symbols_path(plan.output);
    if(package.is_release)
    {
        copy_file_or_throw(get_built_bundle(package.project_dir), bundle);
        copy_file_or_throw(get_built_native_symbols(package.project_dir), symbols);
        APPLOG_INFO("Deploy: built {} ({} MB) with version code {}. Upload it to Google Play, and {} as its native debug "
                    "symbols.",
                    bundle.generic_string(),
                    fs::file_size(bundle, ec) / (1024 * 1024),
                    package.version_code,
                    symbols.filename().generic_string());
    }
    else
    {
        // A debug build leaves no store files of an earlier release beside a package they do not match.
        fs::remove(bundle, ec);
        fs::remove(symbols, ec);
    }
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

auto run_last_android_build(rtti::context& ctx, const deploy_settings& params) -> deploy_jobs
{
    auto plan = make_android_package_plan(ctx, params);
    plan.install_step = std::make_shared<device_step>();
    plan.start_step = std::make_shared<device_step>();
    APPLOG_INFO("Deploy: running the last build, {}.", plan.output.generic_string());
    run_android_game(plan);
    return get_device_jobs(plan);
}

void follow_android_game(rtti::context& ctx, const deploy_settings& params)
{
    get_session().follow(make_device_plan(ctx, params));
}

void clear_android_game_data(rtti::context& ctx, const deploy_settings& params)
{
    const auto plan = make_device_plan(ctx, params);
    const auto& id = plan.package.application_id;
    schedule_device_command(ctx,
                            plan,
                            {"shell", "pm", "clear", id},
                            fmt::format("cleared the saved data of {} on {}.", id, plan.device));
}

void uninstall_android_game(rtti::context& ctx, const deploy_settings& params)
{
    const auto plan = make_device_plan(ctx, params);
    const auto& id = plan.package.application_id;
    schedule_device_command(ctx, plan, {"uninstall", id}, fmt::format("uninstalled {} from {}, with its saved data.", id, plan.device));
}

auto is_android_device_busy() -> bool
{
    return is_running_device_command;
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
