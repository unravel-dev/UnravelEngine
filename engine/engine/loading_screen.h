#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace unravel
{

/**
 * @brief What the application is loading: a session (the start of the application, the opening of a
 * project) runs modules one after the other, and the module in work reports how far it is.
 *
 * A visualizer, when one is set, is called on every change while a session is active to draw it.
 * Between sessions nothing is drawn, so code that also runs outside of loading may report freely.
 */
struct loading_screen
{
    using clock = std::chrono::steady_clock;
    using visualizer_fn = std::function<void(const loading_screen&)>;
    using on_fail_fn = std::function<void(const std::string& module, const std::string& message)>;

    /// A module of the session that has finished, with how long it took.
    struct module_record
    {
        std::string name;
        clock::duration duration{};
    };

    void set_visualizer(visualizer_fn fn);
    void set_on_fail(on_fail_fn fn);

    /// Starts a new session and forgets the modules of the one before.
    void begin_session(const std::string& title);
    /// Ends the session: the visualizer is not called again until the next one begins.
    void end_session();
    /// Finishes the module in work, if any, and starts the next.
    void begin_module(const std::string& module_name);
    /// How far the module is: completed of total items (total 0 while there is no count to report),
    /// the step of the module that is in work ("Indexing asset metadata") and the item that step
    /// is on (a file). Step and item may be empty.
    void progress(size_t completed, size_t total, const std::string& step = {}, const std::string& item = {});
    /// Reports what the module is doing when it has nothing to count.
    void log(const std::string& message);
    /// Redraws while the module waits on work that reports no progress of its own, e.g. a job.
    void refresh();
    void fail(const std::string& message);

    /// Convenience: if `result` is false and no error was set, auto-fails with the current module name.
    auto check(bool result) -> bool;

    auto has_failed() const -> bool;
    auto error_message() const -> const std::string&;

    auto is_session_active() const -> bool;
    auto session_title() const -> const std::string&;
    auto session_elapsed() const -> clock::duration;
    /// The finished modules of the session, oldest first.
    auto finished_modules() const -> const std::vector<module_record>&;

    auto current_module() const -> const std::string&;
    auto module_elapsed() const -> clock::duration;
    auto completed() const -> size_t;
    auto total() const -> size_t;
    auto current_step() const -> const std::string&;
    auto current_item() const -> const std::string&;

private:
    void finish_module();
    void notify_visualizer();

    bool is_session_active_{};
    std::string session_title_;
    clock::time_point session_start_{clock::now()};
    std::vector<module_record> finished_modules_;

    std::string module_;
    clock::time_point module_start_{clock::now()};
    size_t completed_{};
    size_t total_{};
    std::string step_;
    std::string item_;

    bool failed_{};
    std::string error_msg_;

    visualizer_fn visualizer_;
    on_fail_fn on_fail_;
};

} // namespace unravel
