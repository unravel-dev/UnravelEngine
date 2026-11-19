#pragma once

#include "eviction.h"
#include "graphics.h"
#include <atomic>
#include <functional>
#include <memory>
#include <hpp/string_view.hpp>

namespace gfx
{

/// A bgfx handle the graphics API thread writes and any thread reads: a relaxed atomic, a plain load or
/// store on every platform, so a reader on another thread gets one whole handle, the old or the new.
/// Converts to and assigns from the handle type, so the bgfx functions that take one take it.
template<typename T>
class shared_handle
{
public:
    shared_handle(T handle) noexcept : value_(handle)
    {
    }
    shared_handle(const shared_handle&) = delete;
    auto operator=(const shared_handle&) -> shared_handle& = delete;
    shared_handle(shared_handle&&) = delete;
    auto operator=(shared_handle&&) -> shared_handle& = delete;
    ~shared_handle() = default;

    auto operator=(T handle) noexcept -> shared_handle&
    {
        value_.store(handle, std::memory_order_relaxed);
        return *this;
    }

    operator T() const noexcept
    {
        return value_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<T> value_;
};

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
        leave_eviction();
        dispose();
    }

    void dispose()
    {
        if(has_gpu_copy())
        {
            bgfx::destroy(handle_);
        }

        handle_ = invalid_handle();
    }

    /// Whether the resource can be drawn: it has a GPU copy, or paging took the copy of an idle resource
    /// and the next read through @ref native_handle creates it again. Code that skips a draw for an
    /// invalid resource never reads a paged-out one, so it would never come back.
    [[nodiscard]] auto is_valid() const -> bool
    {
        return has_gpu_copy() || get_evict_state() == evict_state::evicted;
    }

    /// Whether a GPU copy exists now. False for a resource paging took, which is still valid.
    [[nodiscard]] auto has_gpu_copy() const -> bool
    {
        return bgfx::isValid(static_cast<T>(handle_));
    }

    /// The handle to bind for a read of @p read kind. On the graphics API thread an evicted resource
    /// gets a GPU copy first: the full one, or for an @ref texture_read::any_level read of a texture
    /// with a tail, its tail; a full read of a reduced texture brings it back to level 0. Other threads
    /// get the copy there is, which stays valid until the end of the frame being built: bgfx destroys
    /// a copy the API thread replaces only after that frame's draws. Falls back to the base type's
    /// fallback handle when there is none.
    auto native_handle(texture_read read = texture_read::full) const -> T
    {
        if(needs_residency_for(read))
        {
            eviction::request_residency(const_cast<handle_impl*>(this), read);
        }
        touch(read);

        const T handle = handle_;
        if(!bgfx::isValid(handle))
        {
            return get_fallback_handle();
        }
        return handle;
    }

    static auto invalid_handle() -> T
    {
        T invalid = {bgfx::kInvalidHandle};
        return invalid;
    }

    void set_name(const hpp::string_view& _name)
    {
        bgfx::setName(handle_, _name.data(), static_cast<int32_t>(_name.size()));
    }

    auto on_evict() -> std::uint64_t override
    {
        if(has_gpu_copy())
        {
            bgfx::destroy(handle_);
        }
        handle_ = invalid_handle();
        return gpu_size_;
    }

    auto on_set_level(std::uint8_t level) -> bool override
    {
        if(level != 0 || !restore_fn_)
        {
            return false;
        }
        const T previous = handle_;
        handle_ = invalid_handle();
        if(!restore_fn_(static_cast<Base&>(*this)) || !has_gpu_copy())
        {
            handle_ = previous;
            return false;
        }
        if(bgfx::isValid(previous))
        {
            bgfx::destroy(previous);
        }
        return true;
    }

    [[nodiscard]] auto gpu_size() const -> std::uint64_t override
    {
        return gpu_size_;
    }

protected:
    auto fallback_handle() const -> T
    {
        return handle_;
    }

    auto get_fallback_handle() const -> T
    {
        return static_cast<const Base&>(*this).fallback_handle();
    }
    /// Opt the resource into the eviction system. Call once, after the GPU handle is created and a
    /// CPU-side backing exists. @p restore_fn recreates the handle from that backing and returns
    /// true on success. @p reserved_bytes from eviction::admit_allocation now count as the handle.
    /// Resources that never call this stay @ref evict_class::non_evictable and incur no tracking cost.
    void make_evictable(std::uint64_t gpu_bytes,
                        std::function<bool(Base&)> restore_fn,
                        evict_class cls = evict_class::evictable,
                        std::uint64_t reserved_bytes = 0)
    {
        if(!eviction::is_supported())
        {
            return;
        }
        gpu_size_ = gpu_bytes;
        restore_fn_ = std::move(restore_fn);
        evict_class_ = cls;
        eviction::register_resource(this, 0, reserved_bytes);
        touch();
    }

    /// Opt in with CPU backing but no GPU handle yet (create skipped or deferred). Restored on the
    /// next @ref native_handle access via the same path as post-eviction restore. Releases
    /// @p reserved_bytes from eviction::admit_allocation for the create that did not happen.
    void make_evictable_deferred(std::uint64_t gpu_bytes,
                                 std::function<bool(Base&)> restore_fn,
                                 evict_class cls = evict_class::evictable,
                                 std::uint64_t reserved_bytes = 0)
    {
        if(!eviction::is_supported())
        {
            return;
        }
        gpu_size_ = gpu_bytes;
        restore_fn_ = std::move(restore_fn);
        evict_class_ = cls;
        eviction::register_evicted_resource(this, reserved_bytes);
    }

    /// Stop being tracked. A derived class whose callbacks use its own members calls this first in its
    /// destructor, so the registry never calls into it once those members are gone.
    void leave_eviction()
    {
        if(evict_class_ != evict_class::non_evictable)
        {
            eviction::unregister_resource(this);
            evict_class_ = evict_class::non_evictable;
        }
    }

    shared_handle<T> handle_ = invalid_handle();
    std::function<bool(Base&)> restore_fn_;
    std::uint64_t gpu_size_ = 0;
};
} // namespace gfx
