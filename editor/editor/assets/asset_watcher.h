#pragma once
#include <context/context.hpp>
#include <filesystem/syncer.h>
#include <ospp/event.h>

#include <deque>
#include <functional>
#include <mutex>

namespace unravel
{

/// Called while watch_assets indexes the assets and waits for their jobs: completed of total, the
/// step it is in ("Indexing asset metadata", or the kind of job waited on) and the item of that step
/// (an asset path), which may be empty.
using on_wait_progress_t =
    std::function<void(size_t completed, size_t total, const std::string& step, const std::string& item)>;

class asset_watcher
{
public:
    asset_watcher();
    ~asset_watcher();

    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    void watch_assets(rtti::context& ctx,
                      const std::string& protocol,
                      bool wait = false,
                      const on_wait_progress_t& on_progress = nullptr);
    void unwatch_assets(rtti::context& ctx, const std::string& protocol);

    void recreate_meta_files(rtti::context& ctx, const std::string& protocol);

    /// Asset compiles and dependency checks queued or running on the thread pool, including the
    /// script library build. Zero means the compiled cache matches the sources as far as they are known.
    auto get_pending_compile_jobs_count(rtti::context& ctx) const -> size_t;

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
