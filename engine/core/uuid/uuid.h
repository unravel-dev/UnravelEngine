#pragma once

#include <hpp/uuid.hpp>

namespace unravel
{

auto generate_uuid() -> hpp::uuid;
auto generate_uuid(const std::string& key) -> hpp::uuid;

}
