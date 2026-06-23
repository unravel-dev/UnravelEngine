#pragma once

#include "eviction.h"
#include "graphics.h"
#include <functional>
#include <memory>
#include <hpp/string_view.hpp>

namespace gfx
{

template<typename Base, typename T>
class handle_impl : public ievictable
{
public:
    using ptr = std::shared_ptr<Base>;
    using uptr = std::unique_ptr<Base>;
    using weak_ptr = std::weak_ptr<Base>;

    using handle_type_t = T;
    using base_type = Base;

    handle_impl() = default;
    handle_impl(const handle_impl&) = delete;
    auto operator=(const handle_impl&) -> handle_impl& = delete;
    handle_impl(handle_impl&&) = delete;
    auto operator=(handle_impl&&) -> handle_impl& = delete;

    ~handle_impl() override
    {
        if(evict_class_ != evict_class::non_evictable)
        {
            eviction::unregister_resource(this);
        }
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
        if(evict_state_ == evict_state::evicted)
        {
            eviction::restore_resource(const_cast<handle_impl*>(this));
        }
        touch();
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

    auto on_evict() -> std::uint64_t override
    {
        if(evict_state_ == evict_state::evicted)
        {
            return 0;
        }
        const std::uint64_t freed = gpu_size_;
        if(is_valid())
        {
            gfx::destroy(handle_);
        }
        handle_ = invalid_handle();
        evict_state_ = evict_state::evicted;
        return freed;
    }

    auto on_restore() -> bool override
    {
        if(evict_state_ == evict_state::resident)
        {
            return true;
        }
        const bool ok = restore_fn_ && restore_fn_(static_cast<Base&>(*this));
        evict_state_ = ok ? evict_state::resident : evict_state::evicted;
        return ok;
    }

    [[nodiscard]] auto gpu_size() const -> std::uint64_t override
    {
        return gpu_size_;
    }

protected:
    /// Opt the resource into the eviction system. Call once, after the GPU handle is created and a
    /// CPU-side backing exists. @p restore_fn recreates the handle from that backing and returns
    /// true on success. Resources that never call this stay @ref evict_class::non_evictable and
    /// incur no tracking cost.
    void make_evictable(std::uint64_t gpu_bytes,
                        std::function<bool(Base&)> restore_fn,
                        evict_class cls = evict_class::evictable)
    {
        if(!eviction::is_supported())
        {
            return;
        }
        gpu_size_ = gpu_bytes;
        restore_fn_ = std::move(restore_fn);
        evict_class_ = cls;
        eviction::register_resource(this);
    }

    T handle_ = invalid_handle();
    std::function<bool(Base&)> restore_fn_;
    std::uint64_t gpu_size_ = 0;
};
} // namespace gfx
