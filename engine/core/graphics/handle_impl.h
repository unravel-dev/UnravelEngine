#pragma once

#include "graphics.h"
#include <memory>
#include <hpp/string_view.hpp>

namespace gfx
{

template<typename Base, typename T>
class handle_impl
{
public:
    using ptr = std::shared_ptr<Base>;
    using uptr = std::unique_ptr<Base>;
    using weak_ptr = std::weak_ptr<Base>;

    using handle_type_t = T;
    using base_type = Base;

    ~handle_impl()
    {
        dispose();
    }

    void dispose()
    {
        if(is_valid())
        {
            gfx::destroy(handle_);
        }

        handle_ = invalid_handle();
    }

    [[nodiscard]] auto is_valid() const -> bool
    {
        return bgfx::isValid(handle_);
    }

    auto native_handle() const -> T
    {
        return handle_;
    }

    static auto invalid_handle() -> T
    {
        T invalid = {bgfx::kInvalidHandle};
        return invalid;
    }

    void set_name(const hpp::string_view& _name)
    {
        gfx::set_name(handle_, _name.data(), static_cast<int32_t>(_name.size()));
    }

protected:
    T handle_ = invalid_handle();
};
} // namespace gfx
