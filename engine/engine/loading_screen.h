#pragma once

#include <functional>
#include <string>

namespace unravel
{

struct loading_screen
{
    using visualizer_fn = std::function<void(const loading_screen&)>;
    using on_fail_fn = std::function<void(const std::string& module, const std::string& message)>;

    void set_visualizer(visualizer_fn fn);
    void set_on_fail(on_fail_fn fn);

    void begin_module(const std::string& module_name);
    void progress(size_t completed, size_t total, const std::string& current_job = {});
    void log(const std::string& message);
    void fail(const std::string& message);

    /// Convenience: if `result` is false and no error was set, auto-fails with the current module name.
    auto check(bool result) -> bool;

    auto has_failed() const -> bool;
    auto error_message() const -> const std::string&;
    auto current_module() const -> const std::string&;
    auto completed() const -> size_t;
    auto total() const -> size_t;
    auto current_job() const -> const std::string&;

private:
    void notify_visualizer();

    std::string module_;
    size_t completed_{};
    size_t total_{};
    std::string current_job_;

    bool failed_{};
    std::string error_msg_;

    visualizer_fn visualizer_;
    on_fail_fn on_fail_;
};

} // namespace unravel
