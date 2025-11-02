#include "world_parsers.hpp"

#include <cmath>
#include <string>
#include <string_view>
#include <unordered_map>

#include "map_parser.hpp"
#include "options.hpp"

namespace bspc::builder
{
namespace
{

ParsedWorld::Vec3 ConvertVec(const map::Vec3 &value)
{
    return {value.x, value.y, value.z};
}

ParsedWorld::Plane ConvertPlane(const map::Plane &source)
{
    ParsedWorld::Plane plane{};
    if (source.has_normal)
    {
        plane.normal[0] = source.normal.x;
        plane.normal[1] = source.normal.y;
        plane.normal[2] = source.normal.z;
        plane.distance = source.distance;
        return plane;
    }

    if (source.has_vertices)
    {
        const map::Vec3 &a = source.vertices[0];
        const map::Vec3 &b = source.vertices[1];
        const map::Vec3 &c = source.vertices[2];
        const float ux = b.x - a.x;
        const float uy = b.y - a.y;
        const float uz = b.z - a.z;
        const float vx = c.x - a.x;
        const float vy = c.y - a.y;
        const float vz = c.z - a.z;
        float nx = (uy * vz) - (uz * vy);
        float ny = (uz * vx) - (ux * vz);
        float nz = (ux * vy) - (uy * vx);
        const float length = std::sqrt((nx * nx) + (ny * ny) + (nz * nz));
        if (length > 0.0f)
        {
            nx /= length;
            ny /= length;
            nz /= length;
            plane.normal[0] = nx;
            plane.normal[1] = ny;
            plane.normal[2] = nz;
            plane.distance = (nx * a.x) + (ny * a.y) + (nz * a.z);
        }
    }

    return plane;
}

} // namespace

bool PopulateWorldFromMapText(std::string_view source_name,
                              std::string_view map_text,
                              ParsedWorld &world,
                              std::string &error)
{
    Options options;
    map::ParseResult parsed = map::ParseText(map_text, options, source_name);
    if (parsed.had_errors)
    {
        error = "failed to parse MAP input";
        return false;
    }

    world = ParsedWorld{};
    world.format = ParsedWorld::Format::kMap;
    world.source_name = std::string(source_name);
    world.entities_text = EnsureEntitiesText(std::string(map_text));
    world.lines = SplitLines(map_text);
    world.map_info = ParsedWorld::MapMetadata{};
    world.map_info->entity_count = parsed.summary.entities;
    world.map_info->brush_count = parsed.summary.brushes;
    world.map_info->plane_count = parsed.summary.planes;
    world.map_info->unique_materials = parsed.summary.unique_materials;
    world.map_info->brush_merge_enabled = parsed.preprocess.brush_merge_enabled;
    world.map_info->csg_enabled = parsed.preprocess.csg_enabled;
    world.map_info->liquids_filtered = parsed.preprocess.liquids_filtered;
    world.map_info->breath_first = parsed.preprocess.breath_first;
    world.bsp_info.reset();

    std::unordered_map<std::string, std::size_t> texture_lookup;
    texture_lookup.reserve(parsed.summary.unique_materials + 1);

    auto acquire_texture = [&](std::string_view name) -> std::size_t {
        if (name.empty())
        {
            return ParsedWorld::kInvalidIndex;
        }
        std::string key(name);
        auto found = texture_lookup.find(key);
        if (found != texture_lookup.end())
        {
            return found->second;
        }
        const std::size_t index = world.textures.size();
        texture_lookup.emplace(key, index);
        ParsedWorld::Texture texture;
        texture.name = std::move(key);
        world.textures.push_back(std::move(texture));
        return index;
    };

    for (const map::Entity &entity : parsed.entities)
    {
        ParsedWorld::Entity converted_entity;
        converted_entity.properties.reserve(entity.properties.size());
        for (const map::KeyValue &kv : entity.properties)
        {
            converted_entity.properties.push_back({kv.key, kv.value});
        }

        for (const map::Brush &brush : entity.brushes)
        {
            ParsedWorld::Brush converted_brush;
            if (brush.type == map::Brush::Type::kPatch)
            {
                converted_brush.source = ParsedWorld::Brush::Source::kMapPatch;
            }
            else
            {
                converted_brush.source = ParsedWorld::Brush::Source::kMapBrush;
            }
            converted_brush.contains_liquid = brush.contains_liquid;
            converted_brush.is_brush_primitive = brush.is_brush_primitive;
            converted_brush.has_bounds = brush.has_bounds;
            converted_brush.mins = ConvertVec(brush.mins);
            converted_brush.maxs = ConvertVec(brush.maxs);

            if (brush.type == map::Brush::Type::kPatch && brush.patch.has_value())
            {
                const map::Patch &patch = *brush.patch;
                ParsedWorld::Patch converted_patch;
                converted_patch.texture_index = acquire_texture(patch.texture);
                converted_patch.width = patch.width;
                converted_patch.height = patch.height;
                converted_patch.contents = patch.contents;
                converted_patch.surface_flags = patch.surface_flags;
                converted_patch.value = patch.value;
                converted_patch.type = patch.type;
                converted_patch.points.reserve(patch.points.size());
                for (const map::PatchPoint &point : patch.points)
                {
                    ParsedWorld::PatchPoint converted_point;
                    converted_point.position = ConvertVec(point.position);
                    converted_point.st[0] = point.st[0];
                    converted_point.st[1] = point.st[1];
                    converted_patch.points.push_back(converted_point);
                }
                converted_brush.patch = std::move(converted_patch);
            }

            converted_brush.sides.reserve(brush.planes.size());
            for (const map::Plane &plane : brush.planes)
            {
                const std::size_t plane_index = world.planes.size();
                world.planes.push_back(ConvertPlane(plane));

                ParsedWorld::BrushSide side;
                side.plane_index = plane_index;
                side.texture_index = acquire_texture(plane.texture);
                side.contents = plane.contents;
                side.surface_flags = plane.surface_flags;
                side.value = plane.value;
                converted_brush.sides.push_back(side);
            }

            const std::size_t brush_index = world.brushes.size();
            converted_entity.brushes.push_back(brush_index);
            world.brushes.push_back(std::move(converted_brush));
        }

        world.entities.push_back(std::move(converted_entity));
    }

    world.bsp_type = ParsedWorld::BspType::kQuake2;
    return true;
}

} // namespace bspc::builder

