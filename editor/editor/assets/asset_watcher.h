#pragma once
#include <context/context.hpp>
#include <filesystem/syncer.h>
#include <ospp/event.h>

#include <deque>
#include <functional>
#include <mutex>

namespace unravel
{

/// Callback invoked after each compilation job completes during a blocking wait.
using on_wait_progress_t = std::function<void(size_t completed, size_t total, const std::string& current_job)>;

class asset_watcher
{
public:
    asset_watcher();
    ~asset_watcher();

    auto init(rtti::context& ctx, const on_wait_progress_t& on_progress = nullptr) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    void watch_assets(rtti::context& ctx,
                      const std::string& protocol,
                      bool wait = false,
                      const on_wait_progress_t& on_progress = nullptr);
    void unwatch_assets(rtti::context& ctx, const std::string& protocol);

    void recreate_meta_files(rtti::context& ctx, const std::string& protocol);

    auto get_pending_jobs_count(rtti::context& ctx) const -> size_t;

private:
    void on_os_event(rtti::context& ctx, os::event& e);

    void setup_directory(rtti::context& ctx, fs::syncer& syncer);
    void setup_meta_syncer(rtti::context& ctx,
                           std::vector<uint64_t>& watchers,
                           fs::syncer& syncer,
                           const fs::path& data_dir,
                           const fs::path& meta_dir,
                           bool wait,
                           const on_wait_progress_t& on_progress);
    void setup_cache_syncer(rtti::context& ctx,
                            std::vector<uint64_t>& watchers,
                            fs::syncer& syncer,
                            const fs::path& meta_dir,
                            const fs::path& cache_dir,
                            bool wait,
                            const on_wait_progress_t& on_progress);

    struct watched
    {
        fs::syncer meta_syncer;
        fs::syncer cache_syncer;
        std::vector<std::uint64_t> watchers;
    };

    std::map<std::string, watched> watched_protocols_{};
    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);

    std::mutex recreate_meta_files_queue_mutex_{};
    std::map<std::string, bool> recreate_meta_files_queue_{};

};
} // namespace unravel
