#pragma once

#include <memory>
#include <threadpp/thread.h>
#include <utility>

namespace unravel
{

/**
 * @brief Deletes an object on the main thread.
 *
 * Use for types whose destructor touches thread-affine APIs (OpenAL, some GPU
 * wrappers, static managers). Destruction is fire-and-forget via
 * `tpp::dispatch` so worker threads never block waiting on main. If the caller
 * is already on the main thread, delete runs inline.
 *
 * Prefer constructing such assets with `make_shared_main_thread` so every
 * release path (reload, demote, last external handle drop) is covered — not
 * only the load site that already hops to main for creation.
 */
template<typename T>
struct main_thread_deleter
{
    void operator()(T* ptr) const
    {
        if(!ptr)
        {
            return;
        }
        tpp::this_thread::register_this_thread();
        if(!tpp::dispatch(tpp::main_thread::get_id(),
                          [ptr]()
                          {
                              delete ptr;
                          }))
        {
            // Queue unavailable (e.g. after tpp::shutdown). Last resort.
            delete ptr;
        }
    }
};

/**
 * @brief Allocates T with a deleter that always runs on the main thread.
 */
template<typename T, typename... Args>
auto make_shared_main_thread(Args&&... args) -> std::shared_ptr<T>
{
    return std::shared_ptr<T>(new T(std::forward<Args>(args)...), main_thread_deleter<T>{});
}

/**
 * @brief Adopts an existing pointer with main-thread deletion.
 */
template<typename T>
auto adopt_shared_main_thread(T* ptr) -> std::shared_ptr<T>
{
    return std::shared_ptr<T>(ptr, main_thread_deleter<T>{});
}

} // namespace unravel
