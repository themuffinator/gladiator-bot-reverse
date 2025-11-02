#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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
    static constexpr std::size_t kInvalidIndex = static_cast<std::size_t>(-1);

    enum class Format
    {
        kUnknown,
        kMap,
        kBsp,
    };

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct KeyValue
    {
        std::string key;
        std::string value;
    };

    struct PatchPoint
    {
        Vec3 position{};
        float st[2]{0.0f, 0.0f};
    };

    struct Patch
    {
        std::size_t texture_index = kInvalidIndex;
        int width = 0;
        int height = 0;
        int contents = 0;
        int surface_flags = 0;
        int value = 0;
        int type = 0;
        std::vector<PatchPoint> points;
    };

    struct BrushSide
    {
        std::size_t plane_index = kInvalidIndex;
        std::size_t texture_index = kInvalidIndex;
        int contents = 0;
        int surface_flags = 0;
        int value = 0;
    };

    struct Brush
    {
        enum class Source
        {
            kNone,
            kMapBrush,
            kMapPatch,
            kBspModel,
        };

        Source source = Source::kNone;
        std::vector<BrushSide> sides;
        std::vector<std::size_t> surface_indices;
        std::optional<Patch> patch;
        bool contains_liquid = false;
        bool is_brush_primitive = false;
        bool has_bounds = false;
        Vec3 mins{};
        Vec3 maxs{};
    };

    struct Texture
    {
        std::string name;
        int width = 0;
        int height = 0;
    };

    struct Plane
    {
        float normal[3]{0.0f, 0.0f, 0.0f};
        float distance = 0.0f;
        std::int32_t type = 0;
    };

    struct Surface
    {
        std::size_t plane_index = kInvalidIndex;
        bool plane_side = false;
        std::size_t texture_index = kInvalidIndex;
        std::int32_t first_edge = 0;
        std::int32_t edge_count = 0;
        std::array<std::uint8_t, 4> styles{0, 0, 0, 0};
        std::int32_t lightmap_offset = 0;
        std::int16_t texinfo = -1;
    };

    struct Entity
    {
        std::vector<KeyValue> properties;
        std::vector<std::size_t> brushes;

        std::optional<std::string_view> FindProperty(std::string_view key) const
        {
            for (const auto &property : properties)
            {
                if (property.key == key)
                {
                    return std::string_view(property.value);
                }
            }
            return std::nullopt;
        }
    };

    struct MapMetadata
    {
        std::size_t entity_count = 0;
        std::size_t brush_count = 0;
        std::size_t plane_count = 0;
        std::size_t unique_materials = 0;
        bool brush_merge_enabled = false;
        bool csg_enabled = false;
        bool liquids_filtered = false;
        bool breath_first = false;
    };

    struct BspMetadata
    {
        std::size_t plane_count = 0;
        std::size_t node_count = 0;
        std::size_t leaf_count = 0;
        std::size_t model_count = 0;
    };

    Format format = Format::kUnknown;
    std::string source_name;
    std::string entities_text;
    std::vector<std::string> lines;
    std::vector<Texture> textures;
    std::vector<Plane> planes;
    std::vector<Surface> surfaces;
    std::vector<Brush> brushes;
    std::vector<Entity> entities;
    std::optional<MapMetadata> map_info;
    std::optional<BspMetadata> bsp_info;
};

struct BspBuildArtifacts
{
    std::array<formats::OwnedLump, formats::kQuake1LumpCount> lumps{};
    std::string portal_text;
    std::string leak_text;
    std::size_t portal_cluster_count = 0;
    std::size_t portal_count = 0;
    std::size_t leak_point_count = 0;

    void Reset() noexcept;
};

bool LoadWorldState(const InputFile &input, ParsedWorld &out_world, std::string &error);

bool BuildBspTree(const ParsedWorld &world, BspBuildArtifacts &out_artifacts);

std::array<formats::LumpView, formats::kQuake1LumpCount> MakeLumpViews(const BspBuildArtifacts &artifacts) noexcept;

void FreeTree(BspBuildArtifacts &artifacts) noexcept;

} // namespace bspc::builder

