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
    enum class SourceFormat
    {
        kUnknown,
        kMap,
        kQuake1Bsp,
        kHalfLifeBsp,
    };

    struct KeyValue
    {
        std::string key;
        std::string value;
    };

    struct Plane
    {
        float normal[3] = {0.0f, 0.0f, 1.0f};
        float dist = 0.0f;
        std::int32_t type = 0;
    };

    struct Texture
    {
        std::string name;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    struct Surface
    {
        std::size_t plane_index = 0;
        std::size_t texture_index = 0;
        std::int32_t texinfo = -1;
        std::int32_t first_edge = 0;
        std::int32_t edge_count = 0;
        bool plane_side = false;
        float shift[2] = {0.0f, 0.0f};
        float rotation = 0.0f;
        float scale[2] = {1.0f, 1.0f};
        std::array<std::uint8_t, 4> styles = {0, 0, 0, 0};
        std::int32_t lightmap_offset = -1;
    };

    struct Brush
    {
        std::vector<std::size_t> surface_indices;
        bool contains_liquid = false;
    };

    struct Entity
    {
        std::vector<KeyValue> properties;
        std::vector<std::size_t> brush_indices;
    };

    struct Node
    {
        std::int32_t plane_index = -1;
        std::array<std::int32_t, 2> children = {-1, -1};
        std::array<std::int16_t, 3> mins = {0, 0, 0};
        std::array<std::int16_t, 3> maxs = {0, 0, 0};
        std::uint16_t first_face = 0;
        std::uint16_t face_count = 0;
    };

    struct Leaf
    {
        std::int32_t contents = 0;
        std::array<std::int16_t, 3> mins = {0, 0, 0};
        std::array<std::int16_t, 3> maxs = {0, 0, 0};
        std::uint16_t first_mark_surface = 0;
        std::uint16_t mark_surface_count = 0;
        std::array<std::uint8_t, 4> ambient_levels = {0, 0, 0, 0};
    };

    SourceFormat format = SourceFormat::kUnknown;
    std::int32_t bsp_version = 0;
    std::string source_name;
    std::string entities_text;
    std::vector<std::string> lines;
    std::vector<Texture> textures;
    std::vector<Plane> planes;
    std::vector<Surface> surfaces;
    std::vector<Brush> brushes;
    std::vector<Entity> entities;
    std::vector<Node> nodes;
    std::vector<Leaf> leaves;
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

