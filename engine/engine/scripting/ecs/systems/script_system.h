#pragma once
#include <engine/engine_export.h>
#include <engine/physics/ecs/components/physics_component.h>
#include <engine/scripting/ecs/components/script_component.h>

#include <engine/threading/threader.h>

#include <base/basetypes.hpp>
#include <cmd_line/parser.h>
#include <context/context.hpp>
#include <filesystem/filesystem.h>
#include <hpp/span.hpp>

#include <dotnetpp/dotnetpp.h>

namespace unravel
{

struct script_system
{
    static void set_needs_recompile(const std::string& protocol, bool now = false);
    static auto get_script_debug_mode() -> bool;
    static void set_script_debug_mode(bool debug);
    static auto get_lib_name(const std::string& protocol) -> std::string;
    static auto get_lib_data_key(const std::string& protocol) -> std::string;
    static auto get_lib_temp_compiled_key(const std::string& protocol) -> std::string;
    static auto get_lib_compiled_key(const std::string& protocol) -> std::string;
    static void copy_compiled_lib(const fs::path& from, const fs::path& to);
    static auto is_debugger_attached() -> bool;
    static void log_exception(const dotnet::exception& e,
                              const hpp::source_location& loc = hpp::source_location::current());
    static auto find_dotnet_paths(const rtti::context& ctx) -> dotnet::compiler_paths;

    script_system(rtti::context& ctx, cmd_line::parser& parser);

    auto init(rtti::context& ctx, const cmd_line::parser& parser) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    void set_debug_config(const std::string& address, uint32_t port, uint32_t loglevel);

    auto load_engine_domain(rtti::context& ctx, bool recompile) -> bool;
    void unload_engine_domain();

    auto load_app_domain(rtti::context& ctx, bool recompile) -> bool;
    void unload_app_domain();

    auto get_all_scriptable_components() const -> const std::vector<dotnet::type>&;
    auto get_scriptable_component_base_type() const -> dotnet::type;
    auto get_engine_assembly() const -> dotnet::assembly;
    auto get_app_assembly() const -> dotnet::assembly;

    auto get_type_by_fullname(const std::string& fullname) const -> dotnet::type;
    auto get_type(const std::string& name_space, const std::string& name) const -> dotnet::type;

    auto is_create_called() const -> bool;
    auto is_update_called() const -> bool;
    void wait_for_jobs_to_finish(rtti::context& ctx);
    auto has_compilation_errors() const -> bool;

    void on_sensor_enter(entt::handle sensor, entt::handle other, const std::vector<manifold_point>& manifolds);
    void on_sensor_exit(entt::handle sensor, entt::handle other, const std::vector<manifold_point>& manifolds);

    void on_collision_enter(entt::handle a, entt::handle b, const std::vector<manifold_point>& manifolds);
    void on_collision_exit(entt::handle a, entt::handle b, const std::vector<manifold_point>& manifolds);

    /**
     * @brief Called when a physics component is created.
     * @param r The registry containing the component.
     * @param e The entity associated with the component.
     */
    static void on_create_component(entt::registry& r, entt::entity e);
    static void on_create_active_component(entt::registry& r, entt::entity e);

    /**
     * @brief Called when a script component is loaded.
     * @param r The registry containing the component.
     * @param e The entity associated with the component.
     */
    static void on_load_component(entt::registry& r, entt::entity e);

    /**
     * @brief Called when a physics component is destroyed.
     * @param r The registry containing the component.
     * @param e The entity associated with the component.
     */
    static void on_destroy_component(entt::registry& r, entt::entity e);
    static void on_destroy_active_component(entt::registry& r, entt::entity e);


    /**
     * @brief Called when playback begins.
     * @param ctx The context for the playback.
     */
    void on_play_begin(rtti::context& ctx);
    void on_play_begin(hpp::span<const entt::handle> entities);
    void on_play_begin(entt::registry& entities);

private:
    /**
     * @brief Updates the physics system for each frame.
     * @param ctx The context for the update.
     * @param dt The delta time for the frame.
     */
    void on_frame_update(rtti::context& ctx, delta_t dt);
    void on_frame_fixed_update(rtti::context& ctx, delta_t dt);
    void on_frame_late_update(rtti::context& ctx, delta_t dt);

    /**
     * @brief Called when playback ends.
     * @param ctx The context for the playback.
     */
    void on_play_end(rtti::context& ctx);

    /**
     * @brief Called when playback is paused.
     * @param ctx The context for the playback.
     */
    void on_pause(rtti::context& ctx);

    /**
     * @brief Called when playback is resumed.
     * @param ctx The context for the playback.
     */
    void on_resume(rtti::context& ctx);

    /**
     * @brief Skips the next frame update.
     * @param ctx The context for the update.
     */
    void on_skip_next_frame(rtti::context& ctx);

    auto bind_internal_calls(rtti::context& ctx) -> bool;

    void check_for_recompile(rtti::context& ctx, delta_t dt, bool emit_callback);

    auto create_compilation_job(rtti::context& ctx, const std::string& protocol, bool debug) -> tpp::job_future<bool>;

    ///< Sentinel value to manage shared resources.
    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);

    delta_t time_since_last_check_{};

    dotnet::debugging_config debug_config_;
    std::unique_ptr<dotnet::domain> domain_;

    struct dotnet_cache
    {
        dotnet::type update_manager_type;
        dotnet::type script_system_type;
        dotnet::type native_component_type;
        dotnet::type script_component_type;
        
        // Cached methods to avoid repeated allocations every frame
        dotnet::method update_method;
        dotnet::method fixed_update_method;
        dotnet::method late_update_method;
    } cache_;

    std::unique_ptr<dotnet::domain> app_domain_;

    struct dotnet_app_cache
    {
        std::vector<dotnet::type> scriptable_component_types;

    } app_cache_;

    enum class call_progress
    {
        not_called,
        started,
        finished
    };

    call_progress create_call_{call_progress::not_called};
    bool is_updating_{};
    std::vector<tpp::future<void>> compilation_jobs_;

    bool has_compilation_errors_{};

    delta_t elapsed_time_{};
};
} // namespace unravel
