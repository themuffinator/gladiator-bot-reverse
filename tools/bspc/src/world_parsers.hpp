#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "bsp_builder.hpp"
#include "bsp_formats.hpp"

namespace bspc::builder
{

std::string EnsureEntitiesText(std::string entities_text);
std::vector<std::string> SplitLines(std::string_view text);

bool PopulateWorldFromMapText(std::string_view source_name,
                              std::string_view map_text,
                              ParsedWorld &world,
                              std::string &error);

bool PopulateWorldFromBspBinary(std::string_view source_name,
                                formats::ConstByteSpan data,
                                ParsedWorld &world,
                                std::string &error);

} // namespace bspc::builder

