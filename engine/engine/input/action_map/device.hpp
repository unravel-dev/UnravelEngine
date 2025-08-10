#pragma once

#include "device_type.hpp"
#include <string>

namespace input
{
class input_device
{
    device_type type_;
    bool is_input_allowed_{true};


public:
    input_device(const device_type type) : type_(type)
    {
    }

    virtual ~input_device()
    {
    }

    auto get_device_type() const -> device_type
    {
        return type_;
    }

    virtual auto get_name() const -> const std::string& = 0;

    void set_is_input_allowed(bool allowed)
    {
        is_input_allowed_= allowed;
    }
    auto is_input_allowed() const -> bool
    {
        return is_input_allowed_;
    }
};
} // namespace input
