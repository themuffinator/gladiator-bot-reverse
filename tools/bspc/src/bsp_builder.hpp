#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "bsp_formats.hpp"

namespace bspc
{
struct InputFile;
struct Options;
}

namespace bspc::builder
{

struct ParsedWorld
{
    std::string source_name;
    std::string entities_text;
    std::vector<std::string> lines;
};

struct BspBuildArtifacts
{
    std::array<formats::OwnedLump, formats::kQuake1LumpCount> lumps{};
    std::string portal_text;
    std::string leak_text;
    std::size_t portal_slice_count = 0;
    std::size_t flood_fill_regions = 0;

    void Reset() noexcept;
};

bool LoadWorldState(const InputFile &input, ParsedWorld &out_world, std::string &error);

bool BuildBspTree(const ParsedWorld &world, BspBuildArtifacts &out_artifacts);

std::array<formats::LumpView, formats::kQuake1LumpCount> MakeLumpViews(const BspBuildArtifacts &artifacts) noexcept;

void FreeTree(BspBuildArtifacts &artifacts) noexcept;

} // namespace bspc::builder

