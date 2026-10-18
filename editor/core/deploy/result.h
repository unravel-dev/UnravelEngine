#pragma once

#include <string>
#include <utility>

namespace unravel::deployment
{

/// The outcome of a deploy action that can fail for an expected reason, such as a release name that is
/// already kept. Unexpected failures inside a running deploy step throw instead.
struct result
{
    /// Why the action did nothing; empty when it succeeded.
    std::string error;

    static auto failure(std::string reason) -> result
    {
        return {std::move(reason)};
    }

    auto is_ok() const -> bool
    {
        return error.empty();
    }
};

} // namespace unravel::deployment
