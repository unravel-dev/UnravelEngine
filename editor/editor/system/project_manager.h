#pragma once
#include <context/context.hpp>
#include <engine/settings/settings.h>
#include <editor/settings/settings.h>
#include <editor/deploy/deploy.h>
#include <filesystem/syncer.h>
#include <cmd_line/parser.h>


namespace unravel
{
class project_manager
{
public:
    project_manager(rtti::context& ctx, cmd_line::parser& parser);
    ~project_manager();

    auto init(rtti::context& ctx, const cmd_line::parser& parser) -> bool;
    auto deinit(rtti::context& ctx) -> bool;


    auto open_project(rtti::context& ctx, const fs::path& project_path) -> bool;

    void close_project(rtti::context& ctx);

    void create_project(rtti::context& ctx, const fs::path& project_path);

    void save_editor_settings();

    void load_editor_settings();

    auto get_name() const -> const std::string&;

    void set_name(const std::string& name);

    auto get_settings() -> settings&;
    auto get_deploy_settings() -> deploy_settings&;
    auto get_editor_settings() -> editor_settings&;


    auto has_open_project() const -> bool;

    void load_project_settings();
    void save_project_settings(rtti::context& ctx);
    void load_deploy_settings();
    void save_deploy_settings();

private:
    void fixup_editor_settings_on_save();
    void fixup_editor_settings_on_load();

    void setup_directory(rtti::context& ctx, fs::syncer& syncer);
    void setup_meta_syncer(rtti::context& ctx, fs::syncer& syncer, const fs::path& data_dir, const fs::path& meta_dir);
    void setup_cache_syncer(rtti::context& ctx,
                            std::vector<uint64_t>& watchers,
                            fs::syncer& syncer,
                            const fs::path& meta_dir,
                            const fs::path& cache_dir);
    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);


    /// Current project name
    std::string project_name_;
    settings project_settings_;
    deploy_settings deploy_settings_;
    editor_settings editor_settings_;

    fs::syncer app_meta_syncer_;
    fs::syncer app_cache_syncer_;
    std::vector<std::uint64_t> app_watchers_;

    fs::syncer editor_meta_syncer_;
    fs::syncer editor_cache_syncer_;
    std::vector<std::uint64_t> editor_watchers_;

    fs::syncer engine_meta_syncer_;
    fs::syncer engine_cache_syncer_;
    std::vector<std::uint64_t> engine_watchers_;
};
} // namespace unravel
