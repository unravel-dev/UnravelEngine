#pragma once

#include "mcp_protocol.h"

#include <engine/assets/asset_handle.h>
#include <engine/rendering/material.h>

#include <context/context.hpp>
#include <ser20/external/simdjson/simdjson.h>

#include <string>
#include <vector>

namespace unravel::mcp
{

struct material_apply_result
{
    std::vector<std::string> applied;
    std::vector<std::string> unknown;
    std::vector<std::string> errors;
    bool ok{true};
};

auto list_material_property_schema_json() -> std::string;

auto resolve_material_asset(rtti::context& ctx, const std::string& key, const std::string& uid, std::string& error)
    -> asset_handle<material>;

auto as_pbr_material(const material::sptr& mat, std::string& error) -> std::shared_ptr<pbr_material>;

auto material_to_json(const material& mat) -> std::string;

auto apply_material_properties(rtti::context& ctx, material::sptr mat, const simdjson::dom::object& properties)
    -> material_apply_result;

auto save_material_asset(rtti::context& ctx, const asset_handle<material>& handle, std::string& error) -> bool;

auto normalize_material_key(const std::string& path_or_key) -> std::string;

} // namespace unravel::mcp
