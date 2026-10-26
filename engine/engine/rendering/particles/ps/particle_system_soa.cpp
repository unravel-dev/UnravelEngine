#include "particle_system_soa.h"

#include "particle_bounds.h"
#include "particle_draw_state.h"
#include "particle_gpu.h"
#include "particle_noise.h"
#include "particle_luts.h"
#include "particle_scale_curve.h"
#include "particle_shapes.h"
#include "particle_slots.h"

#include <bx/allocator.h>
#include <concurrency/parallel.h>
#include <bx/handlealloc.h>
#include <bx/rng.h>
#include <engine/profiler/profiler.h>
#include <graphics/graphics.h>
#include <hpp/small_vector.hpp>
#include <logging/logging.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

namespace unravel
{
namespace ps_soa
{
namespace
{

constexpr float k_standard_gravity = 9.81f;
constexpr float k_min_particle_lifespan = 1.0e-3f;
/// Seconds of a particle's path one collision search step covers, with the fewest and most steps per path, and
/// the halvings that pin a crossing down.
constexpr double k_contact_step_seconds = 1.0 / 30.0;
constexpr uint32_t k_min_contact_search_steps = 24;
constexpr uint32_t k_max_contact_search_steps = 256;
constexpr uint32_t k_contact_refine_steps = 16;
/// Smallest speed factor (speed curve times easing rate) a bounce divides by when it carries a particle's curves
/// on past its contact; below it the bounced velocity is kept as it is.
constexpr float k_min_segment_factor = 1.0e-2f;
/// Below this scale determinant the emitter's basis cannot be inverted (a local-space emitter scaled to zero).
constexpr float k_min_basis_determinant = 1.0e-12f;
/// Metres a bounced particle starts off its plane, so its next path does not cross it at once.
constexpr float k_contact_lift = 1.0e-3f;
/// Salts of an emitter's place in the turbulence noise, drawn from its seed.
constexpr uint32_t k_noise_offset_salt = 101u;
/// Shortest time between the firings of one burst, seconds.
constexpr float k_min_burst_interval = 0.01f;
/// Most emission cycles one update fires bursts for (a long catch-up of a short looping cycle).
constexpr uint32_t k_max_burst_cycles_per_update = 64;
/// Longest clock step, in seconds, the motion vectors take as one frame's motion.
constexpr float k_max_motion_frame_time = 0.1f;
constexpr float k_degrees_to_radians = 3.14159265358979f / 180.0f;
constexpr uint32_t k_seed_mix = 0x9e3779b9u;
/// The emitter clock starts here, one longest lifetime in: a particle born before the clock started (prewarm)
/// still gets its exact birth tick.
constexpr double k_clock_origin_seconds = double(max_particle_lifetime);
/// Emitter time before which a rate-spawned particle is certainly dead is skipped, not spawned.
constexpr double k_spawn_skip_margin = 1.0e-6;
/// Slack for an emission time that lands on a whole number of particle intervals.
constexpr double k_spawn_count_epsilon = 1.0e-6;

particle_sort_backend g_default_sort_backend = particle_sort_backend::gpu;
std::atomic<uint32_t> g_seed_counter{0};

auto make_seed(uint32_t value) -> uint32_t
{
    return particle_hash(value ^ k_seed_mix);
}

particle_quality g_quality;

/// A seed no other emitter of this run starts from.
auto fresh_seed() -> uint32_t
{
    static const uint32_t run_salt =
        uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()) ^ uint32_t(reinterpret_cast<uintptr_t>(&g_seed_counter));
    return make_seed(run_salt + g_seed_counter.fetch_add(1) * k_seed_mix);
}

struct pending_burst
{
    uint32_t count = 0;
    emit_params params{};
};

/// What every particle spawned in one update shares.
struct spawn_frame
{
    math::mat4 transform{1.0f};
    math::mat3 basis{1.0f};
    math::vec3 previous_position{0.0f};
    math::vec3 position{0.0f};
    math::vec3 emitter_velocity{0.0f};
    float lifetime_multiplier = 1.0f;
    /// Seconds of the update's window, its catch-up part (spawned where the emitter is now) first.
    float window = 0.0f;
    float catch_up = 0.0f;
};

class emitter
{
public:
    void create(uint16_t index, uint32_t capacity, particle_gpu& gpu)
    {
        index_ = index;
        capacity_ = capacity;
        pool_base_ = gpu.reserve_range(capacity);
        records_.assign(capacity, particle_record{});
        bounce_counts_.assign(capacity, 0u);
        contact_planes_.assign(capacity, 0u);
        staged_.assign(capacity, 0u);
        staged_slots_.clear();
        slots_.reset(capacity);
        deaths_.clear();
        contacts_.clear();
        events_.clear();
        history_.clear();
        sim_.reset();
        reset_clock();
        has_lut_ = false;
        lut_key_ = 0;
        lut_base_ = 0;
        luts_ = nullptr;
        params_ = emitter_eval_params{};
        render_desc_ = emitter_render_desc{};
        static_row_ = static_row{};
        cutout_table_ = 0.0f;
        has_seed_ = false;
        backend_override_ = particle_sort_backend::gpu;
        has_backend_override_ = false;
        pending_bursts_.clear();
        static_dirty_ = true;
        is_live_ = true;
    }

    void destroy(particle_gpu& gpu)
    {
        if(has_lut_)
        {
            gpu.release_lut(lut_key_);
            has_lut_ = false;
        }
        luts_ = nullptr;
        set_cutout(gpu, 0);
        gpu.release_range(pool_base_, capacity_);
        pool_base_ = particle_gpu::invalid_base;
        records_.clear();
        records_.shrink_to_fit();
        staged_.clear();
        staged_slots_.clear();
        deaths_.clear();
        history_.clear();
        is_live_ = false;
    }

    void reset()
    {
        sim_.reset();
        // The clock restarts with the particles, so a fixed seed replays them exactly.
        reset_clock();
        pending_bursts_.clear();
        slots_.reset(capacity_);
        deaths_.clear();
        contacts_.clear();
        events_.clear();
        history_.clear();
        std::fill(staged_.begin(), staged_.end(), 0u);
        staged_slots_.clear();
        // A fixed seed replays from its start; a free one starts a fresh sequence.
        has_seed_ = false;
    }

    void update(float dt,
                const emitter_desc& desc,
                emitter_transform_state& transform,
                emitter_playback_desc& playback,
                particle_gpu& gpu)
    {
        if(sim_.first_update)
        {
            transform.previous = transform.current;
        }
        apply_seed(desc);
        if(playback.playing && !sim_.playing)
        {
            restart_cycle();
        }
        sim_.playing = playback.playing;
        const bool is_paused = playback.paused;
        const float sim_dt = is_paused ? 0.0f : math::max(dt, 0.0f);
        const float catch_up = is_paused ? 0.0f : sim_.frozen_time;
        sim_.frozen_time = 0.0f;
        if(!is_paused)
        {
            sim_.push_temporal_sample(transform.current.get_position(), sim_dt);
        }
        // The clock moves first and the parameters follow it, so contacts and deaths up to now are settled
        // (and their events placed) with this frame's transform.
        advance_clock(double(sim_dt) + double(catch_up));
        const emitter_feature features = cap_features(desc.bake_features());
        sim_.features = features;
        refresh_luts(desc, features, gpu);
        bake_params(desc, features, transform);
        settle_particles(desc);
        history_.expire(clock_seconds_);
        const spawn_frame frame = make_spawn_frame(desc, features, transform, sim_dt, catch_up);
        if(sim_.prewarm_pending && playback.playing && !is_paused)
        {
            sim_.prewarm_pending = false;
            if(desc.emission.prewarm && desc.emission.loop)
            {
                prewarm(desc, frame);
            }
        }
        if(playback.playing && !is_paused)
        {
            emit_cycle(desc, frame);
        }
        emit_bursts(desc, frame);
        // Particles born this frame may already have reached a plane or died.
        settle_particles(desc);
        if(!desc.emission.loop && sim_.emission_finished && get_live_count() == 0 && pending_bursts_.empty())
        {
            playback.playing = false;
            sim_.playing = false;
        }
        update_bounds(desc, features, transform);
        sim_.first_update = false;
        transform.previous = transform.current;
    }

    void update_frozen(float dt, const emitter_desc& desc, emitter_transform_state& transform, bool catch_up)
    {
        // A paused emitter has no time to catch up on.
        if(catch_up && sim_.playing && !desc.playback.paused)
        {
            sim_.frozen_time += math::max(dt, 0.0f);
        }
        const emitter_feature features = cap_features(desc.bake_features());
        sim_.features = features;
        bake_params(desc, features, transform);
        update_bounds(desc, features, transform);
        transform.previous = transform.current;
    }

    void queue_burst(uint32_t count, const emit_params& params)
    {
        uint32_t queued = 0;
        for(const pending_burst& burst : pending_bursts_)
        {
            queued += burst.count;
        }
        // Bursts past what the emitter holds would be dropped when they spawn; an emitter that is not updated
        // (its entity inactive) would gather them without end.
        const uint32_t room = capacity_ > queued ? capacity_ - queued : 0u;
        count = math::min(count, room);
        if(count > 0)
        {
            pending_bursts_.push_back({count, params});
        }
    }

    void take_events(std::vector<particle_event>& out_events)
    {
        out_events.insert(out_events.end(), events_.begin(), events_.end());
        events_.clear();
    }

    auto has_pending_bursts() const -> bool
    {
        return !pending_bursts_.empty();
    }

    auto has_updated() const -> bool
    {
        return !sim_.first_update;
    }

    auto get_bounds() const -> const math::bbox&
    {
        return sim_.world_bounds;
    }

    auto get_live_count() const -> uint32_t
    {
        return slots_.get_used_count();
    }

    /// Points the emitter at the cutout table of @p key (0: none), releasing the one it held. Main thread.
    void set_cutout(particle_gpu& gpu, uint64_t key)
    {
        if(key == cutout_key_)
        {
            return;
        }
        if(cutout_key_ != 0)
        {
            gpu.release_cutout(cutout_key_);
        }
        cutout_key_ = key;
        if(key != 0)
        {
            gpu.acquire_cutout(key);
        }
    }

    auto get_cutout_key() const -> uint64_t
    {
        return cutout_key_;
    }

    /// The capacity spawns may fill: the emitter's own, scaled by the quality (at least one particle).
    auto get_spawn_capacity() const -> uint32_t
    {
        const float scale = math::clamp(g_quality.max_particles_scale, 0.0f, 1.0f);
        return math::max(uint32_t(float(capacity_) * scale + 0.5f), math::min(capacity_, 1u));
    }

    /// @p features with the lighting the quality allows.
    static auto cap_features(emitter_feature features) -> emitter_feature
    {
        const bool is_surface = has_feature(features, emitter_feature::lit_surface);
        const bool is_volumetric = has_feature(features, emitter_feature::lit_volumetric);
        if(!is_surface && !is_volumetric)
        {
            return features;
        }
        const particle_lighting lighting =
            get_capped_lighting(is_surface ? particle_lighting::surface : particle_lighting::volumetric);
        features = emitter_feature(uint32_t(features) &
                                   ~uint32_t(emitter_feature::lit_surface | emitter_feature::lit_volumetric));
        if(lighting == particle_lighting::surface)
        {
            return features | emitter_feature::lit_surface;
        }
        return lighting == particle_lighting::volumetric ? features | emitter_feature::lit_volumetric : features;
    }

    auto get_high_water() const -> uint32_t
    {
        return slots_.get_high_water();
    }

    auto get_pool_base() const -> uint32_t
    {
        return pool_base_;
    }

    auto get_params() const -> const emitter_eval_params&
    {
        return params_;
    }

    /// The emitter's rotation one frame back, for motion vectors of what turns with it.
    auto get_previous_emitter_rotation() const -> const math::quat&
    {
        return previous_emitter_rotation_;
    }

    auto get_luts() const -> const lut_set*
    {
        return luts_;
    }

    auto get_record(uint32_t slot) const -> const particle_record&
    {
        return records_[slot];
    }

    auto get_render_desc() const -> const emitter_render_desc&
    {
        return render_desc_;
    }

    auto is_live() const -> bool
    {
        return is_live_;
    }

    void set_backend_override(particle_sort_backend backend)
    {
        backend_override_ = backend;
        has_backend_override_ = true;
    }

    auto resolve_backend(bool can_sort) const -> particle_sort_backend
    {
        if(!can_sort)
        {
            return particle_sort_backend::cpu;
        }
        return has_backend_override_ ? backend_override_ : g_default_sort_backend;
    }

    /// Hands this frame's new particles and rows to the GPU (main thread).
    void sync(particle_gpu& gpu, bool restage_all)
    {
        if(!is_live_ || pool_base_ == particle_gpu::invalid_base)
        {
            return;
        }
        if(restage_all)
        {
            staged_slots_.clear();
            const uint32_t high_water = slots_.get_high_water();
            for(uint32_t slot = 0; slot < high_water; ++slot)
            {
                staged_[slot] = 1u;
                staged_slots_.push_back(slot);
            }
            static_dirty_ = true;
        }
        for(uint32_t slot : staged_slots_)
        {
            gpu.stage(pool_base_ + slot, records_[slot]);
            staged_[slot] = 0u;
        }
        staged_slots_.clear();
        // The cutout table comes in from a worker's bake, between updates: patched into the row here.
        const float cutout_table = cutout_key_ != 0 ? gpu.get_cutout_table(cutout_key_) : 0.0f;
        if(cutout_table != cutout_table_)
        {
            cutout_table_ = cutout_table;
            static_row_[11].w = cutout_table;
            static_dirty_ = true;
        }
        if(static_dirty_)
        {
            gpu.set_static_row(index_, static_row_);
            static_dirty_ = false;
        }
        gpu.set_dynamic_row(index_, pack_dynamic_row(params_));
    }

    /// Live particles at the emitter's current time, in slot order.
    template<typename Visitor>
    void for_each_live(Visitor&& visit) const
    {
        if(!luts_)
        {
            return;
        }
        const uint32_t high_water = slots_.get_high_water();
        for(uint32_t slot = 0; slot < high_water; ++slot)
        {
            if(!slots_.is_used(slot))
            {
                continue;
            }
            const particle_record& record = records_[slot];
            const particle_state state = evaluate_particle(record, params_, *luts_);
            if(state.is_alive)
            {
                visit(slot, record, state);
            }
        }
    }

private:
    void apply_seed(const emitter_desc& desc)
    {
        const bool wants_fixed = desc.emission.use_fixed_seed;
        if(has_seed_ && wants_fixed == seed_is_fixed_ && (!wants_fixed || desc.emission.random_seed == fixed_seed_))
        {
            return;
        }
        seed_is_fixed_ = wants_fixed;
        fixed_seed_ = desc.emission.random_seed;
        seed_ = wants_fixed ? make_seed(desc.emission.random_seed) : fresh_seed();
        rng_.reset(seed_ | 1u, make_seed(seed_) | 1u);
        spawn_counter_ = 0;
        has_seed_ = true;
    }

    void restart_cycle()
    {
        sim_.cycle_time = 0.0f;
        sim_.start_delay_elapsed = 0.0f;
        sim_.emission_finished = false;
        sim_.emission_time_accum = 0.0f;
    }

    void reset_clock()
    {
        clock_seconds_ = k_clock_origin_seconds;
        now_ticks_ = seconds_to_ticks(clock_seconds_);
    }

    void advance_clock(double seconds)
    {
        clock_seconds_ += seconds;
        now_ticks_ = seconds_to_ticks(clock_seconds_);
    }

    /// The full clock tick of the 32-bit @p birth (records keep the low bits; ages are short).
    auto get_birth_tick(uint32_t birth) const -> uint64_t
    {
        return now_ticks_ - uint64_t(uint32_t(now_ticks_) - birth);
    }

    /// Bounces and contacts up to now, then deaths: a particle's contact always comes before its death.
    void settle_particles(const emitter_desc& desc)
    {
        contacts_.pop_dead(now_ticks_,
                           [&](const particle_death_queue::entry& contact)
                           {
                               handle_contact(desc, contact.slot, contact.death_tick);
                           });
        deaths_.pop_dead(now_ticks_,
                         [&](const particle_death_queue::entry& dead)
                         {
                             // A particle a contact killed early, or whose slot was reused, has a stale entry.
                             const particle_record& record = records_[dead.slot];
                             if(!slots_.is_used(dead.slot) || uint32_t(dead.death_tick) != record.birth + record.lifespan)
                             {
                                 return;
                             }
                             record_event(desc, particle_event_type::death, record, dead.death_tick);
                             slots_.release(dead.slot);
                         });
    }

    /// The particle's state when the clock reads @p tick (its current record).
    auto evaluate_at(const particle_record& record, uint64_t tick) const -> particle_state
    {
        emitter_eval_params params = params_;
        params.now = uint32_t(tick);
        return evaluate_particle(record, params, *luts_);
    }

    /// Records @p type for the particle in @p record at @p tick when a sub-emitter listens for it.
    void record_event(const emitter_desc& desc, particle_event_type type, const particle_record& record, uint64_t tick)
    {
        if((desc.event_mask & static_cast<uint32_t>(type)) == 0u || !luts_)
        {
            return;
        }
        emitter_eval_params params = params_;
        params.now = uint32_t(tick);
        const particle_state state = evaluate_particle(record, params, *luts_);
        particle_event event;
        event.type = type;
        event.position = state.position;
        event.velocity = state.velocity;
        event.color = evaluate_particle_color(record, state, params, *luts_);
        const float intensity = math::max(params.intensity, 1.0e-4f);
        event.color = math::vec4(math::vec3(event.color) / intensity, event.color.a);
        event.size = evaluate_particle_size(record, state, params, *luts_);
        events_.push_back(event);
    }

    /// The signed distance of @p position to collision plane @p plane (positive on the open side).
    static auto plane_distance(const math::vec4& plane, const math::vec3& position, const math::vec3& origin) -> float
    {
        return math::dot(math::vec3(plane), position - origin) - plane.w;
    }

    /// The first tick the particle's current path crosses a collision plane from its open side, before it dies;
    /// 0 when it never does. @p out_plane receives the plane's index.
    auto find_contact(const emitter_desc& desc, const particle_record& record, uint32_t& out_plane) const -> uint64_t
    {
        const auto& planes = desc.motion.collision_planes;
        if(desc.motion.collision == collision_mode::none || planes.empty() || !luts_ ||
           desc.motion.space != simulation_space::world)
        {
            return 0;
        }
        const math::vec3 origin(params_.local_to_world[3]);
        const uint64_t birth = get_birth_tick(record.birth);
        const uint64_t start = birth + record.motion_offset;
        const uint64_t end = birth + record.lifespan;
        if(end <= start)
        {
            return 0;
        }
        // The particle touches a plane with its edge: half its size (at the start of this path) from it, and never
        // further than it starts, so one born or bounced closer than that still touches instead of passing through.
        const particle_state start_state = evaluate_at(record, start);
        const float radius = 0.5f * math::max(desc.motion.collision_radius_scale, 0.0f) *
                             evaluate_particle_size(record, start_state, params_, *luts_);
        hpp::small_vector<float, 8> radii;
        for(const auto& plane : planes)
        {
            radii.push_back(math::clamp(plane_distance(plane, start_state.position, origin), 0.0f, radius));
        }
        const auto distance_at = [&](uint32_t plane, uint64_t tick)
        {
            return plane_distance(planes[plane], evaluate_at(record, tick).position, origin) - radii[plane];
        };
        // Steps of a fixed duration, so a long life is searched as finely as a short one.
        const double seconds = double(end - start) / ticks_per_second;
        const uint32_t steps = uint32_t(math::clamp(std::ceil(seconds / k_contact_step_seconds),
                                                    double(k_min_contact_search_steps),
                                                    double(k_max_contact_search_steps)));
        uint64_t previous_tick = start;
        for(uint32_t step = 1; step <= steps; ++step)
        {
            const uint64_t tick = start + (end - start) * step / steps;
            // Several planes may be crossed within one step: the earliest crossing is the contact.
            uint64_t contact = 0;
            for(uint32_t plane = 0; plane < uint32_t(planes.size()); ++plane)
            {
                if(distance_at(plane, previous_tick) < 0.0f || distance_at(plane, tick) >= 0.0f)
                {
                    continue;
                }
                // Halve the step down to the crossing.
                uint64_t open = previous_tick;
                uint64_t closed = tick;
                for(uint32_t refine = 0; refine < k_contact_refine_steps && closed - open > 1; ++refine)
                {
                    const uint64_t middle = open + (closed - open) / 2;
                    (distance_at(plane, middle) >= 0.0f ? open : closed) = middle;
                }
                if(contact == 0 || closed < contact)
                {
                    contact = closed;
                    out_plane = plane;
                }
            }
            if(contact != 0)
            {
                return contact;
            }
            previous_tick = tick;
        }
        return 0;
    }

    /// Schedules the slot's next contact (the kill mode ends its life there).
    void schedule_contact(const emitter_desc& desc, uint32_t slot)
    {
        particle_record& record = records_[slot];
        uint32_t plane = 0;
        const uint64_t contact = find_contact(desc, record, plane);
        if(contact == 0)
        {
            return;
        }
        contact_planes_[slot] = uint8_t(plane);
        if(desc.motion.collision == collision_mode::kill)
        {
            record.lifespan = math::max(uint32_t(contact - get_birth_tick(record.birth)), 1u);
        }
        contacts_.push(contact, slot);
    }

    /// The factor the speed curve and the easing apply to a particle's velocity at @p life (1 without them).
    auto get_motion_factor(float life) const -> float
    {
        float factor = 1.0f;
        float progress = math::clamp(life, 0.0f, 1.0f);
        if(has_feature(sim_.features, emitter_feature::non_linear_ease))
        {
            const math::vec4 life_curves = luts_->sample(lut_row_life, progress);
            progress = life_curves.x;
            factor *= life_curves.y;
        }
        if(has_feature(sim_.features, emitter_feature::speed_curve))
        {
            factor *= luts_->sample(lut_row_speed, progress).y;
        }
        return factor;
    }

    /// The slot's particle reaches its plane at @p tick: a collision event, then a bounce, or its death.
    void handle_contact(const emitter_desc& desc, uint32_t slot, uint64_t tick)
    {
        particle_record& record = records_[slot];
        const uint64_t birth = get_birth_tick(record.birth);
        if(!slots_.is_used(slot) || tick < birth + uint64_t(record.motion_offset) || tick > birth + record.lifespan ||
           !luts_)
        {
            return;
        }
        record_event(desc, particle_event_type::collision, record, tick);
        if(desc.motion.collision == collision_mode::kill)
        {
            return;
        }
        if(bounce_counts_[slot] >= desc.motion.max_bounces)
        {
            // Out of bounces: it dies at this contact instead of sinking through the plane.
            record.lifespan = math::max(uint32_t(tick - birth), 1u);
            record_event(desc, particle_event_type::death, record, tick);
            restage(slot);
            slots_.release(slot);
            return;
        }
        const particle_state state = evaluate_at(record, tick);
        const math::vec3 normal(desc.motion.collision_planes[contact_planes_[slot]]);
        const float into = math::dot(state.velocity, normal);
        const math::vec3 along = state.velocity - normal * into;
        const math::vec3 bounced = along * (1.0f - math::clamp(desc.motion.friction, 0.0f, 1.0f)) -
                                   normal * (into * math::clamp(desc.motion.restitution, 0.0f, 1.0f));
        // A new motion segment from the contact; the particle keeps its age, so its looks over life go on. Its
        // speed curve and easing go on too: they scale the segment's velocity, so that is the bounced velocity
        // divided by their factor at the contact.
        const float lifespan = float(double(record.lifespan) / ticks_per_second);
        const float segment_factor = get_motion_factor(state.age / lifespan);
        record.set_start(state.position + normal * k_contact_lift);
        record.set_velocity(math::abs(segment_factor) >= k_min_segment_factor ? bounced / segment_factor : bounced);
        record.motion_offset = uint32_t(tick - birth);
        ++bounce_counts_[slot];
        restage(slot);
        const float remaining = float(double(birth + record.lifespan - tick) / ticks_per_second);
        const float size_peak = luts_->ranges.size_peak;
        // The segment's curves run over the whole lifetime: its reach is the full lifetime's, at its speed.
        const float reach_speed =
            math::length(record.get_velocity()) * lifespan / math::max(remaining, k_min_particle_lifespan);
        history_.add(double(tick) / ticks_per_second, state.position, reach_speed, remaining,
                     size_peak * record.size_factor + record.size_offset);
        schedule_contact(desc, slot);
    }

    void restage(uint32_t slot)
    {
        if(staged_[slot] == 0u)
        {
            staged_[slot] = 1u;
            staged_slots_.push_back(slot);
        }
    }

    void refresh_luts(const emitter_desc& desc, emitter_feature features, particle_gpu& gpu)
    {
        const uint64_t key = compute_lut_key(desc, features);
        if(has_lut_ && key == lut_key_)
        {
            return;
        }
        const uint32_t block = gpu.acquire_lut(key,
                                               [&](lut_set& set)
                                               {
                                                   bake_lut_set(desc, features, set);
                                               });
        if(has_lut_)
        {
            gpu.release_lut(lut_key_);
        }
        lut_key_ = key;
        has_lut_ = true;
        lut_base_ = block * lut_set_vec4s;
        luts_ = gpu.get_lut(key);
    }

    void bake_params(const emitter_desc& desc, emitter_feature features, const emitter_transform_state& transform)
    {
        const math::vec3 scale = transform.current.get_scale();
        emitter_eval_params params;
        const scale_curve_widths widths = get_scale_curve_widths(desc.appearance.size_over_life);
        params.opacity = desc.appearance.opacity;
        params.intensity = desc.appearance.color_intensity;
        params.size_width_start = widths.start;
        params.size_width_end = widths.end;
        params.size_axes = desc.appearance.size_axes;
        params.lut_base = lut_base_;
        const frange_t size_speed = desc.appearance.size_by_speed_speed_range;
        const float size_span = size_speed.max - size_speed.min;
        params.size_by_speed = math::vec4(desc.appearance.size_by_speed_multiplier.min,
                                          desc.appearance.size_by_speed_multiplier.max,
                                          size_span > 0.0f ? 1.0f / size_span : 0.0f,
                                          size_speed.min);
        const frange_t color_speed = desc.appearance.color_by_speed_speed_range;
        const float color_span = color_speed.max - color_speed.min;
        params.color_by_speed = math::vec2(color_span > 0.0f ? 1.0f / color_span : 0.0f, color_speed.min);
        params.soft_fade_distance = math::max(desc.render.soft_fade_distance, 0.0f);
        params.render_mode = desc.render.render_mode;
        params.pivot = desc.render.pivot;
        params.tiles = math::max(desc.render.tex_sheet_tiles, math::vec2(1.0f));
        params.cycles = math::max(desc.render.tex_sheet_cycles, 0.0f);
        params.flags = static_cast<uint32_t>(features);
        params.rotation = math::vec2(desc.render.start_rotation.min, desc.render.start_rotation.max) * k_degrees_to_radians;
        params.spin = math::vec2(desc.render.spin_speed.min, desc.render.spin_speed.max) * k_degrees_to_radians;
        const math::vec2 pivot_offset = math::abs(desc.render.pivot - math::vec2(0.5f));
        // Half the quad's diagonal holds every spin angle; an off-centre pivot pushes it further out.
        params.pivot_pad = 1.0f + math::max(pivot_offset.x, pivot_offset.y) * 2.0f;
        params.sort_mode = desc.render.sort_mode;
        params.camera_offset = math::max(desc.render.camera_offset, 0.0f);
        params.camera_fade = math::vec2(desc.render.camera_fade_range.min, desc.render.camera_fade_range.max);
        params.max_screen_size = math::max(desc.render.max_screen_size, 0.0f);
        params.stretch_length_scale = desc.render.stretch_length_scale;
        params.trail_length = math::max(desc.render.trail_length, 0.0f);
        params.trail_width_end = math::max(desc.render.trail_width_end, 0.0f);
        params.ribbon_texture_mode = desc.render.ribbon_texture_mode;
        params.ribbon_tiles_per_second = 1.0f / math::max(desc.render.ribbon_tile_time, k_min_ribbon_tile_time);
        params.ribbon_facing = desc.render.ribbon_facing;
        params.stretch_speed_scale = desc.render.stretch_speed_scale;
        params.stretch_camera_velocity_scale = desc.render.stretch_camera_velocity_scale;
        params.facing_axis = desc.render.facing_axis;
        params.taa_response = math::clamp(desc.render.taa_response, 0.0f, 1.0f);
        params.velocity_alpha_threshold = math::clamp(desc.render.velocity_alpha_threshold, 0.0f, 1.0f);
        if(luts_)
        {
            params.speed_bound = math::vec2(luts_->ranges.speed_factor_max * luts_->ranges.ease_rate_max,
                                            luts_->ranges.ease_rate_max);
        }
        params.draw_distance = math::vec2(math::max(desc.render.max_draw_distance, 0.0f),
                                          math::max(desc.render.draw_distance_fade, 0.0f));
        params.receive_shadows = desc.render.receive_shadows ? 1.0f : 0.0f;
        params.lighting_wrap = math::clamp(desc.render.lighting_wrap, 0.0f, 1.0f);
        params.cutout_table = cutout_table_;
        if(has_feature(features, emitter_feature::turbulence))
        {
            // Noise tiles per metre / per second from features per metre / per second; a difference of two
            // unit-bounded samples moves a particle at most twice the strength.
            const float features_to_tiles = 1.0f / float(particle_noise_cells);
            const float peak = luts_ ? luts_->ranges.turbulence_peak : 1.0f;
            // A local-space emitter's scale stretches the field's push in the world.
            const float reach_scale = desc.motion.space == simulation_space::local ? get_max_axis_scale(scale) : 1.0f;
            params.turbulence = math::vec4(desc.motion.turbulence_strength,
                                           math::max(desc.motion.turbulence_frequency, 0.0f) * features_to_tiles,
                                           desc.motion.turbulence_speed * features_to_tiles,
                                           2.0f * desc.motion.turbulence_strength * peak * reach_scale);
            params.noise_offset = math::vec3(particle_random(seed_, k_noise_offset_salt),
                                             particle_random(seed_, k_noise_offset_salt + 1u),
                                             particle_random(seed_, k_noise_offset_salt + 2u));
        }
        params.local_to_world = transform.current;
        // Sizes follow the scale's magnitude: a mirrored emitter draws its particles at full size.
        const math::vec3 magnitude = math::abs(scale);
        params.system_scale = (magnitude.x + magnitude.y + magnitude.z) / 3.0f;
        const math::vec3 gravity(0.0f, -k_standard_gravity * desc.motion.gravity_scale, 0.0f);
        if(desc.motion.space == simulation_space::local)
        {
            params.emitter_rotation = transform.current.get_rotation();
            // Gravity pulls down in the world whatever the emitter's rotation; the force is authored in
            // the emitter's own space. A basis scaled flat has no inverse: its particles fall nowhere.
            const math::mat3 basis(params.local_to_world);
            const bool is_invertible = math::abs(glm::determinant(basis)) > k_min_basis_determinant;
            params.acceleration = (is_invertible ? glm::inverse(basis) * gravity : math::vec3(0.0f)) +
                                  desc.motion.acceleration;
        }
        else
        {
            params.emitter_rotation = math::identity<math::quat>();
            params.acceleration = gravity * magnitude.y + desc.motion.acceleration * scale;
        }
        params.drag = math::max(desc.motion.drag, 0.0f);
        params.now = uint32_t(now_ticks_);
        // Motion vectors cover what moved since the last frame: a restarted clock or a long catch-up is no
        // frame's motion.
        const uint32_t elapsed_ticks = params.now >= params_.now && !sim_.first_update ? params.now - params_.now : 0u;
        params.frame_time = math::min(float(double(elapsed_ticks) / ticks_per_second), k_max_motion_frame_time);
        previous_emitter_rotation_ = params.emitter_rotation;
        if(desc.motion.space == simulation_space::local)
        {
            params.origin_motion = transform.current.get_position() - transform.previous.get_position();
            previous_emitter_rotation_ = transform.previous.get_rotation();
        }
        params_ = params;
        render_desc_ = desc.render;
        const static_row row = pack_static_row(params);
        if(row != static_row_)
        {
            static_row_ = row;
            static_dirty_ = true;
        }
    }

    /// The largest of @p scale's axes, by magnitude.
    static auto get_max_axis_scale(const math::vec3& scale) -> float
    {
        const math::vec3 magnitude = math::abs(scale);
        return math::max(magnitude.x, math::max(magnitude.y, magnitude.z));
    }

    auto make_spawn_frame(const emitter_desc& desc,
                          emitter_feature features,
                          const emitter_transform_state& transform,
                          float sim_dt,
                          float catch_up) const -> spawn_frame
    {
        spawn_frame frame;
        frame.transform = transform.current;
        frame.basis = math::mat3(frame.transform);
        frame.position = transform.current.get_position();
        frame.previous_position = transform.previous.get_position();
        frame.window = sim_dt + catch_up;
        frame.catch_up = catch_up;
        if(sim_dt > 0.0f)
        {
            frame.emitter_velocity = (frame.position - frame.previous_position) / sim_dt;
        }
        if(has_feature(features, emitter_feature::lifetime_by_emitter_speed))
        {
            const frange_t range = desc.motion.lifetime_by_emitter_speed_range;
            const float speed = sim_.calculate_smoothed_emitter_speed(range.max);
            const float factor = math::clamp((speed - range.min) / (range.max - range.min), 0.0f, 1.0f);
            frame.lifetime_multiplier = desc.motion.lifetime_by_emitter_speed_curve.sample(factor);
        }
        return frame;
    }

    auto get_max_lifetime(const emitter_desc& desc, const spawn_frame& frame) const -> float
    {
        return math::clamp(desc.motion.lifetime.max * frame.lifetime_multiplier, k_min_particle_lifespan, max_particle_lifetime);
    }

    /// Starts a looping emitter in its steady state: the particles one maximum lifetime of emission
    /// leaves alive, born at their exact times as if the emitter had held still.
    void prewarm(const emitter_desc& desc, const spawn_frame& frame)
    {
        spawn_frame still = frame;
        still.previous_position = frame.position;
        const float window = get_max_lifetime(desc, frame);
        still.window = window;
        still.catch_up = window;
        float accumulator = 0.0f;
        spawn_rate_window(desc, still, 0.0f, window, accumulator);
    }

    /// The update's emission: start delay, then a looping emitter emits throughout and a one-shot one until
    /// its cycle ends - at the rate, in the authored bursts and per metre the emitter moves.
    void emit_cycle(const emitter_desc& desc, const spawn_frame& frame)
    {
        const float duration = desc.emission.duration;
        if(duration <= 0.0f || frame.window <= 0.0f)
        {
            return;
        }
        const float delay_left = math::max(desc.emission.start_delay - sim_.start_delay_elapsed, 0.0f);
        sim_.start_delay_elapsed += frame.window;
        if(delay_left >= frame.window)
        {
            return;
        }
        float window_start = delay_left;
        float window_end = frame.window;
        if(!desc.emission.loop)
        {
            if(sim_.emission_finished)
            {
                return;
            }
            const float remaining = duration - sim_.cycle_time;
            window_end = math::min(window_end, window_start + math::max(remaining, 0.0f));
            if(sim_.cycle_time + (window_end - window_start) >= duration)
            {
                sim_.emission_finished = true;
            }
        }
        const float cycle_start = sim_.cycle_time;
        const float emitted = window_end - window_start;
        sim_.cycle_time = desc.emission.loop ? std::fmod(sim_.cycle_time + emitted, duration) : sim_.cycle_time + emitted;
        if(get_rate(desc) > 0.0f)
        {
            spawn_rate_window(desc, frame, window_start, window_end, sim_.emission_time_accum);
        }
        emit_authored_bursts(desc, frame, window_start, cycle_start, emitted);
        emit_over_distance(desc, frame, window_start, window_end);
    }

    /// Fires the authored bursts whose times fall in the @p emitted seconds of the cycle this update covers,
    /// from @p cycle_start on (wrapping into the next cycles of a looping emitter).
    void emit_authored_bursts(const emitter_desc& desc,
                              const spawn_frame& frame,
                              float window_start,
                              float cycle_start,
                              float emitted)
    {
        if(desc.emission.bursts.empty())
        {
            return;
        }
        const float duration = desc.emission.duration;
        float cycle_position = cycle_start;
        float offset = window_start;
        float remaining = emitted;
        if(desc.emission.loop && duration > 0.0f)
        {
            // A long catch-up skips whole cycles at its start: their particles would be dead by now, and the
            // newest cycles must fit in the passes. Skipping whole cycles keeps the cycle position.
            const float alive_span = math::min(get_max_lifetime(desc, frame), float(k_max_burst_cycles_per_update - 1) * duration);
            const float skipped = std::floor(math::max(remaining - duration - alive_span, 0.0f) / duration) * duration;
            offset += skipped;
            remaining -= skipped;
        }
        for(uint32_t pass = 0; remaining > 0.0f && pass < k_max_burst_cycles_per_update; ++pass)
        {
            const float segment = math::min(remaining, math::max(duration - cycle_position, 0.0f));
            for(const emitter_burst& burst : desc.emission.bursts)
            {
                if(!fire_burst(desc, frame, burst, cycle_position, segment, offset))
                {
                    return;
                }
            }
            remaining -= segment;
            offset += segment;
            cycle_position = 0.0f;
            if(!desc.emission.loop || segment <= 0.0f)
            {
                break;
            }
        }
    }

    /// The firings of @p burst in [@p segment_start, @p segment_start + @p segment_length) seconds of the
    /// cycle, which start @p window_offset seconds into the update's window. False when the emitter is full.
    auto fire_burst(const emitter_desc& desc,
                    const spawn_frame& frame,
                    const emitter_burst& burst,
                    float segment_start,
                    float segment_length,
                    float window_offset) -> bool
    {
        const float interval = math::max(burst.interval, k_min_burst_interval);
        const float segment_end = segment_start + segment_length;
        const double first = math::max(std::ceil(double(segment_start - burst.time) / double(interval)), 0.0);
        const double last = burst.cycles == 0 ? std::numeric_limits<double>::max() : double(burst.cycles - 1);
        for(double k = first; k <= last; k += 1.0)
        {
            const float time = burst.time + float(k) * interval;
            if(time >= segment_end || time >= desc.emission.duration)
            {
                break;
            }
            if(time < segment_start || (burst.probability < 1.0f && frand01(rng_) >= burst.probability))
            {
                continue;
            }
            const float offset = window_offset + (time - segment_start);
            const float age = math::max(frame.window - offset, 0.0f);
            const math::vec3 origin = spawn_origin(desc, frame, offset);
            const uint32_t count = pick_burst_count(burst.count);
            for(uint32_t i = 0; i < count; ++i)
            {
                if(!spawn_particle(desc, frame, age, origin, nullptr))
                {
                    return false;
                }
            }
        }
        return true;
    }

    /// A random whole number of particles between the range's ends (both included), scaled by the project's
    /// spawn scale (rounded at random, so the scaled count holds on average).
    auto pick_burst_count(const frange_t& range) -> uint32_t
    {
        const float low = std::ceil(math::max(math::min(range.min, range.max), 0.0f));
        const float high = std::floor(math::max(range.min, range.max));
        float count = low;
        if(high > low)
        {
            count = low + math::min(std::floor(frand01(rng_) * (high - low + 1.0f)), high - low);
        }
        return get_scaled_count(count);
    }

    /// @p count after the project's spawn scale, rounded at random so the scaled count holds on average.
    auto get_scaled_count(float count) -> uint32_t
    {
        const float spawn_scale = math::clamp(g_quality.spawn_scale, 0.0f, 1.0f);
        return spawn_scale >= 1.0f ? uint32_t(count) : uint32_t(std::floor(count * spawn_scale + frand01(rng_)));
    }

    /// Particles per second after the project's spawn scale.
    static auto get_rate(const emitter_desc& desc) -> float
    {
        return desc.emission.particles_per_second * math::clamp(g_quality.spawn_scale, 0.0f, 1.0f);
    }

    /// Spawns particles_per_meter particles per metre the emitter moved while it emitted, spaced evenly along
    /// its path, each born when the emitter passed its place.
    void emit_over_distance(const emitter_desc& desc, const spawn_frame& frame, float window_start, float window_end)
    {
        const float per_meter = desc.emission.particles_per_meter * math::clamp(g_quality.spawn_scale, 0.0f, 1.0f);
        const float moving = frame.window - frame.catch_up;
        const float distance = math::distance(frame.previous_position, frame.position);
        if(per_meter <= 0.0f || moving <= 0.0f || distance <= 0.0f)
        {
            return;
        }
        const double spacing = 1.0 / double(per_meter);
        const double first = spacing - double(sim_.distance_accum);
        const double count = first <= double(distance) ? std::floor((double(distance) - first) / spacing) + 1.0 : 0.0;
        // More spawns than the emitter holds cannot all live: only the last capacity of them matter.
        const double skipped = math::max(count - double(capacity_), 0.0);
        for(double k = skipped; k < count; k += 1.0)
        {
            const float fraction = float((first + k * spacing) / double(distance));
            const float offset = frame.catch_up + fraction * moving;
            if(offset < window_start || offset > window_end)
            {
                continue;
            }
            const math::vec3 origin = math::mix(frame.previous_position, frame.position, fraction);
            if(!spawn_particle(desc, frame, frame.window - offset, origin, nullptr))
            {
                break;
            }
        }
        const double last = count > 0.0 ? first + (count - 1.0) * spacing : first - spacing;
        sim_.distance_accum = float(math::clamp(double(distance) - last, 0.0, spacing));
    }

    /// Spawns at the emission rate over [@p window_start, @p window_end] seconds of the update's window,
    /// each particle born at its exact time: aged by the rest of the window and placed where the
    /// emitter was then. Particles that would already be dead are skipped.
    void spawn_rate_window(const emitter_desc& desc,
                           const spawn_frame& frame,
                           float window_start,
                           float window_end,
                           float& accumulator)
    {
        const double interval = 1.0 / double(get_rate(desc));
        // Carried over from a lower rate, the accumulator would backdate a burst of spawns: it holds one interval.
        accumulator = float(math::min(double(accumulator), interval));
        const double span = double(window_end - window_start);
        // A whole number of intervals that rounds just below itself still counts as reached.
        const double count = std::floor((double(accumulator) + span) / interval + k_spawn_count_epsilon);
        const double first_offset = double(window_start) + (interval - double(accumulator));
        accumulator = float(double(accumulator) + span - count * interval);
        if(count <= 0.0)
        {
            return;
        }
        const double max_age = double(get_max_lifetime(desc, frame));
        const double window = double(frame.window);
        // Spawns older than the longest lifetime are already dead: start from the first that is not.
        const double oldest_alive_offset = window - max_age - k_spawn_skip_margin;
        double first_index = 0.0;
        if(first_offset < oldest_alive_offset)
        {
            first_index = std::ceil((oldest_alive_offset - first_offset) / interval);
        }
        for(double k = first_index; k < count; k += 1.0)
        {
            const double offset = first_offset + k * interval;
            const float age = float(math::max(window - offset, 0.0));
            const math::vec3 origin = spawn_origin(desc, frame, float(offset));
            if(!spawn_particle(desc, frame, age, origin, nullptr))
            {
                break;
            }
        }
    }

    /// Where the emitter was @p offset seconds into the update's window: still during the catch-up part,
    /// then moving from its previous to its current position (spawn_along_path 0 keeps it at the current).
    auto spawn_origin(const emitter_desc& desc, const spawn_frame& frame, float offset) const -> math::vec3
    {
        const float moving = frame.window - frame.catch_up;
        if(moving <= 0.0f || offset < frame.catch_up)
        {
            return frame.position;
        }
        const float phase = math::clamp((offset - frame.catch_up) / moving, 0.0f, 1.0f);
        const float blend = math::mix(1.0f, phase, math::clamp(desc.motion.spawn_along_path, 0.0f, 1.0f));
        return math::mix(frame.previous_position, frame.position, blend);
    }

    void emit_bursts(const emitter_desc& desc, const spawn_frame& frame)
    {
        for(const pending_burst& burst : pending_bursts_)
        {
            const uint32_t count = get_scaled_count(float(burst.count));
            for(uint32_t i = 0; i < count; ++i)
            {
                if(!spawn_particle(desc, frame, 0.0f, frame.position, &burst.params))
                {
                    break;
                }
            }
        }
        pending_bursts_.clear();
    }

    /// Writes one particle born @p age seconds ago at @p origin. False when the emitter is full.
    auto spawn_particle(const emitter_desc& desc,
                        const spawn_frame& frame,
                        float age,
                        const math::vec3& origin,
                        const emit_params* burst) -> bool
    {
        const uint32_t mask = burst ? burst->mask : 0u;
        const auto has_override = [mask](uint32_t flag)
        {
            return (mask & flag) != 0u;
        };
        float lifespan = math::clamp(frand_range(rng_, desc.motion.lifetime.min, desc.motion.lifetime.max) * frame.lifetime_multiplier,
                                     k_min_particle_lifespan,
                                     max_particle_lifetime);
        if(has_override(emit_params::lifetime_flag))
        {
            lifespan = math::clamp(burst->lifetime, k_min_particle_lifespan, max_particle_lifetime);
        }
        if(age >= lifespan)
        {
            // Spawned and gone within the window: nothing to draw, nothing to keep.
            return true;
        }
        if(get_live_count() >= get_spawn_capacity())
        {
            return false;
        }
        const uint32_t slot = slots_.allocate();
        if(slot >= capacity_)
        {
            return false;
        }
        shape_sample sample = sample_shape(desc.emission, rng_);
        const math::vec3 point = (desc.emission.shape_position + sample.point) * desc.emission.shape_scale;
        if(!has_shape_normal(desc.emission.shape))
        {
            sample.normal = direction_from_center(point);
        }
        const math::vec3 direction = random_in_cone(rng_,
                                                    shape_direction(desc.emission.direction, desc.emission.shape, sample),
                                                    desc.emission.direction_spread * k_degrees_to_radians);
        const float speed = frand_range(rng_, desc.motion.start_speed.min, desc.motion.start_speed.max);
        math::vec3 start = point;
        math::vec3 velocity = direction * speed;
        if(desc.motion.space == simulation_space::world)
        {
            start = frame.basis * point + origin;
            velocity = frame.basis * velocity + frame.emitter_velocity * desc.motion.inherit_velocity;
        }
        if(has_override(emit_params::position_flag))
        {
            start = burst->position;
        }
        if(has_override(emit_params::velocity_flag))
        {
            velocity = burst->velocity;
        }
        if(has_override(emit_params::added_velocity_flag))
        {
            velocity += burst->added_velocity;
        }
        particle_scale_pick pick;
        pick.start = frand01(rng_);
        pick.end = frand01(rng_);
        const scale_curve_widths widths = get_scale_curve_widths(desc.appearance.size_over_life);
        if(has_override(emit_params::size_flag))
        {
            fit_scale_pick_to_size(desc.appearance.size_over_life, widths, burst->size, pick);
        }
        math::vec4 tint(1.0f);
        if(has_override(emit_params::color_flag))
        {
            tint = burst->color.to_linear().value;
        }
        const uint64_t age_ticks = seconds_to_ticks(double(age));
        const uint64_t birth = now_ticks_ >= age_ticks ? now_ticks_ - age_ticks : 0;
        const uint64_t lifespan_ticks = std::max<uint64_t>(seconds_to_ticks(double(lifespan)), 1);
        particle_record record;
        record.set_start(start);
        record.birth = uint32_t(birth);
        record.set_velocity(velocity);
        record.lifespan = uint32_t(lifespan_ticks);
        record.seed = make_seed(seed_ + spawn_counter_++);
        record.size_picks = pack_size_picks(pick.start, pick.end);
        record.size_factor = pick.factor;
        record.size_offset = pick.offset;
        pack_tint(tint, record);
        record.emitter = index_;
        record.motion_offset = 0;
        records_[slot] = record;
        bounce_counts_[slot] = 0;
        schedule_contact(desc, slot);
        deaths_.push(birth + records_[slot].lifespan, slot);
        restage(slot);
        record_event(desc, particle_event_type::birth, records_[slot], birth);
        const float size_peak = luts_ ? luts_->ranges.size_peak : 1.0f;
        history_.add(clock_seconds_ - double(age), start, math::length(velocity), lifespan, size_peak * pick.factor + pick.offset);
        return true;
    }

    void update_bounds(const emitter_desc& desc, emitter_feature features, const emitter_transform_state& transform)
    {
        const math::transform& world = transform.current;
        if(desc.render.bounds_mode == bounds_mode::fixed)
        {
            math::bbox fixed = desc.render.fixed_bounds;
            fixed.mul(world);
            sim_.world_bounds = fixed;
            return;
        }
        const math::bbox unit = get_unit_spawn_region(desc.emission);
        const math::vec3 corner_a = (desc.emission.shape_position + unit.min) * desc.emission.shape_scale;
        const math::vec3 corner_b = (desc.emission.shape_position + unit.max) * desc.emission.shape_scale;
        math::bbox region(math::min(corner_a, corner_b), math::max(corner_a, corner_b));
        const bool is_local = desc.motion.space == simulation_space::local;
        if(!is_local)
        {
            region.mul(world);
        }
        motion_bounds_params motion;
        motion.acceleration = params_.acceleration;
        motion.drag = params_.drag;
        if(luts_)
        {
            motion.ranges = luts_->ranges;
        }
        motion.has_ease = has_feature(features, emitter_feature::non_linear_ease);
        motion.has_speed_curve = has_feature(features, emitter_feature::speed_curve);
        math::bbox bounds = compute_motion_bounds(history_, region, motion);
        if(is_local)
        {
            bounds.mul(world);
        }
        // The largest particle: the live ones, and what the size curve gives new ones.
        float size = math::max(history_.get_max_size(), luts_ ? luts_->ranges.size_peak : 0.0f);
        if(has_feature(features, emitter_feature::size_by_speed))
        {
            size *= math::max(desc.appearance.size_by_speed_multiplier.min, desc.appearance.size_by_speed_multiplier.max);
        }
        size *= params_.system_scale;
        math::vec2 quad(size);
        if(desc.render.render_mode == render_mode::stretched)
        {
            // Fastest a particle can move: its launch at the speed curve's peak plus what the acceleration adds
            // over a life, at the easing's fastest rate.
            float launch = 0.0f;
            float longest_life = 0.0f;
            for(const auto& bucket : history_.get_buckets())
            {
                launch = math::max(launch, bucket.max_speed);
                longest_life = math::max(longest_life, bucket.max_lifespan);
            }
            const float top_speed = (launch * motion.ranges.speed_factor_max + math::length(params_.acceleration) * longest_life) *
                                    motion.ranges.ease_rate_max;
            quad.y = size * params_.stretch_length_scale + top_speed * params_.stretch_speed_scale;
        }
        if(has_feature(features, emitter_feature::size_axis_curve) && luts_)
        {
            quad *= luts_->ranges.size_axis_peak;
        }
        // The cull radius holds the turbulence's reach too.
        const float extent = get_particle_cull_radius(quad, params_);
        bounds.min -= math::vec3(extent);
        bounds.max += math::vec3(extent);
        sim_.world_bounds = bounds;
    }

    uint16_t index_ = 0;
    uint32_t capacity_ = 0;
    uint32_t pool_base_ = particle_gpu::invalid_base;
    bool is_live_ = false;
    emitter_sim_state sim_;
    double clock_seconds_ = 0.0;
    uint64_t now_ticks_ = 0;
    bx::RngMwc rng_;
    bool has_seed_ = false;
    bool seed_is_fixed_ = false;
    uint32_t fixed_seed_ = 0;
    uint32_t seed_ = 0;
    uint32_t spawn_counter_ = 0;
    particle_slot_allocator slots_;
    particle_death_queue deaths_;
    std::vector<particle_record> records_;
    /// Per slot: bounces so far, and the plane of the next contact.
    std::vector<uint8_t> bounce_counts_;
    std::vector<uint8_t> contact_planes_;
    /// Particles in contact order (collision).
    particle_death_queue contacts_;
    /// Events recorded for take_particle_events.
    std::vector<particle_event> events_;
    std::vector<uint8_t> staged_;
    std::vector<uint32_t> staged_slots_;
    std::vector<pending_burst> pending_bursts_;
    spawn_history history_;
    bool has_lut_ = false;
    uint64_t lut_key_ = 0;
    uint32_t lut_base_ = 0;
    const lut_set* luts_ = nullptr;
    emitter_eval_params params_{};
    math::quat previous_emitter_rotation_{1.0f, 0.0f, 0.0f, 0.0f};
    emitter_render_desc render_desc_{};
    static_row static_row_{};
    /// The cutout table the emitter points at, and its first vec4 + 1 as last read (0 while it bakes).
    uint64_t cutout_key_ = 0;
    float cutout_table_ = 0.0f;
    bool static_dirty_ = true;
    particle_sort_backend backend_override_ = particle_sort_backend::gpu;
    bool has_backend_override_ = false;
};

/// Sorting key of a CPU-sorted particle: ascending draws first.
struct cpu_sort_entry
{
    uint64_t key = 0;
    uint32_t slot = 0;
};

auto float_order(float value) -> uint32_t
{
    uint32_t bits = 0;
    const float clamped = math::max(value, 0.0f);
    std::memcpy(&bits, &clamped, sizeof(bits));
    return bits;
}

auto is_inside(const std::array<math::vec4, 6>& planes, const math::vec3& position, float radius) -> bool
{
    for(const math::vec4& plane : planes)
    {
        if(math::dot(math::vec3(plane), position) + plane.w > radius)
        {
            return false;
        }
    }
    return true;
}

class particle_system_soa
{
public:
    void init(uint16_t max_emitters)
    {
        static bx::DefaultAllocator allocator;
        allocator_ = &allocator;
        emitter_alloc_ = bx::createHandleAlloc(allocator_, max_emitters);
        emitters_.resize(max_emitters);
        tex_color_ = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
        noise_ = bgfx::createUniform("s_particle_noise", bgfx::UniformType::Sampler);
        view_camera_ = bgfx::createUniform("u_viewCamera", bgfx::UniformType::Mat4);
        camera_velocity_ = bgfx::createUniform("u_particle_camera_velocity", bgfx::UniformType::Vec4);
        scene_depth_ = bgfx::createUniform("s_sceneDepth", bgfx::UniformType::Sampler);
        particle_params_ = bgfx::createUniform("u_particleParams", bgfx::UniformType::Vec4);
    }

    void init_gpu(rtti::context& ctx)
    {
        gpu_.init(ctx);
        if(gpu_.can_draw())
        {
            APPLOG_INFO("Particles: {}", gpu_.can_sort() ? "GPU cull and sort" : "CPU cull and sort");
        }
    }

    void shutdown()
    {
        if(emitter_alloc_)
        {
            const uint16_t count = emitter_alloc_->getNumHandles();
            const uint16_t* handles = emitter_alloc_->getHandles();
            for(uint16_t i = 0; i < count; ++i)
            {
                emitters_[handles[i]].destroy(gpu_);
            }
        }
        gpu_.shutdown();
        destroy_uniform(tex_color_);
        destroy_uniform(noise_);
        destroy_uniform(view_camera_);
        destroy_uniform(camera_velocity_);
        destroy_uniform(scene_depth_);
        destroy_uniform(particle_params_);
        if(emitter_alloc_)
        {
            bx::destroyHandleAlloc(allocator_, emitter_alloc_);
            emitter_alloc_ = nullptr;
        }
        emitters_.clear();
        allocator_ = nullptr;
    }

    auto create_emitter(uint32_t max_particles) -> emitter_handle
    {
        emitter_handle handle{emitter_alloc_->alloc()};
        if(is_valid(handle))
        {
            emitters_[handle.idx].create(handle.idx, max_particles, gpu_);
        }
        return handle;
    }

    void destroy_emitter(emitter_handle handle)
    {
        emitters_[handle.idx].destroy(gpu_);
        emitter_alloc_->free(handle.idx);
    }

    void set_emitter_cutout(emitter_handle handle, uint64_t key, const cutout_baker& bake)
    {
        emitter& em = get(handle);
        if(em.get_cutout_key() == key)
        {
            return;
        }
        em.set_cutout(gpu_, key);
        if(key == 0 || !gpu_.is_cutout_pending(key) || cutout_bakes_.count(key) != 0 || !bake)
        {
            return;
        }
        // First ask: the bake runs on a worker; sync hands its polygons to the table.
        auto bake_state = std::make_shared<cutout_bake>();
        cutout_bakes_.emplace(key, bake_state);
        poolstl::get_background_pool().submit_detach(
            [bake_state, bake]()
            {
                try
                {
                    bake_state->polygons = bake();
                }
                catch(const std::exception& e)
                {
                    APPLOG_WARNING("Particles: a cutout bake failed: {}", e.what());
                }
                bake_state->is_done.store(true, std::memory_order_release);
            });
    }

    /// Hands every finished cutout bake to its table. Main thread.
    void collect_cutout_bakes()
    {
        for(auto it = cutout_bakes_.begin(); it != cutout_bakes_.end();)
        {
            if(!it->second->is_done.load(std::memory_order_acquire))
            {
                ++it;
                continue;
            }
            gpu_.set_cutout_polygons(it->first, it->second->polygons);
            it = cutout_bakes_.erase(it);
        }
    }

    auto get(emitter_handle handle) -> emitter&
    {
        BX_ASSERT(is_valid(handle), "invalid particle emitter handle");
        return emitters_[handle.idx];
    }

    auto get_gpu() -> particle_gpu&
    {
        return gpu_;
    }

    void sync()
    {
        if(!emitter_alloc_)
        {
            return;
        }
        APP_SCOPE_PERF("Particles/SOA GPU Sync");
        collect_cutout_bakes();
        const bool restage_all = gpu_.take_pool_reset();
        const uint16_t count = emitter_alloc_->getNumHandles();
        const uint16_t* handles = emitter_alloc_->getHandles();
        for(uint16_t i = 0; i < count; ++i)
        {
            emitters_[handles[i]].sync(gpu_, restage_all);
        }
        gpu_.flush();
    }

    auto render_view(const render_view_params& params, const render_batch* batches, uint32_t batch_count) -> render_view_stats
    {
        render_view_stats stats;
        if(batch_count == 0)
        {
            return stats;
        }
        gpu_.begin_view();
        if(!gpu_.can_draw())
        {
            return stats;
        }
        APP_SCOPE_PERF("Rendering/Particle Pass SOA/Render View");
        const bool can_sort = gpu_.can_sort();
        // GPU batches: every emitter of the batch culls and sorts on the GPU.
        std::vector<particle_gpu::view_task>& tasks = tasks_scratch_;
        std::vector<particle_gpu::view_batch>& gpu_batches = gpu_batches_scratch_;
        tasks.clear();
        gpu_batches.assign(batch_count, {});
        cpu_firsts_.assign(batch_count, 0u);
        cpu_counts_.assign(batch_count, 0u);
        is_gpu_batch_.assign(batch_count, 0u);
        for(uint32_t b = 0; b < batch_count; ++b)
        {
            const render_batch& batch = batches[b];
            bool is_gpu = can_sort;
            gpu_sort_kind kind = gpu_sort_kind::append;
            for(uint32_t e = 0; e < batch.count; ++e)
            {
                const emitter& em = emitters_[batch.emitters[e].idx];
                is_gpu = is_gpu && em.resolve_backend(can_sort) == particle_sort_backend::gpu;
                if(e == 0 && em.get_render_desc().render_mode == render_mode::ribbon)
                {
                    // A ribbon needs its birth order whatever the sorting setting.
                    kind = gpu_sort_kind::ribbon;
                }
                else if(e == 0 && g_quality.sorting)
                {
                    kind = to_sort_kind(em.get_render_desc().sort_mode, batch.blend_mode);
                }
                stats.drawn_particles += em.get_live_count();
            }
            stats.drawn_emitters += batch.count;
            is_gpu_batch_[b] = is_gpu ? 1u : 0u;
            gpu_batches[b].is_sorted = kind != gpu_sort_kind::append;
            switch(batch.primitive)
            {
                case particle_primitive::octagon:
                    gpu_batches[b].index_count = octagon_index_count;
                    gpu_batches[b].first_index = octagon_first_index;
                    break;
                case particle_primitive::trail:
                    gpu_batches[b].index_count = trail_index_count;
                    gpu_batches[b].first_index = trail_first_index;
                    break;
                case particle_primitive::ribbon:
                    gpu_batches[b].index_count = quad_index_count;
                    gpu_batches[b].first_index = 0u;
                    break;
                case particle_primitive::cutout:
                    gpu_batches[b].index_count = cutout_index_count;
                    gpu_batches[b].first_index = cutout_first_index;
                    break;
                default:
                    gpu_batches[b].index_count = quad_index_count;
                    gpu_batches[b].first_index = 0u;
                    break;
            }
            if(!is_gpu)
            {
                stats.sorted_keys += build_cpu_list(params, batch, kind, b);
                continue;
            }
            for(uint32_t e = 0; e < batch.count; ++e)
            {
                const emitter& em = emitters_[batch.emitters[e].idx];
                if(em.get_high_water() == 0)
                {
                    continue;
                }
                tasks.push_back({em.get_pool_base(), em.get_high_water(), b, kind});
            }
        }
        if(!tasks.empty())
        {
            const auto dispatch = gpu_.dispatch_view(params.view,
                                                     tasks,
                                                     gpu_batches,
                                                     params.eye,
                                                     params.forward,
                                                     math::length(params.camera_velocity),
                                                     params.frustum_planes);
            stats.dispatches += dispatch.dispatches;
            stats.sorted_keys += dispatch.sorted_keys;
        }
        gpu_.upload_view_lists();
        const bool has_depth = bgfx::isValid(params.scene_depth);
        for(uint32_t b = 0; b < batch_count; ++b)
        {
            const render_batch& batch = batches[b];
            const bool is_gpu = is_gpu_batch_[b] != 0u;
            const bool is_unsupported = params.is_transmittance_target && batch.blend_mode == blend_mode::multiply;
            if(is_unsupported || (is_gpu ? gpu_batches[b].capacity == 0 : cpu_counts_[b] == 0))
            {
                continue;
            }
            const bool is_mask = batch.texture_mode == texture_mode::mask;
            const float particle_params[4] = {has_depth ? 1.0f : 0.0f,
                                              float(int(batch.blend_mode)),
                                              is_mask ? 1.0f : 0.0f,
                                              has_frame_blending(batch) ? 1.0f : 0.0f};
            bgfx::setTexture(gpu_color_texture_stage, tex_color_, batch.texture, get_sampler_flags(batch));
            bgfx::setTexture(gpu_noise_stage, noise_, gpu_.get_noise_texture());
            if(has_depth)
            {
                bgfx::setTexture(gpu_scene_depth_stage, scene_depth_, params.scene_depth);
            }
            bgfx::setUniform(particle_params_, particle_params);
            bgfx::setUniform(view_camera_, view_camera(params.view_matrix).data());
            const math::vec4 camera_velocity(params.camera_velocity, 0.0f);
            bgfx::setUniform(camera_velocity_, &camera_velocity);
            if(bgfx::isValid(params.pre_exposure_uniform))
            {
                bgfx::setUniform(params.pre_exposure_uniform, &params.pre_exposure);
            }
            if(bgfx::isValid(params.environment_texture))
            {
                bgfx::setTexture(params.environment_stage, params.environment_sampler, params.environment_texture);
            }
            particle_gpu::draw_params draw;
            draw.view = params.view;
            draw.program = is_mask ? params.mask_program : params.program;
            const bool is_overdraw = params.overdraw_weight > 0.0f;
            const bool is_lit = batch.lighting != particle_lighting::unlit && params.prev_view_proj == nullptr &&
                                !is_overdraw && bgfx::isValid(params.lit_program) &&
                                bgfx::isValid(params.lit_mask_program) && params.bind_lighting;
            if(params.bind_fog)
            {
                params.bind_fog(is_lit);
            }
            if(is_lit)
            {
                draw.program = is_mask ? params.lit_mask_program : params.lit_program;
                params.bind_lighting(batch);
            }
            // Trails and ribbons turn with their paths: both of their sides face the camera at some point.
            const bool is_two_sided = batch.primitive == particle_primitive::trail || batch.primitive == particle_primitive::ribbon;
            draw.state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS |
                         (is_two_sided ? 0 : BGFX_STATE_CULL_CW);
            if(params.prev_view_proj != nullptr)
            {
                bgfx::setUniform(params.prev_view_proj_uniform, params.prev_view_proj);
            }
            else if(is_overdraw)
            {
                draw.state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_FACTOR, BGFX_STATE_BLEND_ONE);
                draw.blend_rgba = get_blend_factor_rgba(params.overdraw_weight);
            }
            else
            {
                draw.state |= (params.is_transmittance_target ? transmittance_blend_state(batch.blend_mode)
                                                              : blend_state(batch.blend_mode)) |
                              (params.has_reactive_target ? BGFX_STATE_BLEND_INDEPENDENT : 0);
                draw.blend_rgba = params.has_reactive_target ? reactive_blend_rgba : 0u;
            }
            draw.is_cpu_list = !is_gpu;
            draw.first = is_gpu ? gpu_batches[b].first : cpu_firsts_[b];
            draw.batch = b;
            draw.instance_count = cpu_counts_[b];
            draw.index_count = gpu_batches[b].index_count;
            draw.first_index = gpu_batches[b].first_index;
            gpu_.submit_draw(draw);
            ++stats.draws;
        }
        return stats;
    }

private:
    static void destroy_uniform(bgfx::UniformHandle& handle)
    {
        if(bgfx::isValid(handle))
        {
            bgfx::destroy(handle);
            handle = BGFX_INVALID_HANDLE;
        }
    }

    /// How @p batch samples its texture: as the texture says, except a ribbon, which clamps across itself and
    /// along itself too unless its texture tiles (its head and tail must not filter into each other).
    auto get_sampler_flags(const render_batch& batch) const -> uint32_t
    {
        constexpr uint32_t texture_flags = UINT32_MAX;
        if(batch.primitive != particle_primitive::ribbon || batch.count == 0)
        {
            return texture_flags;
        }
        const emitter_render_desc& render = emitters_[batch.emitters[0].idx].get_render_desc();
        const bool is_tiled = render.ribbon_texture_mode == ribbon_texture_mode::tile;
        return BGFX_SAMPLER_U_CLAMP | (is_tiled ? 0u : BGFX_SAMPLER_V_CLAMP);
    }

    /// True when an emitter of @p batch crossfades its texture sheet frames (the shaders sample twice then).
    auto has_frame_blending(const render_batch& batch) const -> bool
    {
        for(uint32_t e = 0; e < batch.count; ++e)
        {
            const emitter_feature features = emitter_feature(emitters_[batch.emitters[e].idx].get_params().flags);
            if(has_feature(features, emitter_feature::frame_blending))
            {
                return true;
            }
        }
        return false;
    }

    /// The view matrix's rotation, transposed: its columns are the camera's axes.
    static auto view_camera(const float* mtx_view) -> std::array<float, 16>
    {
        return {mtx_view[0], mtx_view[4], mtx_view[8], 0.0f,
                mtx_view[1], mtx_view[5], mtx_view[9], 0.0f,
                mtx_view[2], mtx_view[6], mtx_view[10], 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f};
    }

    /// CPU path: evaluates, culls and sorts the batch's particles here and uploads their slots.
    /// @return The particles sorted (0 for an unsorted list).
    auto build_cpu_list(const render_view_params& params, const render_batch& batch, gpu_sort_kind kind, uint32_t index)
        -> uint32_t
    {
        APP_SCOPE_PERF("Rendering/Particle Pass SOA/CPU Sort");
        cpu_entries_.clear();
        for(uint32_t e = 0; e < batch.count; ++e)
        {
            const emitter& em = emitters_[batch.emitters[e].idx];
            const lut_set* luts = em.get_luts();
            if(!luts)
            {
                continue;
            }
            const emitter_eval_params& eval = em.get_params();
            const uint32_t base = em.get_pool_base();
            em.for_each_live(
                [&](uint32_t slot, const particle_record& record, const particle_state& state)
                {
                    const float axis_y = has_feature(emitter_feature(eval.flags), emitter_feature::size_axis_curve)
                                             ? luts->sample(lut_row_shape, state.life).y
                                             : 1.0f;
                    const float radius = get_particle_cull_radius(evaluate_particle_quad(record, state, eval, *luts), eval) +
                                         get_camera_stretch_reach(eval, math::length(params.camera_velocity), axis_y);
                    const bool is_ribbon = kind == gpu_sort_kind::ribbon;
                    if(!is_ribbon && (!is_inside(params.frustum_planes, state.position, radius) ||
                                      !is_within_draw_distance(state.position, radius, params.eye, eval)))
                    {
                        return;
                    }
                    const math::vec3 to_particle = state.position - params.eye;
                    const uint32_t age_ticks = eval.now - record.birth;
                    uint64_t key = 0;
                    switch(kind)
                    {
                        case gpu_sort_kind::distance:
                            key = ~float_order(math::dot(to_particle, to_particle));
                            break;
                        case gpu_sort_kind::depth:
                            key = ~float_order(math::dot(to_particle, params.forward));
                            break;
                        case gpu_sort_kind::oldest_first:
                        case gpu_sort_kind::ribbon:
                            key = ~age_ticks;
                            break;
                        case gpu_sort_kind::youngest_first:
                            key = age_ticks;
                            break;
                        default:
                            key = base + slot;
                            break;
                    }
                    // Ties keep pool order, so equal keys draw the same way every frame.
                    cpu_entries_.push_back({(uint64_t(uint32_t(key)) << 32u) | uint64_t(base + slot), base + slot});
                });
        }
        const bool is_sorted = kind != gpu_sort_kind::append;
        if(is_sorted)
        {
            std::sort(cpu_entries_.begin(),
                      cpu_entries_.end(),
                      [](const cpu_sort_entry& a, const cpu_sort_entry& b)
                      {
                          return a.key < b.key;
                      });
        }
        const uint32_t sorted_keys = is_sorted ? uint32_t(cpu_entries_.size()) : 0u;
        cpu_slots_.clear();
        for(const auto& entry : cpu_entries_)
        {
            cpu_slots_.push_back(entry.slot);
        }
        cpu_firsts_[index] = gpu_.upload_cpu_list(cpu_slots_);
        cpu_counts_[index] = uint32_t(cpu_slots_.size());
        return sorted_keys;
    }

    bx::AllocatorI* allocator_ = nullptr;
    bx::HandleAlloc* emitter_alloc_ = nullptr;
    std::vector<emitter> emitters_;
    particle_gpu gpu_;
    /// A cutout table's polygons, filled by a worker; is_done publishes them.
    struct cutout_bake
    {
        std::atomic<bool> is_done{false};
        std::vector<cutout_polygon> polygons;
    };
    /// Bakes in flight, by cutout key.
    std::unordered_map<uint64_t, std::shared_ptr<cutout_bake>> cutout_bakes_;
    bgfx::UniformHandle tex_color_ = BGFX_INVALID_HANDLE;
    /// The turbulence noise volume (vertex stage 7).
    bgfx::UniformHandle noise_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle view_camera_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle camera_velocity_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle scene_depth_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle particle_params_ = BGFX_INVALID_HANDLE;
    std::vector<particle_gpu::view_task> tasks_scratch_;
    std::vector<particle_gpu::view_batch> gpu_batches_scratch_;
    std::vector<uint32_t> cpu_firsts_;
    std::vector<uint32_t> cpu_counts_;
    std::vector<uint8_t> is_gpu_batch_;
    std::vector<cpu_sort_entry> cpu_entries_;
    std::vector<uint32_t> cpu_slots_;
};

particle_system_soa g_system;

} // namespace

void set_quality(const particle_quality& quality)
{
    g_quality = quality;
}

auto get_quality() -> const particle_quality&
{
    return g_quality;
}

auto get_capped_lighting(particle_lighting lighting) -> particle_lighting
{
    return particle_lighting(math::min(int(lighting), int(g_quality.max_lighting)));
}

void init(uint16_t max_emitters)
{
    g_system.init(max_emitters);
}

void init_gpu(rtti::context& ctx)
{
    g_system.init_gpu(ctx);
}

void shutdown()
{
    g_system.shutdown();
}

void set_default_sort_backend(particle_sort_backend backend)
{
    g_default_sort_backend = backend;
}

auto get_default_sort_backend() -> particle_sort_backend
{
    return g_default_sort_backend;
}

void set_emitter_sort_backend(emitter_handle handle, particle_sort_backend backend)
{
    g_system.get(handle).set_backend_override(backend);
}

auto get_emitter_sort_backend(emitter_handle handle) -> particle_sort_backend
{
    return g_system.get(handle).resolve_backend(g_system.get_gpu().can_sort());
}

auto is_gpu_sort_available() -> bool
{
    return g_system.get_gpu().can_sort();
}

auto create_emitter(uint32_t max_particles) -> emitter_handle
{
    return g_system.create_emitter(max_particles);
}

void destroy_emitter(emitter_handle handle)
{
    g_system.destroy_emitter(handle);
}

void set_emitter_cutout(emitter_handle handle, uint64_t key, const cutout_baker& bake)
{
    g_system.set_emitter_cutout(handle, key, bake);
}

auto get_emitter_cutout(emitter_handle handle) -> uint64_t
{
    return g_system.get(handle).get_cutout_key();
}

void reset_emitter(emitter_handle handle)
{
    g_system.get(handle).reset();
}

void update_emitter(emitter_handle handle,
                    float dt,
                    const emitter_desc& desc,
                    emitter_transform_state& transform,
                    emitter_playback_desc& playback)
{
    g_system.get(handle).update(dt, desc, transform, playback, g_system.get_gpu());
}

void update_emitter_frozen(emitter_handle handle,
                           float dt,
                           const emitter_desc& desc,
                           emitter_transform_state& transform,
                           bool catch_up)
{
    g_system.get(handle).update_frozen(dt, desc, transform, catch_up);
}

void emit(emitter_handle handle, uint32_t count, const emit_params& params)
{
    g_system.get(handle).queue_burst(count, params);
}

auto has_pending_bursts(emitter_handle handle) -> bool
{
    return g_system.get(handle).has_pending_bursts();
}

void sync_gpu_simulation()
{
    g_system.sync();
}

auto has_updated(emitter_handle handle) -> bool
{
    return g_system.get(handle).has_updated();
}

void get_aabb(emitter_handle handle, math::bbox& out_aabb)
{
    out_aabb = g_system.get(handle).get_bounds();
}

auto get_num_particles(emitter_handle handle) -> uint32_t
{
    return g_system.get(handle).get_live_count();
}

auto get_camera_stretch_reach(emitter_handle handle, float camera_speed) -> float
{
    const emitter& em = g_system.get(handle);
    const lut_set* luts = em.get_luts();
    const float axis_peak = luts && has_feature(emitter_feature(em.get_params().flags), emitter_feature::size_axis_curve)
                                ? luts->ranges.size_axis_peak
                                : 1.0f;
    return get_camera_stretch_reach(em.get_params(), camera_speed, axis_peak);
}

void get_particle_states(emitter_handle handle, std::vector<particle_state>& out_states)
{
    out_states.clear();
    g_system.get(handle).for_each_live(
        [&](uint32_t, const particle_record&, const particle_state& state)
        {
            out_states.push_back(state);
        });
}

void take_particle_events(emitter_handle handle, std::vector<particle_event>& out_events)
{
    g_system.get(handle).take_events(out_events);
}

namespace
{
/// The rotation that turns +Y onto the unit @p direction.
auto get_rotation_from_up(const math::vec3& direction) -> math::quat
{
    constexpr float opposite_threshold = -0.9999f;
    if(direction.y < opposite_threshold)
    {
        return math::angleAxis(math::pi<float>(), math::vec3(1.0f, 0.0f, 0.0f));
    }
    const math::vec3 axis = math::cross(math::vec3(0.0f, 1.0f, 0.0f), direction);
    const float scale = math::sqrt((1.0f + direction.y) * 2.0f);
    return math::quat(0.5f * scale, axis.x / scale, axis.y / scale, axis.z / scale);
}

/// A mesh particle's own axis of rotation: a direction drawn uniformly from its seed.
auto get_particle_mesh_axis(uint32_t seed) -> math::vec3
{
    const float height = 2.0f * particle_random(seed, salt_mesh_axis_height) - 1.0f;
    const float turn = math::two_pi<float>() * particle_random(seed, salt_mesh_axis_turn);
    const float radius = math::sqrt(math::max(1.0f - height * height, 0.0f));
    return {radius * math::cos(turn), height, radius * math::sin(turn)};
}

auto make_particle_mesh_transform(const particle_record& record,
                                  const particle_state& state,
                                  const emitter_eval_params& params,
                                  const lut_set& luts) -> math::mat4
{
    constexpr float min_align_speed = 1.0e-4f;
    const emitter_feature features = emitter_feature(params.flags);
    math::vec3 scale = params.size_axes * evaluate_particle_size(record, state, params, luts);
    if(has_feature(features, emitter_feature::size_axis_curve))
    {
        const math::vec4 shape = luts.sample(lut_row_shape, state.life);
        scale.x *= shape.x;
        scale.y *= shape.y;
        scale.z *= luts.sample(lut_row_turbulence_size_z, state.life).y;
    }
    const float angle = evaluate_particle_angle(record, state, params, luts);
    const float speed = math::length(state.velocity);
    math::quat orientation;
    if(has_feature(features, emitter_feature::align_to_direction) && speed > min_align_speed)
    {
        orientation = get_rotation_from_up(state.velocity / speed) * math::angleAxis(angle, math::vec3(0.0f, 1.0f, 0.0f));
    }
    else
    {
        // Local-space particles turn with their emitter; world-space ones keep the world's axes.
        const math::quat base = has_feature(features, emitter_feature::local_space) ? params.emitter_rotation
                                                                                   : math::quat(1.0f, 0.0f, 0.0f, 0.0f);
        orientation = base * math::angleAxis(angle, get_particle_mesh_axis(record.seed));
    }
    math::mat4 transform = math::mat4_cast(orientation);
    transform[0] *= scale.x;
    transform[1] *= scale.y;
    transform[2] *= scale.z;
    transform[3] = math::vec4(state.position, 1.0f);
    return transform;
}
} // namespace

void collect_particle_mesh_transforms(emitter_handle handle,
                                      std::vector<math::mat4>& out_current,
                                      std::vector<math::mat4>& out_previous,
                                      std::vector<float>& out_opacity)
{
    out_current.clear();
    out_previous.clear();
    out_opacity.clear();
    const emitter& em = g_system.get(handle);
    const lut_set* luts = em.get_luts();
    if(!luts)
    {
        return;
    }
    const emitter_eval_params& params = em.get_params();
    const float frame_time = params.frame_time;
    // A local-space emitter's particles also moved with its origin.
    const bool is_local = has_feature(emitter_feature(params.flags), emitter_feature::local_space);
    const math::vec3 origin_motion = is_local ? params.origin_motion : math::vec3(0.0f);
    emitter_eval_params previous_params = params;
    previous_params.emitter_rotation = em.get_previous_emitter_rotation();
    em.for_each_live(
        [&](uint32_t, const particle_record& record, const particle_state& state)
        {
            out_current.push_back(make_particle_mesh_transform(record, state, params, *luts));
            out_opacity.push_back(math::clamp(evaluate_particle_color(record, state, params, *luts).a, 0.0f, 1.0f));
            if(state.age <= frame_time)
            {
                out_previous.push_back(out_current.back());
                return;
            }
            // One frame back along its motion, like the billboards' motion vectors (a second closed-form
            // evaluation would double the cost), at its age, size and angle then, turned as its emitter was.
            particle_state previous = state;
            previous.position -= state.velocity * frame_time + origin_motion;
            previous.life = state.life * (state.age - frame_time) / state.age;
            previous.age = state.age - frame_time;
            out_previous.push_back(make_particle_mesh_transform(record, previous, previous_params, *luts));
        });
}

void collect_particle_lights(emitter_handle handle, std::vector<particle_light>& out_lights)
{
    const emitter& em = g_system.get(handle);
    const emitter_render_desc& render = em.get_render_desc();
    const lut_set* luts = em.get_luts();
    if(render.light_count == 0 || !luts)
    {
        return;
    }
    struct candidate
    {
        float brightness;
        particle_light light;
    };
    std::vector<candidate> candidates;
    const emitter_eval_params& params = em.get_params();
    const float intensity_scale = math::max(params.intensity, 1.0e-4f);
    em.for_each_live(
        [&](uint32_t, const particle_record& record, const particle_state& state)
        {
            const math::vec4 color = evaluate_particle_color(record, state, params, *luts);
            // The color without the emitter's intensity: the light's own intensity sets its strength.
            const math::vec3 hue = math::vec3(color) / intensity_scale;
            const float brightness = math::dot(hue, math::vec3(0.2126f, 0.7152f, 0.0722f)) * color.a;
            if(brightness <= 0.0f)
            {
                return;
            }
            particle_light light;
            light.position = state.position;
            light.color = render.light_color_from_particle ? hue * color.a / brightness : math::vec3(1.0f);
            light.intensity = render.light_intensity * brightness;
            light.range = render.light_range;
            candidates.push_back({brightness, light});
        });
    const size_t count = math::min(candidates.size(), size_t(render.light_count));
    std::partial_sort(candidates.begin(),
                      candidates.begin() + std::ptrdiff_t(count),
                      candidates.end(),
                      [](const candidate& a, const candidate& b)
                      {
                          return a.brightness > b.brightness;
                      });
    for(size_t i = 0; i < count; ++i)
    {
        out_lights.push_back(candidates[i].light);
    }
}

void get_particle_sizes(emitter_handle handle, std::vector<float>& out_sizes)
{
    out_sizes.clear();
    const emitter& em = g_system.get(handle);
    const lut_set* luts = em.get_luts();
    em.for_each_live(
        [&](uint32_t, const particle_record& record, const particle_state& state)
        {
            out_sizes.push_back(evaluate_particle_size(record, state, em.get_params(), *luts));
        });
}

auto render_view(const render_view_params& params, const render_batch* batches, uint32_t batch_count)
    -> render_view_stats
{
    return g_system.render_view(params, batches, batch_count);
}

} // namespace ps_soa
} // namespace unravel
