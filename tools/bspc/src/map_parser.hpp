#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "filesystem_helper.h"
#include "options.hpp"

namespace bspc::map
{

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Plane
{
    Vec3 vertices[3]{};
    Vec3 normal{0.0f, 0.0f, 0.0f};
    float distance = 0.0f;
    bool has_vertices = false;
    bool has_normal = false;
    std::string texture;
    float shift[2]{0.0f, 0.0f};
    float rotation = 0.0f;
    float scale[2]{1.0f, 1.0f};
    float matrix[2][3]{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    bool has_matrix = false;
    int contents = 0;
    int surface_flags = 0;
    int value = 0;
};

struct PatchPoint
{
    Vec3 position{};
    float st[2]{0.0f, 0.0f};
};

struct Patch
{
    std::string texture;
    int width = 0;
    int height = 0;
    int contents = 0;
    int surface_flags = 0;
    int value = 0;
    int type = 0;
    std::vector<PatchPoint> points;
};

struct Brush
{
    enum class Type
    {
        kSolid,
        kPatch,
    };

    Type type = Type::kSolid;
    std::vector<Plane> planes;
    std::optional<Patch> patch;
    bool contains_liquid = false;
    bool is_brush_primitive = false;
    Vec3 mins{0.0f, 0.0f, 0.0f};
    Vec3 maxs{0.0f, 0.0f, 0.0f};
    bool has_bounds = false;
};

struct KeyValue
{
    std::string key;
    std::string value;
};

struct Entity
{
    std::vector<KeyValue> properties;
    std::vector<Brush> brushes;

    std::optional<std::string_view> FindProperty(std::string_view key) const;
};

struct Summary
{
    std::size_t entities = 0;
    std::size_t brushes = 0;
    std::size_t planes = 0;
    std::size_t unique_materials = 0;
};

struct PreprocessReport
{
    bool brush_merge_enabled = false;
    bool csg_enabled = false;
    bool liquids_filtered = false;
    bool breath_first = false;
};

struct ParseResult
{
    std::vector<Entity> entities;
    Summary summary;
    PreprocessReport preprocess;
    bool had_errors = false;
};

ParseResult ParseText(std::string_view map_text, const Options &options, std::string_view source_name = {});
std::optional<ParseResult> ParseMapFromFile(const InputFile &input, const Options &options);

} // namespace bspc::map

