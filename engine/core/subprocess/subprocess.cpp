#include "subprocess.hpp"

#include <atomic>
#include <cstring>

namespace subprocess
{
namespace
{
constexpr std::size_t READ_BUFFER_SIZE = 4096;
// No window: a process started without a console (detached, or from a shortcut) would otherwise open a
// console window for every tool it runs; the output is read from pipes.
constexpr int CREATE_OPTIONS = subprocess_option_search_user_path | subprocess_option_combined_stdout_stderr |
                               subprocess_option_no_window;

auto to_pointers(const std::vector<std::string>& strings) -> std::vector<const char*>
{
    std::vector<const char*> pointers;
    pointers.reserve(strings.size() + 1);
    for(const auto& text : strings)
    {
        pointers.push_back(text.c_str());
    }
    pointers.push_back(nullptr);
    return pointers;
}

/// Starts `args_array` with this process's environment plus `changes`; 0 on success.
auto create(const std::vector<std::string>& args_array, const std::vector<std::string>& changes, subprocess_s& process)
    -> int
{
    std::memset(&process, 0, sizeof(process));
    if(args_array.empty())
    {
        return -1;
    }
    const auto arguments = to_pointers(args_array);
    if(changes.empty())
    {
        return subprocess_create(arguments.data(), CREATE_OPTIONS | subprocess_option_inherit_environment, &process);
    }
    // A given environment cannot be combined with inheriting one, so the merge happens here.
    const auto merged = merge_environment(changes);
    const auto environment = to_pointers(merged);
    return subprocess_create_ex(arguments.data(), CREATE_OPTIONS, environment.data(), &process);
}

/// Splits output into lines for a callback.
class line_splitter
{
public:
    explicit line_splitter(const line_callback& on_line)
        : on_line_(on_line)
    {
    }

    void add(const char* data, std::size_t size)
    {
        if(!on_line_)
        {
            return;
        }
        for(std::size_t i = 0; i < size; ++i)
        {
            if(data[i] == '\n')
            {
                emit();
            }
            else
            {
                pending_ += data[i];
            }
        }
    }

    void finish()
    {
        if(!pending_.empty())
        {
            emit();
        }
    }

private:
    void emit()
    {
        // adb on Windows ends its lines with "\r\r\n".
        while(!pending_.empty() && pending_.back() == '\r')
        {
            pending_.pop_back();
        }
        on_line_(pending_);
        pending_.clear();
    }

    const line_callback& on_line_;
    std::string pending_;
};

/// Reads until the process closes its output; also kept whole in `output` when it is given.
void read_output(subprocess_s& process, line_splitter& splitter, std::string* output)
{
    char buffer[READ_BUFFER_SIZE];
    unsigned read = 0;
    while((read = subprocess_read_stdout(&process, buffer, sizeof(buffer))) > 0)
    {
        if(output != nullptr)
        {
            output->append(buffer, read);
        }
        splitter.add(buffer, read);
    }
    splitter.finish();
}
} // namespace

auto call(const std::vector<std::string>& args_array, const std::vector<std::string>& environment_array, const line_callback& on_line)
    -> call_result
{
    call_result result;
    subprocess_s process;
    if(0 != create(args_array, environment_array, process))
    {
        result.out_output = "Failed to create subprocess for " + (args_array.empty() ? std::string() : args_array.front());
        return result;
    }
    result.started = true;
    line_splitter splitter(on_line);
    read_output(process, splitter, &result.out_output);
    subprocess_join(&process, &result.retcode);
    subprocess_destroy(&process);
    return result;
}

struct stream::state
{
    subprocess_s process{};
    line_callback on_line;
    std::atomic<bool> is_running{false};
};

stream::~stream()
{
    stop();
}

auto stream::start(const std::vector<std::string>& args_array, line_callback on_line) -> bool
{
    stop();
    auto started = std::make_shared<state>();
    started->on_line = std::move(on_line);
    if(0 != create(args_array, {}, started->process))
    {
        return false;
    }
    started->is_running = true;
    state_ = started;
    reader_ = std::thread(
        [started]()
        {
            line_splitter splitter(started->on_line);
            read_output(started->process, splitter, nullptr);
            started->is_running = false;
        });
    return true;
}

void stream::stop()
{
    if(!state_)
    {
        return;
    }
    // Ending the process closes its output, which ends the reader's blocking read.
    subprocess_terminate(&state_->process);
    if(reader_.joinable())
    {
        reader_.join();
    }
    int exit_code = 0;
    subprocess_join(&state_->process, &exit_code);
    subprocess_destroy(&state_->process);
    state_.reset();
}

auto stream::is_running() const -> bool
{
    return state_ && state_->is_running;
}
} // namespace subprocess
