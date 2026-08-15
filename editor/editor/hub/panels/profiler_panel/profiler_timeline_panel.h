#pragma once
#include <editor/imgui/integration/imgui.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/profiler/profiler.h>

#include <unordered_set>

namespace unravel
{

class imgui_panels;

class profiler_timeline_panel
{
public:
    explicit profiler_timeline_panel(imgui_panels* parent, const char* name);

    void on_frame_ui_render(rtti::context& ctx, const char* name);

    void show(bool s);

    struct lane_context
    {
        ImVec2 canvas_pos;
        float lane_content_width;
        float lane_height;
        double view_start_ns;
        double view_end_ns;
        double ns_per_pixel;
    };

private:
    void draw_ui(rtti::context& ctx);
    void draw_recording_toolbar();
    void draw_frame_selector_bar();
    void draw_profiler_bottom_sections(rtti::context& ctx);
    void draw_timeline();
    void draw_time_ruler(double view_start_ns, double reference_ns, double ns_per_pixel,
                         float ruler_width, ImVec2 canvas_pos);

    void draw_lane_events(const lane_context& lc,
                          const std::vector<const frame_snapshot*>& visible_frames,
                          const std::vector<uint32_t>& visible_hist_indices,
                          const frame_snapshot* selected_snap,
                          uint16_t thread_index,
                          const std::string& thread_name);

    static void timeline_render_event_block(const lane_context& lc,
                                            const profile_event& ev,
                                            bool is_reference_frame,
                                            const std::string& thread_name,
                                            profiler_timeline_panel* panel,
                                            uint32_t hist_frame_idx,
                                            uint16_t thread_idx,
                                            uint32_t event_idx);

    void validate_timeline_scope_selection(uint32_t frame_count);

    [[nodiscard]] auto is_timeline_scope_selected(uint32_t hist_frame,
                                                  uint16_t thread_index,
                                                  uint32_t event_index) const -> bool
    {
        return has_timeline_scope_selection_ && selected_scope_hist_frame_ == hist_frame &&
               selected_scope_thread_index_ == thread_index && selected_scope_event_index_ == event_index;
    }

    void set_timeline_scope_selection(uint32_t hist_frame,
                                      uint16_t thread_index,
                                      uint32_t event_index,
                                      const char* label)
    {
        has_timeline_scope_selection_ = true;
        selected_scope_hist_frame_ = hist_frame;
        selected_scope_thread_index_ = thread_index;
        selected_scope_event_index_ = event_index;
        selected_scope_label_ = label ? label : "";
    }
    void draw_lane_frame_boundaries(const lane_context& lc,
                                    const std::vector<const frame_snapshot*>& visible_frames,
                                    const frame_snapshot* selected_snap);
    void gather_visible_frames(performance_profiler* profiler,
                               uint32_t frame_count,
                               const frame_snapshot* selected_snap,
                               std::vector<const frame_snapshot*>& out);

    struct thread_entry
    {
        std::string name;
        uint16_t index;
        uint16_t max_depth;
    };

    static void collect_unique_threads(const std::vector<const frame_snapshot*>& frames,
                                       std::vector<thread_entry>& out);

    /**
     * @brief Threads sharing a name, shown as one collapsible row.
     *
     * Worker pools register every thread under the same label, so a machine with many
     * cores pushes everything below them off screen. Grouping by name keeps the pool
     * to one row until it is worth looking at.
     */
    struct thread_group
    {
        std::string name;
        /// Indices into the thread list, in first-seen order.
        std::vector<size_t> members;
    };

    static void group_threads_by_name(const std::vector<thread_entry>& threads,
                                      std::vector<thread_group>& out);

    [[nodiscard]] auto is_group_expanded(const std::string& name) const -> bool
    {
        return expanded_groups_.find(name) != expanded_groups_.end();
    }

    void toggle_group_expanded(const std::string& name)
    {
        auto it = expanded_groups_.find(name);
        if(it != expanded_groups_.end())
        {
            expanded_groups_.erase(it);
        }
        else
        {
            expanded_groups_.insert(name);
        }
    }

    /**
     * @brief Draws a group's combined activity as a single occupancy strip.
     *
     * Per pixel column, how many of the group's threads were busy. Answers the question
     * a collapsed pool is actually asked - "was anything running here" - without needing
     * one row per thread.
     */
    void draw_merged_lane(const lane_context& lc,
                          const std::vector<const frame_snapshot*>& visible_frames,
                          const std::vector<thread_entry>& threads,
                          const thread_group& group);

    void draw_aggregate_section();
    void draw_frame_histogram(performance_profiler* profiler, uint32_t frame_count, float bar_width);
    void draw_live_histogram_stack(float bar_width);

    [[nodiscard]] auto histogram_stack_height() const -> float;
    void handle_histogram_input(performance_profiler* profiler, uint32_t frame_count,
                                ImVec2 canvas_pos, float bar_width,
                                float eff_start, float eff_range);
    void handle_histogram_zoom_pan(uint32_t frame_count, ImVec2 canvas_pos,
                                   float bar_width, float eff_start, float eff_range);

    std::string name_;
    imgui_panels* parent_{nullptr};

    sample_data frame_time_history_;
    /// Rolling frame busy ms aligned with @ref frame_time_history_ (live / recording view).
    sample_data frame_busy_ms_history_;
    sample_data cpu_heap_mb_history_;
    sample_data gpu_memory_mb_history_;
    sample_data process_rss_mb_history_;

    bool show_histogram_managed_heap_{false};
    bool show_histogram_gpu_memory_{false};
    bool show_histogram_process_rss_{false};

    double view_start_ns_{0.0};
    double view_duration_ns_{20'000'000.0};
    int32_t last_centered_frame_{-2};

    int32_t selected_frame_{-1};
    bool auto_follow_{true};
    bool is_dragging_cursor_{false};

    float hist_start_{0.0f};
    float hist_range_{0.0f};

    bool show_request_{};
    bool show_{false};

    /// Groups the user opened. Multi-thread groups start collapsed, which is the whole
    /// point - a 14-thread pool should not have to be scrolled past.
    std::unordered_set<std::string> expanded_groups_;
    /// Per-pixel busy counts for the merged lane, kept across frames to avoid a
    /// per-draw allocation.
    std::vector<uint16_t> merged_lane_occupancy_;

    bool has_timeline_scope_selection_{false};
    uint32_t selected_scope_hist_frame_{0};
    uint16_t selected_scope_thread_index_{0};
    uint32_t selected_scope_event_index_{0};
    std::string selected_scope_label_{};
};

} // namespace unravel
