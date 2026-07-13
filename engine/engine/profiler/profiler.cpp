#include "profiler.h"

#include <base/platform/process_memory.hpp>
#include <graphics/graphics.h>
#include <dotnetpp/dotnetpp.h>

#include <sstream>
#include <thread>

namespace unravel
{

auto get_app_profiler() -> performance_profiler*
{
    static performance_profiler profiler;
    return &profiler;
}

auto ensure_thread_registered(const char* thread_name) -> thread_profile_data*
{
    auto* profiler = get_app_profiler();
    if(!profiler)
    {
        return nullptr;
    }

    if(thread_name != nullptr && thread_name[0] != '\0')
    {
        return profiler->register_thread(thread_name);
    }

    std::ostringstream oss;
    oss << "Thread-" << std::this_thread::get_id();
    return profiler->register_thread(oss.str());
}

auto performance_profiler::register_thread_unlocked(const std::string& name) -> thread_profile_data*
{
    auto thread_index = static_cast<uint16_t>(threads_.size());
    auto data = std::make_unique<thread_profile_data>();
    data->buffers[0].thread_index = thread_index;
    data->buffers[1].thread_index = thread_index;

    auto* raw_ptr = data.get();

    threads_.push_back({name, std::move(data)});
    return raw_ptr;
}

auto performance_profiler::register_thread(const std::string& name) -> thread_profile_data*
{
    std::lock_guard lock(registration_mutex_);
    return register_thread_unlocked(name);
}

void performance_profiler::add_record(hpp::string_view name, float time_ms)
{
    if(time_ms < 0.0f)
    {
        return;
    }

    const int64_t dur_ns = static_cast<int64_t>(time_ms * 1'000'000.0f);
    const int64_t end_ns = get_time_ns();
    const int64_t start_ns = end_ns - dur_ns;

    const uint32_t idx = profile_begin_owned(name);
    if(idx == UINT32_MAX)
    {
        return;
    }

    profile_end(idx);

    auto* data = get_thread_profile_data();
    if(!data)
    {
        return;
    }

    auto& ev = data->write_buffer().events[idx];
    ev.start_ns = start_ns;
    ev.end_ns = end_ns;
    ev.cpu_start_ns = start_ns;
    ev.cpu_end_ns = end_ns;
}

void performance_profiler::swap()
{
    prev_frame_start_ns_ = frame_start_ns_;
    prev_frame_end_ns_ = get_time_ns();

    for(auto& [n, data] : aggregate_data_)
    {
        (void)n;
        data.reset();
    }

    {
        std::lock_guard lock(registration_mutex_);
        for(auto& thread : threads_)
        {
            thread.data->flip();
        }
    }

    {
        std::lock_guard lock(registration_mutex_);
        for(const auto& thread : threads_)
        {
            auto& buf = thread.data->read_buffer();
            for(uint32_t i = 0; i < buf.count; ++i)
            {
                auto& ev = buf.events[i];
                if(ev.end_ns <= ev.start_ns)
                {
                    continue;
                }

                float ms = static_cast<float>(ev.end_ns - ev.start_ns) / 1'000'000.0f;
                auto name_view = hpp::string_view(ev.name());
                auto it = aggregate_data_.find(name_view);
                if(it != aggregate_data_.end())
                {
                    it->second.add_sample(ms);
                }
                else
                {
                    per_frame_data pfd;
                    pfd.add_sample(ms);
                    aggregate_data_.emplace(std::string(ev.name()), pfd);
                }
            }
        }
    }

    if(recording_state_ == recording_state::recording)
    {
        capture_frame_snapshot();
    }

    frame_start_ns_ = get_time_ns();
}

void performance_profiler::capture_frame_snapshot()
{
    frame_snapshot snapshot;
    snapshot.frame_start_ns = prev_frame_start_ns_;
    snapshot.frame_end_ns = prev_frame_end_ns_;

    std::lock_guard lock(registration_mutex_);
    snapshot.threads.reserve(threads_.size());

    int64_t emin = INT64_MAX;
    int64_t emax = INT64_MIN;

    for(const auto& thread : threads_)
    {
        auto& buf = thread.data->read_buffer();
        if(buf.count == 0)
        {
            continue;
        }

        frame_snapshot::thread_snapshot ts;
        ts.thread_index = buf.thread_index;
        ts.name = thread.name;
        ts.events.reserve(buf.count);

        for(uint32_t i = 0; i < buf.count; ++i)
        {
            const auto& src = buf.events[i];
            ts.events.push_back(src);

            if(src.end_ns > src.start_ns)
            {
                emin = std::min(emin, src.start_ns);
                emax = std::max(emax, src.end_ns);
            }
        }

        snapshot.threads.push_back(std::move(ts));
    }

    snapshot.event_min_ns = (emin <= emax) ? emin : snapshot.frame_start_ns;
    snapshot.event_max_ns = (emin <= emax) ? emax : snapshot.frame_end_ns;

    snapshot.cpu_heap_used_bytes = dotnet::gc_get_used_size();
    snapshot.process_resident_bytes = platform::get_process_resident_set_bytes();
    if(const auto* st = gfx::get_stats())
    {
        snapshot.gpu_memory_used_bytes = st->gpuMemoryUsed;
    }

    if(snapshot.frame_start_ns <= 0 && emin <= emax)
    {
        snapshot.frame_start_ns = emin;
    }

    if(frame_history_.size() < max_frame_history)
    {
        frame_history_.push_back(std::move(snapshot));
        history_count_ = static_cast<uint32_t>(frame_history_.size());
        history_write_idx_ = history_count_ % max_frame_history;
    }
    else
    {
        frame_history_[history_write_idx_] = std::move(snapshot);
        history_write_idx_ = (history_write_idx_ + 1) % max_frame_history;
        history_count_ = max_frame_history;
    }
}

auto performance_profiler::get_per_frame_data_read() const -> const record_data_t&
{
    return aggregate_data_;
}

auto performance_profiler::get_threads() const -> const std::vector<thread_info>&
{
    return threads_;
}

auto performance_profiler::get_frame_start_ns() const -> int64_t
{
    return prev_frame_start_ns_;
}

auto performance_profiler::get_frame_end_ns() const -> int64_t
{
    return prev_frame_end_ns_;
}

void performance_profiler::sync_capture_active_to_threads()
{
    const uint8_t v = (recording_state_ == recording_state::recording) ? 1u : 0u;
    profiler_process_capture_gate_store(v);
}

void performance_profiler::set_recording_state(recording_state state)
{
    if(state == recording_state::stopped)
    {
        clear_history();
    }
    recording_state_ = state;
    std::lock_guard lock(registration_mutex_);
    sync_capture_active_to_threads();
}

auto performance_profiler::get_recording_state() const -> recording_state
{
    return recording_state_;
}

auto performance_profiler::get_frame_count() const -> uint32_t
{
    return history_count_;
}

auto performance_profiler::get_frame_snapshot(uint32_t index) const -> const frame_snapshot*
{
    if(index >= history_count_)
    {
        return nullptr;
    }

    if(history_count_ < max_frame_history)
    {
        return &frame_history_[index];
    }

    uint32_t actual = (history_write_idx_ + index) % max_frame_history;
    return &frame_history_[actual];
}

void performance_profiler::set_selected_frame(int32_t index)
{
    selected_frame_ = index;
}

auto performance_profiler::get_selected_frame() const -> int32_t
{
    return selected_frame_;
}

void performance_profiler::clear_history()
{
    frame_history_.clear();
    history_write_idx_ = 0;
    history_count_ = 0;
    selected_frame_ = -1;
}

} // namespace unravel
