#include "world_parsers.hpp"

#include <cmath>
#include <unordered_map>

#include "logging.hpp"
#include "map_parser.hpp"
#include "options.hpp"

namespace bspc::builder
{
namespace
{

float DotProduct(const map::Vec3 &lhs, const map::Vec3 &rhs) noexcept
{
    return (lhs.x * rhs.x) + (lhs.y * rhs.y) + (lhs.z * rhs.z);
}

map::Vec3 CrossProduct(const map::Vec3 &lhs, const map::Vec3 &rhs) noexcept
{
    map::Vec3 result;
    result.x = lhs.y * rhs.z - lhs.z * rhs.y;
    result.y = lhs.z * rhs.x - lhs.x * rhs.z;
    result.z = lhs.x * rhs.y - lhs.y * rhs.x;
    return result;
}

bool Normalise(map::Vec3 &vector) noexcept
{
    const float magnitude = std::sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
    if (magnitude <= 1e-4f)
    {
        return false;
    }
    const float inv = 1.0f / magnitude;
    vector.x *= inv;
    vector.y *= inv;
    vector.z *= inv;
    return true;
}

ParsedWorld::Plane BuildPlane(const map::Plane &source, bool &valid)
{
    ParsedWorld::Plane plane{};

    const map::Vec3 &a = source.vertices[0];
    const map::Vec3 &b = source.vertices[1];
    const map::Vec3 &c = source.vertices[2];

    const map::Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z};
    const map::Vec3 v{c.x - a.x, c.y - a.y, c.z - a.z};

    map::Vec3 normal = CrossProduct(u, v);
    if (!Normalise(normal))
    {
        valid = false;
        return plane;
    }

    plane.normal[0] = normal.x;
    plane.normal[1] = normal.y;
    plane.normal[2] = normal.z;
    plane.dist = DotProduct(normal, a);
    plane.type = 0;
    valid = true;
    return plane;
}

std::size_t GetOrAddTexture(const std::string &name,
                             std::vector<ParsedWorld::Texture> &textures,
                             std::unordered_map<std::string, std::size_t> &index)
{
    auto found = index.find(name);
    if (found != index.end())
    {
        return found->second;
    }

    const std::size_t slot = textures.size();
    ParsedWorld::Texture texture;
    texture.name = name;
    texture.width = 0;
    texture.height = 0;
    textures.push_back(std::move(texture));
    index.emplace(name, slot);
    return slot;
}

void PopulateEntities(const map::ParseResult &parsed_map, ParsedWorld &world)
{
    world.entities.clear();
    world.entities.reserve(parsed_map.entities.size());
    for (const map::Entity &source_entity : parsed_map.entities)
    {
        ParsedWorld::Entity entity;
        entity.properties.reserve(source_entity.properties.size());
        for (const auto &kv : source_entity.properties)
        {
            ParsedWorld::KeyValue entry;
            entry.key = kv.key;
            entry.value = kv.value;
            entity.properties.push_back(std::move(entry));
        }
        world.entities.push_back(std::move(entity));
    }
}

} // namespace

bool PopulateWorldFromMap(std::string_view map_text, ParsedWorld &world, std::string &error)
{
    world.format = ParsedWorld::SourceFormat::kMap;
    world.bsp_version = 0;
    world.planes.clear();
    world.surfaces.clear();
    world.brushes.clear();
    world.textures.clear();
    world.entities.clear();
    world.nodes.clear();
    world.leaves.clear();

    Options options;
    const map::ParseResult parsed_map = map::ParseText(map_text, options, world.source_name);

    PopulateEntities(parsed_map, world);

    std::unordered_map<std::string, std::size_t> texture_index;
    texture_index.reserve(parsed_map.summary.unique_materials + 1);

    world.brushes.reserve(parsed_map.summary.brushes);
    world.planes.reserve(parsed_map.summary.planes);

    for (std::size_t entity_index = 0; entity_index < parsed_map.entities.size(); ++entity_index)
    {
        const map::Entity &source_entity = parsed_map.entities[entity_index];
        ParsedWorld::Entity &entity = world.entities[entity_index];

        for (const map::Brush &source_brush : source_entity.brushes)
        {
            ParsedWorld::Brush brush;
            brush.contains_liquid = source_brush.contains_liquid;

            for (const map::Plane &source_plane : source_brush.planes)
            {
                bool plane_valid = false;
                ParsedWorld::Plane plane = BuildPlane(source_plane, plane_valid);
                if (!plane_valid)
                {
                    log::Warning("Skipping degenerate plane while parsing map entity %zu", entity_index);
                    continue;
                }

                const std::size_t plane_slot = world.planes.size();
                world.planes.push_back(plane);

                ParsedWorld::Surface surface;
                surface.plane_index = plane_slot;
                surface.plane_side = false;
                surface.texture_index = GetOrAddTexture(source_plane.texture, world.textures, texture_index);
                surface.shift[0] = source_plane.shift[0];
                surface.shift[1] = source_plane.shift[1];
                surface.rotation = source_plane.rotation;
                surface.scale[0] = source_plane.scale[0];
                surface.scale[1] = source_plane.scale[1];

                const std::size_t surface_slot = world.surfaces.size();
                world.surfaces.push_back(surface);
                brush.surface_indices.push_back(surface_slot);
            }

            const std::size_t brush_slot = world.brushes.size();
            world.brushes.push_back(std::move(brush));
            entity.brush_indices.push_back(brush_slot);
        }
    }

    error.clear();
    return true;
}

} // namespace bspc::builder

