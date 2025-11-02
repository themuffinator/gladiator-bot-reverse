#include "world_parsers.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace bspc::builder
{
namespace
{

struct Quake1PlaneDisk
{
    float normal[3];
    float dist;
    std::int32_t type;
};
static_assert(sizeof(Quake1PlaneDisk) == 20, "unexpected Quake 1 plane layout");

struct Quake1TexInfoDisk
{
    float vecs[2][4];
    std::int32_t miptex;
    std::int32_t flags;
};
static_assert(sizeof(Quake1TexInfoDisk) == 40, "unexpected Quake 1 texinfo layout");

struct Quake1FaceDisk
{
    std::uint16_t planenum;
    std::int16_t side;
    std::int32_t firstedge;
    std::int16_t numedges;
    std::int16_t texinfo;
    std::uint8_t styles[4];
    std::int32_t lightofs;
};
static_assert(sizeof(Quake1FaceDisk) == 20, "unexpected Quake 1 face layout");

struct Quake1ModelDisk
{
    float mins[3];
    float maxs[3];
    float origin[3];
    std::int32_t headnode[4];
    std::int32_t visleafs;
    std::int32_t firstface;
    std::int32_t numfaces;
};
static_assert(sizeof(Quake1ModelDisk) == 64, "unexpected Quake 1 model layout");

struct Quake1NodeDisk
{
    std::int32_t planenum;
    std::int16_t children[2];
    std::int16_t mins[3];
    std::int16_t maxs[3];
    std::uint16_t firstface;
    std::uint16_t numfaces;
};
static_assert(sizeof(Quake1NodeDisk) == 24, "unexpected Quake 1 node layout");

struct Quake1LeafDisk
{
    std::int32_t contents;
    std::int32_t visofs;
    std::int16_t mins[3];
    std::int16_t maxs[3];
    std::uint16_t firstmarksurface;
    std::uint16_t nummarksurfaces;
    std::uint8_t ambient_level[4];
};
static_assert(sizeof(Quake1LeafDisk) == 28, "unexpected Quake 1 leaf layout");

struct MiptexHeader
{
    std::int32_t count;
};

struct MiptexDisk
{
    char name[16];
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t offsets[4];
};
static_assert(sizeof(MiptexDisk) == 40, "unexpected Quake 1 miptex layout");

struct TexInfoEntry
{
    std::size_t texture_index = ParsedWorld::kInvalidIndex;
    std::int32_t flags = 0;
};

bool ParseEntities(std::string_view text, std::vector<ParsedWorld::Entity> &entities, std::string &error)
{
    entities.clear();
    std::size_t position = 0;

    auto skip_whitespace = [&](void) {
        while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])))
        {
            ++position;
        }
    };

    auto parse_string = [&](std::string &out) -> bool {
        if (position >= text.size() || text[position] != '"')
        {
            error = "malformed entity string";
            return false;
        }
        ++position;
        out.clear();
        while (position < text.size())
        {
            char ch = text[position++];
            if (ch == '"')
            {
                return true;
            }
            if (ch == '\\' && position < text.size())
            {
                char escape = text[position++];
                switch (escape)
                {
                case 'n':
                    out.push_back('\n');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case '\\':
                    out.push_back('\\');
                    break;
                case '"':
                    out.push_back('"');
                    break;
                default:
                    out.push_back(escape);
                    break;
                }
            }
            else
            {
                out.push_back(ch);
            }
        }
        error = "unterminated entity string";
        return false;
    };

    std::string key;
    std::string value;
    while (true)
    {
        skip_whitespace();
        if (position >= text.size())
        {
            break;
        }

        if (text[position] != '{')
        {
            ++position;
            continue;
        }

        ++position;
        ParsedWorld::Entity entity;
        while (true)
        {
            skip_whitespace();
            if (position >= text.size())
            {
                error = "unterminated entity block";
                return false;
            }
            if (text[position] == '}')
            {
                ++position;
                break;
            }

            if (!parse_string(key))
            {
                return false;
            }
            skip_whitespace();
            if (!parse_string(value))
            {
                return false;
            }
            entity.properties.push_back({key, value});
        }
        entities.push_back(std::move(entity));
    }

    return true;
}

bool ParseTextures(const formats::LumpView &lump, ParsedWorld &world, std::string &error)
{
    world.textures.clear();
    if (lump.size == 0)
    {
        return true;
    }

    if (lump.data == nullptr || lump.size < sizeof(MiptexHeader))
    {
        error = "textures lump truncated";
        return false;
    }

    const auto *header = reinterpret_cast<const MiptexHeader *>(lump.data);
    const std::int32_t texture_count = header->count;
    if (texture_count < 0)
    {
        error = "negative texture count in BSP";
        return false;
    }

    const std::size_t directory_offset = sizeof(MiptexHeader);
    const std::size_t directory_length = static_cast<std::size_t>(texture_count) * sizeof(std::int32_t);
    if (lump.size < directory_offset + directory_length)
    {
        error = "texture directory truncated";
        return false;
    }

    const auto *offsets = reinterpret_cast<const std::int32_t *>(lump.data + directory_offset);
    world.textures.reserve(static_cast<std::size_t>(texture_count));

    for (std::int32_t i = 0; i < texture_count; ++i)
    {
        const std::int32_t offset = offsets[i];
        if (offset < 0)
        {
            world.textures.push_back({std::string(), 0, 0});
            continue;
        }
        if (static_cast<std::size_t>(offset) + sizeof(MiptexDisk) > lump.size)
        {
            error = "texture definition truncated";
            return false;
        }
        const auto *miptex = reinterpret_cast<const MiptexDisk *>(lump.data + offset);
        std::string name(miptex->name, miptex->name + sizeof(miptex->name));
        const auto null_pos = name.find('\0');
        if (null_pos != std::string::npos)
        {
            name.resize(null_pos);
        }
        ParsedWorld::Texture texture;
        texture.name = std::move(name);
        texture.width = static_cast<int>(miptex->width);
        texture.height = static_cast<int>(miptex->height);
        world.textures.push_back(std::move(texture));
    }

    return true;
}

bool ParsePlanes(const formats::LumpView &lump, ParsedWorld &world, std::string &error)
{
    world.planes.clear();
    if (lump.size == 0)
    {
        return true;
    }
    if (lump.data == nullptr || (lump.size % sizeof(Quake1PlaneDisk)) != 0)
    {
        error = "planes lump misaligned";
        return false;
    }

    const auto count = lump.size / sizeof(Quake1PlaneDisk);
    const auto *planes = reinterpret_cast<const Quake1PlaneDisk *>(lump.data);
    world.planes.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        ParsedWorld::Plane plane{};
        plane.normal[0] = planes[i].normal[0];
        plane.normal[1] = planes[i].normal[1];
        plane.normal[2] = planes[i].normal[2];
        plane.distance = planes[i].dist;
        plane.type = planes[i].type;
        world.planes.push_back(plane);
    }
    return true;
}

bool ParseTexInfo(const formats::LumpView &lump,
                  std::vector<TexInfoEntry> &out,
                  std::size_t texture_count,
                  std::string &error)
{
    out.clear();
    if (lump.size == 0)
    {
        return true;
    }
    if (lump.data == nullptr || (lump.size % sizeof(Quake1TexInfoDisk)) != 0)
    {
        error = "texinfo lump misaligned";
        return false;
    }

    const auto count = lump.size / sizeof(Quake1TexInfoDisk);
    const auto *texinfos = reinterpret_cast<const Quake1TexInfoDisk *>(lump.data);
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        TexInfoEntry entry;
        if (texinfos[i].miptex >= 0 && static_cast<std::size_t>(texinfos[i].miptex) < texture_count)
        {
            entry.texture_index = static_cast<std::size_t>(texinfos[i].miptex);
        }
        entry.flags = texinfos[i].flags;
        out.push_back(entry);
    }
    return true;
}

bool ParseSurfaces(const formats::LumpView &lump,
                   const std::vector<TexInfoEntry> &texinfo,
                   ParsedWorld &world,
                   std::string &error)
{
    world.surfaces.clear();
    if (lump.size == 0)
    {
        return true;
    }
    if (lump.data == nullptr || (lump.size % sizeof(Quake1FaceDisk)) != 0)
    {
        error = "faces lump misaligned";
        return false;
    }

    const auto count = lump.size / sizeof(Quake1FaceDisk);
    const auto *faces = reinterpret_cast<const Quake1FaceDisk *>(lump.data);
    world.surfaces.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (faces[i].planenum >= world.planes.size())
        {
            error = "face references invalid plane";
            return false;
        }
        ParsedWorld::Surface surface;
        surface.plane_index = faces[i].planenum;
        surface.plane_side = faces[i].side != 0;
        surface.first_edge = faces[i].firstedge;
        surface.edge_count = faces[i].numedges;
        surface.texinfo = faces[i].texinfo;
        std::copy(std::begin(faces[i].styles), std::end(faces[i].styles), surface.styles.begin());
        surface.lightmap_offset = faces[i].lightofs;
        if (faces[i].texinfo >= 0 && static_cast<std::size_t>(faces[i].texinfo) < texinfo.size())
        {
            surface.texture_index = texinfo[static_cast<std::size_t>(faces[i].texinfo)].texture_index;
        }
        world.surfaces.push_back(surface);
    }
    return true;
}

bool ParseModels(const formats::LumpView &lump, ParsedWorld &world, std::string &error)
{
    world.brushes.clear();
    if (lump.size == 0)
    {
        return true;
    }
    if (lump.data == nullptr || (lump.size % sizeof(Quake1ModelDisk)) != 0)
    {
        error = "models lump misaligned";
        return false;
    }

    const auto count = lump.size / sizeof(Quake1ModelDisk);
    const auto *models = reinterpret_cast<const Quake1ModelDisk *>(lump.data);
    world.brushes.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        ParsedWorld::Brush brush;
        brush.source = ParsedWorld::Brush::Source::kBspModel;
        brush.has_bounds = true;
        brush.mins = {models[i].mins[0], models[i].mins[1], models[i].mins[2]};
        brush.maxs = {models[i].maxs[0], models[i].maxs[1], models[i].maxs[2]};
        brush.surface_indices.reserve(static_cast<std::size_t>(std::max(models[i].numfaces, 0)));
        const std::int32_t first_face = models[i].firstface;
        const std::int32_t num_faces = models[i].numfaces;
        if (first_face < 0 || num_faces < 0)
        {
            world.brushes.push_back(std::move(brush));
            continue;
        }
        for (std::int32_t face = 0; face < num_faces; ++face)
        {
            const std::size_t index = static_cast<std::size_t>(first_face + face);
            if (index >= world.surfaces.size())
            {
                error = "model references invalid face";
                return false;
            }
            brush.surface_indices.push_back(index);
        }
        world.brushes.push_back(std::move(brush));
    }
    return true;
}

bool CountEntries(const formats::LumpView &lump, std::size_t element_size, std::size_t &count, std::string &error)
{
    if (lump.size == 0)
    {
        count = 0;
        return true;
    }
    if (lump.data == nullptr || (lump.size % element_size) != 0)
    {
        error = "BSP lump misaligned";
        return false;
    }
    count = lump.size / element_size;
    return true;
}

} // namespace

bool PopulateWorldFromBspBinary(std::string_view source_name,
                                formats::ConstByteSpan data,
                                ParsedWorld &world,
                                std::string &error)
{
    formats::Quake1BspView view;
    if (!formats::DeserializeQuake1Bsp(data, view, error))
    {
        return false;
    }

    world = ParsedWorld{};
    world.format = ParsedWorld::Format::kBsp;
    world.source_name = std::string(source_name);
    world.map_info.reset();

    const auto &entities_lump = view.lumps[static_cast<std::size_t>(formats::Quake1Lump::kEntities)];
    if (entities_lump.size > 0 && entities_lump.data != nullptr)
    {
        world.entities_text.assign(reinterpret_cast<const char *>(entities_lump.data), entities_lump.size);
    }
    world.entities_text = EnsureEntitiesText(std::move(world.entities_text));
    world.lines = SplitLines(world.entities_text);

    if (!ParseEntities(world.entities_text, world.entities, error))
    {
        return false;
    }

    if (!ParseTextures(view.lumps[static_cast<std::size_t>(formats::Quake1Lump::kTextures)], world, error))
    {
        return false;
    }
    if (!ParsePlanes(view.lumps[static_cast<std::size_t>(formats::Quake1Lump::kPlanes)], world, error))
    {
        return false;
    }

    std::vector<TexInfoEntry> texinfo;
    if (!ParseTexInfo(view.lumps[static_cast<std::size_t>(formats::Quake1Lump::kTexInfo)], texinfo, world.textures.size(), error))
    {
        return false;
    }
    if (!ParseSurfaces(view.lumps[static_cast<std::size_t>(formats::Quake1Lump::kFaces)], texinfo, world, error))
    {
        return false;
    }
    if (!ParseModels(view.lumps[static_cast<std::size_t>(formats::Quake1Lump::kModels)], world, error))
    {
        return false;
    }

    if (!world.entities.empty())
    {
        auto &worldspawn = world.entities.front();
        worldspawn.brushes.clear();
        for (std::size_t i = 0; i < world.brushes.size(); ++i)
        {
            worldspawn.brushes.push_back(i);
        }
    }

    std::size_t node_count = 0;
    std::size_t leaf_count = 0;
    std::size_t model_count = world.brushes.size();
    if (!CountEntries(view.lumps[static_cast<std::size_t>(formats::Quake1Lump::kNodes)], sizeof(Quake1NodeDisk), node_count, error))
    {
        return false;
    }
    if (!CountEntries(view.lumps[static_cast<std::size_t>(formats::Quake1Lump::kLeaves)], sizeof(Quake1LeafDisk), leaf_count, error))
    {
        return false;
    }

    world.bsp_info = ParsedWorld::BspMetadata{};
    world.bsp_info->plane_count = world.planes.size();
    world.bsp_info->node_count = node_count;
    world.bsp_info->leaf_count = leaf_count;
    world.bsp_info->model_count = model_count;

    return true;
}

} // namespace bspc::builder

