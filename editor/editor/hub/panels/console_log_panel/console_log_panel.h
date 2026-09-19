#pragma once
#include "../panel_base.h"
#include <editor/imgui/integration/imgui.h>

#include <hpp/optional.hpp>
#include <hpp/ring_buffer.hpp>
#include <hpp/small_vector.hpp>

// #include <console/console.h>
#include <logging/logging.h>

#include <array>
#include <atomic>
#include <string>
#include <vector>

namespace unravel
{

class console_log_panel : public panel_base, public sinks::base_sink<std::mutex>
{
public:
    using mem_buf = hpp::small_vector<char, 250>;

    struct log_source
    {
        std::string filename{};
        std::string funcname{};
        int line{0};
    };

    struct log_entry
    {
        mem_buf formatted;

        level::level_enum level{level::off};
        log_source source;

        uint64_t id{};
    };

    struct log_snapshot_entry
    {
        uint64_t id{};
        level::level_enum level{level::off};
        std::string text;
        std::string filename;
        std::string funcname;
        int line{0};
    };

    using display_entries_t = hpp::small_vector<log_entry, 1024>;
    using entries_t = hpp::stack_ringbuffer<log_entry, 1024>;

    explicit console_log_panel(const char* name);
    void sink_it_(const details::log_msg& msg) override;
    void flush_() override;

    void draw_ui(rtti::context& ctx) override;
    auto get_window_flags() const -> ImGuiWindowFlags override;

    /**
     * @brief The newest visible log as a one-line button, for the status bar. A click brings the
     * console to the front. Also raises the error toast for the entries that arrived since the
     * last call.
     */
    void draw_last_log_button();

    void on_play();
    void on_recompile();

    /**
     * @brief Snapshot recent log entries (thread-safe). Newest-last order.
     * Filters by min_level (inclusive) and optional after_id exclusive cursor.
     */
    auto snapshot_logs(level::level_enum min_level, size_t max_count, uint64_t after_id = 0) const
        -> std::vector<log_snapshot_entry>;

private:
    using level_counts_t = std::array<size_t, size_t(level::n_levels)>;

    void select_log(const log_entry& entry);
    void clear_log();
    void open_log(const log_entry& entry);
    auto has_new_entries() const -> bool;
    void set_has_new_entries(bool val);

    /// True when the entry passes the level toggles and the search.
    auto is_entry_visible(const log_entry& entry) const -> bool;
    /// Copies the visible entries out from under the lock, and recounts every level on the way.
    auto collect_visible_entries() -> display_entries_t;
    auto find_last_visible_entry() const -> hpp::optional<log_entry>;
    /// Raises a toast for the errors that arrived since the last call.
    void notify_new_errors();

    void draw_toolbar();
    void draw_clear_controls();
    void draw_search_field();
    void draw_level_filters();
    void draw_log_list(const display_entries_t& entries);
    void draw_log_row(const log_entry& entry, int row_index);
    void draw_list_context_menu();
    void draw_details();

    std::array<bool, size_t(level::n_levels)> enabled_categories_{};
    /// How many entries of each level the buffer holds, whatever the toggles and the search say.
    level_counts_t level_counts_{};

    mutable std::recursive_mutex entries_mutex_;
    ///
    entries_t entries_;
    ///
    std::atomic<bool> has_new_entries_ = {false};

    std::atomic<int64_t> new_entries_begin_idx_{-1};

    ImGuiTextFilter filter_;

    uint64_t current_id_{};
    hpp::optional<log_entry> selected_log_{};

    bool clear_on_play_{true};
    bool clear_on_recompile_{true};
};
} // namespace unravel
