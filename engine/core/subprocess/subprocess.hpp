#pragma once
#include "subprocess.h"
#include <string>
#include <cstring>
#include <vector>

namespace subprocess
{
struct call_result
{
    int retcode{};
    std::string out_output;
    std::string err_output;
};

inline auto call(const std::vector<std::string>& args_array) -> call_result
{
    call_result result;
    result.retcode = 1;

    std::vector<const char*> args;

    args.reserve(args_array.size());
    for(const auto& arg : args_array)
    {
        args.emplace_back(arg.c_str());
    }
    args.emplace_back(nullptr);

    subprocess_s subprocess;
    std::memset(&subprocess, 0, sizeof(subprocess));
    {
        int options = /*subprocess_option_enable_async |*/ subprocess_option_inherit_environment |
                      subprocess_option_search_user_path | subprocess_option_combined_stdout_stderr;
        int op_code = subprocess_create(args.data(), options, &subprocess);
        if(0 != op_code)
        {
            // an error occurred!
            result.out_output = "Failed to create subprocess for " + args_array.front();
            return result;
        }
    }

    {
        // Buffer for reading the output
        char out_buffer[1024];
        char err_buffer[1024];

        auto read_all_stdout = [&]()
        {
            unsigned bytes_read;
            while((bytes_read = subprocess_read_stdout(&subprocess, out_buffer, sizeof(out_buffer) - 1)) > 0)
            {
                out_buffer[bytes_read] = '\0'; // Null-terminate the buffer
                result.out_output += out_buffer;
            }
        };

        auto read_all_stderr = [&]()
        {
            unsigned bytes_read;
            while((bytes_read = subprocess_read_stderr(&subprocess, err_buffer, sizeof(err_buffer) - 1)) > 0)
            {
                err_buffer[bytes_read] = '\0'; // Null-terminate the buffer
                result.err_output += err_buffer;
            }
        };

        // Read output from the process continuously
        while(subprocess_alive(&subprocess))
        {
            read_all_stdout();
            read_all_stderr();
        }

        // Ensure all remaining output is drained after the process finishes
        read_all_stdout();
        read_all_stderr();
    }

    {
        int op_code = subprocess_join(&subprocess, &result.retcode);
        if(0 != op_code)
        {
            // an error occurred!
        }
    }

    {
        int op_code = subprocess_destroy(&subprocess);
        if(0 != op_code)
        {
            // an error occurred!
        }
    }

    return result;
}

inline auto call(const std::string& process, const std::vector<std::string>& args_array = {}) -> call_result
{
    std::vector<std::string> merged{process};
    merged.insert(merged.end(), args_array.begin(), args_array.end());

    return call(merged);
}
} // namespace subprocess
