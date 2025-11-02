#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "bsp_builder.hpp"

namespace bspc::builder
{

enum class WorldInputFormat
{
    kUnknown,
    kMapText,
    kBinaryBsp,
};

WorldInputFormat DetectWorldFormat(const std::vector<std::byte> &data) noexcept;

bool PopulateWorldFromMap(std::string_view map_text, ParsedWorld &world, std::string &error);

bool PopulateWorldFromBsp(const std::vector<std::byte> &data, ParsedWorld &world, std::string &error);

} // namespace bspc::builder

