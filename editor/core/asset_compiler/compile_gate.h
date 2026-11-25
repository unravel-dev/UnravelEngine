#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace unravel::asset_compiler
{

/**
 * @brief Keeps the compiles of one compiled file from running at the same time.
 *
 * Every change of an asset's source or import settings asks for a compile, and a compile reads the settings when
 * it starts. Two compiles of one file that ran together would finish in either order, so one that started first,
 * with the older settings, could write its result over the newer one: a texture's first compile, as a color
 * texture, over its compile as the normal map a model import made of it meanwhile.
 *
 * Through the gate a request that arrives while the file compiles starts nothing. The compile that runs compiles
 * the file once more when it is done, so the last compile to finish always started after the last request.
 */
class compile_gate
{
public:
    /**
     * @brief Calls `compile` to compile `output`, unless a compile of it runs: that one then calls its own
     * `compile` again once it is done, and this call returns at once. Any thread.
     * @return Whether this call compiled.
     */
    template<typename Compile>
    auto run(const std::string& output, Compile&& compile) -> bool
    {
        if(!enter(output))
        {
            return false;
        }
        const leave_on_exit leave_guard{*this, output};
        do
        {
            compile();
        } while(is_asked_again(output));
        return true;
    }

private:
    /// Lets go of `output` when a compile ends by an exception, so the next request compiles it.
    struct leave_on_exit
    {
        compile_gate& gate;
        const std::string& output;

        ~leave_on_exit()
        {
            gate.leave(output);
        }
    };

    /// Whether the caller compiles `output` now. When a compile of it runs, that compile is asked to run again.
    auto enter(const std::string& output) -> bool
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto [entry, is_new] = compiling_.emplace(output, false);
        if(!is_new)
        {
            entry->second = true;
        }
        return is_new;
    }

    /// Whether a request came for `output` since its compile started; that request is taken, and the caller
    /// compiles again. Without one the file is let go.
    auto is_asked_again(const std::string& output) -> bool
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto entry = compiling_.find(output);
        if(entry == compiling_.end())
        {
            return false;
        }
        if(entry->second)
        {
            entry->second = false;
            return true;
        }
        compiling_.erase(entry);
        return false;
    }

    void leave(const std::string& output)
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        compiling_.erase(output);
    }

    std::mutex mutex_;
    /// The files compiling now, each with whether it was asked for again since its compile started.
    std::unordered_map<std::string, bool> compiling_;
};

} // namespace unravel::asset_compiler
