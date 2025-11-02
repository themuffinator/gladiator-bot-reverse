#pragma once

#include <filesystem>
#include <string>

namespace bspc
{
struct InputFile;
}

namespace bspc::bsp_to_map
{

bool ConvertBspToMap(const InputFile &input, const std::filesystem::path &destination, std::string &error);

} // namespace bspc::bsp_to_map

