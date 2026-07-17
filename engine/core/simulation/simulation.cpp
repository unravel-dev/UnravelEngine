#include "simulation.h"
#include <algorithm>
#include <thread>
#include <limits>

namespace unravel
{

simulation::simulation()
{
    if(max_inactive_fps_ == 0)
    {
        max_inactive_fps_ = std::max(max_inactive_fps_, max_fps_);
    }
}

void simulation::run_one_frame(bool is_active)
{
    // perform waiting loop if maximum fps set
    auto max_fps = max_fps_;
    if(!is_active && max_fps > 0)
    {
        max_fps = std::min(max_inactive_fps_, max_fps);
    }

    const auto wait_begin = clock_t::now();
    duration_t elapsed = wait_begin - last_frame_timepoint_;
    if(max_fps > 0)
    {
        // Compute the target frame duration at the clock's native resolution
        // BEFORE dividing. std::chrono::duration's operator/ does integer
        // division on its stored representation, so `seconds(1) / 60` gives
        // `seconds(0)` and `1000ms / 60` gives `16ms` (62.5 FPS), both losing
        // the fractional part. Casting to duration_t (nanoseconds on
        // Windows/Linux) first turns the dividend into 1'000'000'000, after
        // which `/ 60 = 16'666'666 ns` ≈ 16.667 ms with sub-microsecond error.
        const duration_t target_duration =
            std::chrono::duration_cast<duration_t>(std::chrono::seconds(1)) / max_fps;

        // Two-stage wait: a single coarse sleep that leaves a small safety
        // margin to absorb OS scheduler jitter, then a yield-spin to the
        // exact deadline. Matches the pacing approach used by Unity/Unreal.
        // The margin trades a tiny amount of CPU for sub-millisecond timing
        // accuracy independent of the platform timer resolution.
        constexpr auto sleep_safety_margin = std::chrono::milliseconds(2);

        const auto deadline = last_frame_timepoint_ + target_duration;
        const auto coarse_until = deadline - sleep_safety_margin;

        if(clock_t::now() < coarse_until)
        {
            std::this_thread::sleep_until(coarse_until);
        }

        while(clock_t::now() < deadline)
        {
            std::this_thread::yield();
        }

        elapsed = clock_t::now() - last_frame_timepoint_;
    }

    if(elapsed < duration_t(0))
    {
        elapsed = duration_t(0);
    }

    const auto wait_end = clock_t::now();
    last_sleep_duration_ = wait_end - wait_begin;
    if(last_sleep_duration_ < duration_t(0))
    {
        last_sleep_duration_ = duration_t::zero();
    }

    last_frame_timepoint_ = wait_end;

    // if fps lower than minimum, clamp eplased time
    if(min_fps_ > 0)
    {
        const duration_t target_duration =
            std::chrono::duration_cast<duration_t>(std::chrono::seconds(1)) / min_fps_;
        if(elapsed > target_duration)
        {
            elapsed = target_duration;
        }
    }

    // perform time step smoothing
    if(smoothing_step_ > 0)
    {
        timestep_ = duration_t::zero();
        previous_timesteps_.push_back(elapsed);
        if(previous_timesteps_.size() > smoothing_step_)
        {
            auto begin = previous_timesteps_.begin();
            previous_timesteps_.erase(begin, begin + int(previous_timesteps_.size() - smoothing_step_));
            for(auto step : previous_timesteps_)
            {
                timestep_ += step;
            }
            timestep_ /= static_cast<duration_t::rep>(previous_timesteps_.size());
        }
        else
        {
            timestep_ = previous_timesteps_.back();
        }
    }
    else
    {
        timestep_ = elapsed;
    }

    ++frame_;
}

void simulation::reset_delta_clock()
{
    previous_timesteps_.clear();
    timestep_ = duration_t::zero();
    last_sleep_duration_ = duration_t::zero();
    last_frame_timepoint_ = clock_t::now();
}

auto simulation::get_frame() const -> uint64_t
{
    return frame_;
}

void simulation::set_min_fps(uint32_t fps)
{
    min_fps_ = std::max<uint32_t>(fps, 0);
}

void simulation::set_max_fps(uint32_t fps)
{
    max_fps_ = std::max<uint32_t>(fps, 0);
}

void simulation::set_max_inactive_fps(uint32_t fps)
{
    max_inactive_fps_ = std::max<uint32_t>(fps, 0);
}

void simulation::set_time_smoothing_step(uint32_t step)
{
    smoothing_step_ = step;
}

auto simulation::get_time_since_launch() const -> duration_t
{
    return clock_t::now() - launch_timepoint_;
}

auto simulation::get_fps() const -> float
{
    auto dt = std::chrono::duration_cast<std::chrono::duration<float, std::milli>>(get_delta_time()).count();
    return (std::abs(dt) < std::numeric_limits<float>::epsilon()) ? 0 : 1000.0f / dt;
}

auto simulation::get_delta_time() const -> delta_t
{
    auto dt = std::chrono::duration_cast<delta_t>(timestep_) * time_scale_;
    return dt;
}

void simulation::set_time_scale(float time_scale)
{
    time_scale_ = time_scale;
}

auto simulation::get_time_scale() const -> float
{
    return time_scale_;
}

auto simulation::get_max_fps() const -> uint32_t
{
    return max_fps_;
}

auto simulation::get_last_sleep_duration() const -> duration_t
{
    return last_sleep_duration_;
}
} // namespace unravel
