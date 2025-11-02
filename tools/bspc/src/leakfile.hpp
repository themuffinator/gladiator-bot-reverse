#pragma once

#include <string>
#include <string_view>

#include "legacy_common.hpp"

namespace bspc::legacy
{

std::string LeakFile(const Tree &tree, std::string_view source_name);

} // namespace bspc::legacy

