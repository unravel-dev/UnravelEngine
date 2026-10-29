#pragma once
#include <engine/engine_export.h>
#include <engine/physics/ecs/components/physics_component.h>
#include <engine/scripting/ecs/components/script_component.h>

#include <engine/threading/threader.h>

#include <base/basetypes.hpp>
#include <cmd_line/parser.h>
#include <context/context.hpp>
#include <engine/frame_times.h>
#include <filesystem/filesystem.h>
#include <hpp/span.hpp>

#include <dotnetpp/dotnetpp.h>

#include <functional>
#include <map>
#include <set>
#include <variant>

namespace unravel
{

class asset_manager;

struct script_system
{
    /// Builds the script library at key into output (flags: script_library::compilation_flags).
    using library_builder =
        std::function<bool(asset_manager& am, const fs::path& key, const fs::path& output, uint32_t flags)>;

    /**
     * @brief Installs what builds script libraries. The engine owns no compiler: the editor
     *        installs its asset compiler's script build before the engine's systems initialize,
     *        and a host without one (the player) only loads libraries built beforehand.
     */
    static void set_library_builder(library_builder builder);
    static void set_needs_recompile(const std::string& protocol, bool now = false);
    static auto get_script_debug_mode() -> bool;
    static void set_script_debug_mode(bool debug);
    static auto get_lib_name(const std::string& protocol) -> std::string;
    static auto get_lib_data_key(const std::string& protocol) -> std::string;
    static auto get_lib_temp_compiled_key(const std::string& protocol) -> std::string;
    static auto get_lib_compiled_key(const std::string& protocol) -> std::string;
    /// Moves a compiled library dll with its pdb/mdb/xml. A companion the source lacks is removed
    /// at the destination, so the dll never sits beside another build's symbols. Does nothing
    /// when the dll cannot be copied.
    static void copy_compiled_lib(const fs::path& from, const fs::path& to);
    /// Removes a compiled library dll with its pdb/mdb/xml.
    static void remove_compiled_lib(const fs::path& lib);
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

    /// Loads the app scripts' domain. With @p recompile the library is built first, unless this
    /// process already built it from the sources as they are now.
    auto load_app_domain(rtti::context& ctx, bool recompile) -> bool;
    void unload_app_domain();

    /// The script components an entity can take: the app's, then the engine's (the samples) that no
    /// app type of the same name shadows.
    auto get_all_scriptable_components() const -> const std::vector<dotnet::type>&;
    auto get_scriptable_component_base_type() const -> dotnet::type;
    auto get_engine_assembly() const -> dotnet::assembly;
    auto get_app_assembly() const -> dotnet::assembly;

    /**
     * @brief Searches the public scripting API - the engine's types and the game's - for every word
     * of @p query in type and member names and documentation.
     * @param limit Maximum number of results, 0 for the default.
     * @return A count line, then one result per line: the member, its C# signature and summary.
     */
    auto search_api(const std::string& query, int limit) const -> std::string;

    /**
     * @brief Describes one scripting API type: its declaration and every public member with its
     * C# signature and summary.
     * @param type_name Full or short type name, case-insensitive.
     */
    auto describe_api_type(const std::string& type_name) const -> std::string;

    /// Blittable frame payloads for the managed SystemManager hooks (layouts
    /// mirror the managed UpdateInfo structs).
    struct frame_update_data
    {
        float time{};
        float delta_time{};
        float time_scale{};
        uint64_t frame_count{};
        float unscaled_delta_time{};
        float realtime_since_startup{};
    };

    struct fixed_update_data
    {
        float fixed_delta_time{};
    };

    /// Blittable UI event payload for UIEventManager.InternalDispatchPayload (layout mirrors the
    /// managed UIEventPayload). Strings travel as separate arguments.
    struct ui_event_payload
    {
        int64_t native_event{};
        int64_t target_handle{};
        int64_t current_handle{};
        /// Element an event refers to besides its target: the dragged element of drag events.
        int64_t related_handle{};
        int32_t kind{};
        int32_t phase{};
        int32_t is_capture_listener{};
        int32_t button{-1};
        int32_t key_code{};
        /// Bits: 1 ctrl, 2 shift, 4 alt, 8 meta.
        int32_t modifiers{};
        int32_t click_count{};
        float x{};
        float y{};
        float delta_x{};
        float delta_y{};
        float value{};
        float min_value{};
        float max_value{};
        float step{};
    };

    using ui_dispatch_signature =
        void(ui_event_payload, std::string, std::string, std::string, std::string);

    // UIDataModels callbacks: a model node is a managed id; a child id carries its kind in the low bits.
    using ui_model_event_signature = void(entt::entity, std::string, std::string, std::string, ui_event_payload,
                                          std::string, std::string, std::string, std::string);
    using ui_inline_event_signature =
        void(entt::entity, std::string, ui_event_payload, std::string, std::string, std::string, std::string);

    /// Cached engine-assembly types/methods. Valid between load_engine_domain
    /// and unload_engine_domain.
    struct engine_script_cache
    {
        dotnet::type update_manager_type;
        dotnet::type script_system_type;
        dotnet::type component_type;
        dotnet::type script_component_type;
        dotnet::type ui_event_manager_type;

        // SystemManager frame hooks
        dotnet::method begin_frame_method;
        dotnet::method update_method;
        dotnet::method fixed_update_method;
        dotnet::method late_update_method;

        // Frame-hook invokers, constructed (and signature-checked) once per
        // engine domain load; building one per frame costs bridge round-trips
        // for the signature validation alone. Default-constructed instances
        // are invalid until load_engine_domain assigns them.
        dotnet::method_invoker<void(frame_update_data)> begin_frame_invoker;
        dotnet::method_invoker<void(frame_update_data)> update_invoker;
        dotnet::method_invoker<void(fixed_update_data)> fixed_update_invoker;
        dotnet::method_invoker<void()> late_update_invoker;

        // Component / ScriptComponent lifecycle (resolved once per engine domain)
        dotnet::method set_entity_method;
        dotnet::method on_create_method;
        dotnet::method on_enable_method;
        dotnet::method on_disable_method;
        dotnet::method on_start_method;
        dotnet::method on_destroy_method;
        dotnet::method on_sensor_enter_method;
        dotnet::method on_sensor_exit_method;
        dotnet::method on_collision_enter_method;
        dotnet::method on_collision_exit_method;
        // ScriptComponent.internal_n2m_on_animation_event(name, string, float, int, layer, weight, clip, clip name)
        dotnet::method on_animation_event_method;
        // ScriptComponent.internal_n2m_invoke(method name, encoded arguments) -> JSON, for tools
        dotnet::method invoke_method;

        // UIEventManager.InternalDispatchPayload(payload, event type, target id, current id, text)
        dotnet::method ui_dispatch_method;
        dotnet::method_invoker<ui_dispatch_signature> ui_dispatch_invoker;

        // UIDataModels: values of bound data model variables, model events, transforms, inline handlers
        dotnet::type ui_data_models_type;
        dotnet::method_invoker<std::string(int64_t)> ui_model_get_invoker;
        dotnet::method_invoker<void(int64_t, std::string)> ui_model_set_invoker;
        dotnet::method_invoker<int32_t(int64_t)> ui_model_size_invoker;
        dotnet::method_invoker<int64_t(int64_t, std::string, int32_t)> ui_model_child_invoker;
        dotnet::method_invoker<ui_model_event_signature> ui_model_event_invoker;
        dotnet::method_invoker<std::string(entt::entity, std::string, std::string)> ui_model_transform_invoker;
        dotnet::method_invoker<ui_inline_event_signature> ui_inline_event_invoker;
        // UIDataModels.internal_n2m_describe(owner, model, max depth, max items) -> JSON
        dotnet::method_invoker<std::string(entt::entity, std::string, int32_t, int32_t)> ui_model_describe_invoker;
    };

    auto get_cache() const -> const engine_script_cache&
    {
        return cache_;
    }

    /**
     * @brief The data models scripts bound for @p owner's UI document, as JSON: variables with their
     * current values, event and transform names (see UIDataModels.internal_n2m_describe).
     * @param model One model's name, or empty for all of them.
     * @param max_depth Levels of nested objects and lists to expand.
     * @param max_items Entries of a list to include.
     * @return {"models":[...]}; an empty list when no script bound a model, null when scripts are not loaded.
     */
    auto describe_ui_models(entt::entity owner, const std::string& model, int max_depth, int max_items) const
        -> std::string;

    /// An argument for invoke_script_method: a flag, an integer, a number, text, or numbers that
    /// fill a vector or colour parameter.
    using script_argument = std::variant<bool, int64_t, double, std::string, std::vector<double>>;

    /**
     * @brief Runs a public instance method of a script by name, for tools. The arguments convert to
     * the method's parameter types; missing trailing ones take their defaults (see
     * ScriptComponent.internal_n2m_invoke).
     * @return {"result":value} or {"error":"message"} as JSON; null when scripts are not loaded.
     */
    auto invoke_script_method(const dotnet::object& script,
                              const std::string& method,
                              const std::vector<script_argument>& arguments) const -> std::string;

    auto get_type_by_fullname(const std::string& fullname) const -> dotnet::type;
    auto get_type(const std::string& name_space, const std::string& name) const -> dotnet::type;

    auto is_create_called() const -> bool;
    auto is_update_called() const -> bool;
    void wait_for_jobs_to_finish(rtti::context& ctx);
    auto has_compilation_errors() const -> bool;
    /// True until a source edit's build is in use: while the build is queued or running, and
    /// after it succeeds until its library is loaded - the editor reloads it a frame or more
    /// later, and the new types are not there before. Main thread.
    auto is_build_pending(const std::string& protocol) const -> bool;

    void on_sensor_enter(entt::handle sensor, entt::handle other, const std::vector<manifold_point>& manifolds);
    void on_sensor_exit(entt::handle sensor,
                        entt::handle other,
                        const std::vector<manifold_point>& manifolds,
                        contact_end_reason reason);

    void on_collision_enter(entt::handle a, entt::handle b, const std::vector<manifold_point>& manifolds);

    /**
     * @brief Dispatches a collision exit to both participants.
     * @param reason Stated relative to @p a. @p b receives the mirrored reason.
     */
    void on_collision_exit(entt::handle a,
                           entt::handle b,
                           const std::vector<manifold_point>& manifolds,
                           contact_end_reason reason);

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
     * @param times The frame's times.
     */
    void on_frame_update(rtti::context& ctx, const frame_times& times);
    void on_frame_fixed_update(rtti::context& ctx, delta_t dt);
    void on_frame_late_update(rtti::context& ctx, const frame_times& times);

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

    auto bind_internal_calls(rtti::context& ctx) -> bool;

    /// The assembly's script component types an entity can take: the ones that are not abstract.
    auto get_addable_script_component_types(const dotnet::assembly& assembly) const -> std::vector<dotnet::type>;
    /// Lists the engine's script components after the app's, leaving out the ones an app type shadows.
    void append_engine_scriptable_component_types();

    void check_for_recompile(rtti::context& ctx, delta_t dt, bool emit_callback);

    /**
     * @brief Delivers the animation events every player recorded this frame to the scripts on the
     * animated entity (or, when it has none, its nearest ancestor with scripts).
     *
     * Runs on the main thread: players record events on the pool threads the animation system
     * runs on, and managed code must not be entered from there.
     */
    void dispatch_animation_events(scene& scn);

    auto create_compilation_job(rtti::context& ctx, const std::string& protocol, bool debug) -> tpp::job_future<bool>;

    /// True when the last build of @p protocol's library succeeded, in @p debug mode, from sources
    /// with no change queued or saved since it read them, and the library is still there.
    static auto is_library_current(rtti::context& ctx, const std::string& protocol, bool debug) -> bool;

    /// True until the last build scheduled for the protocol's library has finished.
    auto is_compiling(const std::string& protocol) const -> bool;
    void wait_for_compilation_jobs();

    ///< Sentinel value to manage shared resources.
    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);

    delta_t time_since_last_check_{};

    dotnet::debugging_config debug_config_;
    std::unique_ptr<dotnet::domain> domain_;

    engine_script_cache cache_;

    std::unique_ptr<dotnet::domain> app_domain_;

    struct app_script_cache
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
    ///< Last scheduled library build per protocol.
    std::map<std::string, tpp::future<void>> compilation_jobs_;

    /// Protocols whose successful build has been announced (on_script_recompile) and whose
    /// library has not been loaded since.
    std::set<std::string> reloads_pending_;

    bool has_compilation_errors_{};

    delta_t elapsed_time_{};
};
} // namespace unravel
