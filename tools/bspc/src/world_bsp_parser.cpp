#include "world_parsers.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

#include "bsp_formats.hpp"
#include "logging.hpp"

namespace bspc::builder
{
namespace
{

constexpr float kDefaultScale = 1.0f;

struct QuakeFace
{
    std::uint16_t plane_index = 0;
    std::uint16_t side = 0;
    std::int32_t first_edge = 0;
    std::int16_t edge_count = 0;
    std::int16_t texinfo = -1;
    std::uint8_t styles[4] = {0, 0, 0, 0};
    std::int32_t light_ofs = -1;
};
static_assert(sizeof(QuakeFace) == 20, "Unexpected packing for QuakeFace");

struct QuakeTexInfo
{
    float vecs[2][4];
    std::int32_t miptex = -1;
    std::int32_t flags = 0;
};
static_assert(sizeof(QuakeTexInfo) == 40, "Unexpected packing for QuakeTexInfo");

struct QuakeNode
{
    std::int32_t plane_index = -1;
    std::int16_t children[2] = {-1, -1};
    std::int16_t mins[3] = {0, 0, 0};
    std::int16_t maxs[3] = {0, 0, 0};
    std::uint16_t first_face = 0;
    std::uint16_t face_count = 0;
};
static_assert(sizeof(QuakeNode) == 24, "Unexpected packing for QuakeNode");

struct QuakeLeaf
{
    std::int32_t contents = 0;
    std::int16_t mins[3] = {0, 0, 0};
    std::int16_t maxs[3] = {0, 0, 0};
    std::uint16_t first_mark_surface = 0;
    std::uint16_t mark_surface_count = 0;
    std::uint8_t ambient_levels[4] = {0, 0, 0, 0};
};
static_assert(sizeof(QuakeLeaf) == 24, "Unexpected packing for QuakeLeaf");

struct MipTextureHeader
{
    char name[16];
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t offsets[4] = {0, 0, 0, 0};
};
static_assert(sizeof(MipTextureHeader) == 40, "Unexpected packing for MipTextureHeader");

template <typename T>
std::vector<T> CopyStructVector(formats::LumpView view)
{
    if (view.size % sizeof(T) != 0)
    {
        return {};
    }
    const auto count = view.size / sizeof(T);
    const auto *typed = reinterpret_cast<const T *>(view.data);
    return std::vector<T>(typed, typed + count);
}

std::vector<ParsedWorld::Plane> ParsePlanes(formats::LumpView view)
{
    struct PlaneRecord
    {
        float normal[3];
        float dist;
        std::int32_t type;
    };
    static_assert(sizeof(PlaneRecord) == 20, "Unexpected packing for PlaneRecord");

    if (view.size % sizeof(PlaneRecord) != 0)
    {
        return {};
    }

    const std::size_t count = view.size / sizeof(PlaneRecord);
    const auto *records = reinterpret_cast<const PlaneRecord *>(view.data);

    std::vector<ParsedWorld::Plane> planes;
    planes.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        ParsedWorld::Plane plane;
        plane.normal[0] = records[i].normal[0];
        plane.normal[1] = records[i].normal[1];
        plane.normal[2] = records[i].normal[2];
        plane.dist = records[i].dist;
        plane.type = records[i].type;
        planes.push_back(plane);
    }
    return planes;
}

std::vector<ParsedWorld::Texture> ParseTextures(formats::LumpView view)
{
    std::vector<ParsedWorld::Texture> textures;
    if (view.size < sizeof(std::int32_t))
    {
        return textures;
    }

    const auto *data = reinterpret_cast<const std::byte *>(view.data);
    const auto *num_ptr = reinterpret_cast<const std::int32_t *>(data);
    const std::int32_t texture_count = *num_ptr;
    if (texture_count <= 0)
    {
        return textures;
    }

    const std::size_t header_size = sizeof(std::int32_t) + sizeof(std::int32_t) * static_cast<std::size_t>(texture_count);
    if (view.size < header_size)
    {
        return textures;
    }

    textures.reserve(static_cast<std::size_t>(texture_count));
    const auto *offsets = reinterpret_cast<const std::int32_t *>(data + sizeof(std::int32_t));
    for (std::int32_t i = 0; i < texture_count; ++i)
    {
        const std::int32_t offset = offsets[i];
        if (offset < 0)
        {
            textures.push_back({});
            continue;
        }

        const std::size_t start = static_cast<std::size_t>(offset);
        if (start + sizeof(MipTextureHeader) > view.size)
        {
            textures.push_back({});
            continue;
        }

        const auto *header = reinterpret_cast<const MipTextureHeader *>(data + start);
        ParsedWorld::Texture texture;
        std::string name(header->name, header->name + sizeof(header->name));
        const auto null_pos = name.find('\0');
        if (null_pos != std::string::npos)
        {
            name.resize(null_pos);
        }
        texture.name = std::move(name);
        texture.width = header->width;
        texture.height = header->height;
        textures.push_back(std::move(texture));
    }

    return textures;
}

std::vector<ParsedWorld::Surface> ParseSurfaces(formats::LumpView faces_view,
                                                const std::vector<QuakeTexInfo> &texinfo,
                                                const std::vector<ParsedWorld::Texture> &textures)
{
    std::vector<ParsedWorld::Surface> surfaces;
    if (faces_view.size % sizeof(QuakeFace) != 0)
    {
        return surfaces;
    }

    const auto count = faces_view.size / sizeof(QuakeFace);
    const auto *faces = reinterpret_cast<const QuakeFace *>(faces_view.data);
    surfaces.reserve(count);

    for (std::size_t i = 0; i < count; ++i)
    {
        const QuakeFace &face = faces[i];
        ParsedWorld::Surface surface;
        surface.plane_index = face.plane_index;
        surface.plane_side = face.side != 0;
        surface.first_edge = face.first_edge;
        surface.edge_count = face.edge_count;
        surface.texinfo = face.texinfo;
        surface.lightmap_offset = face.light_ofs;
        std::copy(std::begin(face.styles), std::end(face.styles), surface.styles.begin());

        if (face.texinfo >= 0 && static_cast<std::size_t>(face.texinfo) < texinfo.size())
        {
            const QuakeTexInfo &info = texinfo[static_cast<std::size_t>(face.texinfo)];
            const std::int32_t miptex = info.miptex;
            if (miptex >= 0 && static_cast<std::size_t>(miptex) < textures.size())
            {
                surface.texture_index = static_cast<std::size_t>(miptex);
            }
            surface.shift[0] = info.vecs[0][3];
            surface.shift[1] = info.vecs[1][3];
            surface.scale[0] = kDefaultScale;
            surface.scale[1] = kDefaultScale;
        }

        surfaces.push_back(surface);
    }

    return surfaces;
}

std::vector<ParsedWorld::Node> ParseNodes(formats::LumpView view)
{
    std::vector<ParsedWorld::Node> nodes;
    if (view.size % sizeof(QuakeNode) != 0)
    {
        return nodes;
    }
    const auto count = view.size / sizeof(QuakeNode);
    const auto *records = reinterpret_cast<const QuakeNode *>(view.data);
    nodes.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        ParsedWorld::Node node;
        node.plane_index = records[i].plane_index;
        node.children[0] = records[i].children[0];
        node.children[1] = records[i].children[1];
        node.mins = {records[i].mins[0], records[i].mins[1], records[i].mins[2]};
        node.maxs = {records[i].maxs[0], records[i].maxs[1], records[i].maxs[2]};
        node.first_face = records[i].first_face;
        node.face_count = records[i].face_count;
        nodes.push_back(node);
    }
    return nodes;
}

std::vector<ParsedWorld::Leaf> ParseLeaves(formats::LumpView view)
{
    std::vector<ParsedWorld::Leaf> leaves;
    if (view.size % sizeof(QuakeLeaf) != 0)
    {
        return leaves;
    }
    const auto count = view.size / sizeof(QuakeLeaf);
    const auto *records = reinterpret_cast<const QuakeLeaf *>(view.data);
    leaves.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        ParsedWorld::Leaf leaf;
        leaf.contents = records[i].contents;
        leaf.mins = {records[i].mins[0], records[i].mins[1], records[i].mins[2]};
        leaf.maxs = {records[i].maxs[0], records[i].maxs[1], records[i].maxs[2]};
        leaf.first_mark_surface = records[i].first_mark_surface;
        leaf.mark_surface_count = records[i].mark_surface_count;
        leaf.ambient_levels = {records[i].ambient_levels[0],
                               records[i].ambient_levels[1],
                               records[i].ambient_levels[2],
                               records[i].ambient_levels[3]};
        leaves.push_back(leaf);
    }
    return leaves;
}

} // namespace

WorldInputFormat DetectWorldFormat(const std::vector<std::byte> &data) noexcept
{
    if (data.empty())
    {
        return WorldInputFormat::kUnknown;
    }

    if (data.size() >= 4)
    {
        const std::uint32_t header = std::uint32_t(static_cast<unsigned char>(data[0])) |
                                     (std::uint32_t(static_cast<unsigned char>(data[1])) << 8) |
                                     (std::uint32_t(static_cast<unsigned char>(data[2])) << 16) |
                                     (std::uint32_t(static_cast<unsigned char>(data[3])) << 24);
        if (header == static_cast<std::uint32_t>(formats::kQuake1BspVersion) ||
            header == static_cast<std::uint32_t>(formats::kHalfLifeBspVersion))
        {
            return WorldInputFormat::kBinaryBsp;
        }
        const std::uint32_t ibsp = (std::uint32_t('I')) | (std::uint32_t('B') << 8) |
                                   (std::uint32_t('S') << 16) | (std::uint32_t('P') << 24);
        const std::uint32_t vbsp = (std::uint32_t('V')) | (std::uint32_t('B') << 8) |
                                   (std::uint32_t('S') << 16) | (std::uint32_t('P') << 24);
        if (header == ibsp)
        {
            return WorldInputFormat::kBinaryBsp;
        }
        if (header == vbsp)
        {
            return WorldInputFormat::kBinaryBsp;
        }
    }

    const std::size_t scan_limit = std::min<std::size_t>(data.size(), 32);
    for (std::size_t i = 0; i < scan_limit; ++i)
    {
        const unsigned char value = static_cast<unsigned char>(data[i]);
        if (value == 0)
        {
            return WorldInputFormat::kBinaryBsp;
        }
        if (value < 0x09)
        {
            return WorldInputFormat::kBinaryBsp;
        }
    }

    return WorldInputFormat::kMapText;
}

bool PopulateWorldFromBsp(const std::vector<std::byte> &data, ParsedWorld &world, std::string &error)
{
    world.planes.clear();
    world.surfaces.clear();
    world.brushes.clear();
    world.textures.clear();
    world.entities.clear();
    world.nodes.clear();
    world.leaves.clear();

    const formats::ConstByteSpan span{data.data(), data.size()};
    formats::Quake1BspView quake_view;
    if (formats::DeserializeQuake1Bsp(span, quake_view, error))
    {
        world.format = ParsedWorld::SourceFormat::kQuake1Bsp;
        world.bsp_version = quake_view.header.version;
        if (!quake_view.lumps[static_cast<std::size_t>(formats::Quake1Lump::kEntities)].empty())
        {
            const auto &entities = quake_view.lumps[static_cast<std::size_t>(formats::Quake1Lump::kEntities)];
            world.entities_text.assign(reinterpret_cast<const char *>(entities.data), entities.size);
        }

        world.planes = ParsePlanes(quake_view.lumps[static_cast<std::size_t>(formats::Quake1Lump::kPlanes)]);
        world.textures = ParseTextures(quake_view.lumps[static_cast<std::size_t>(formats::Quake1Lump::kTextures)]);

        const auto texinfo_raw = CopyStructVector<QuakeTexInfo>(
            quake_view.lumps[static_cast<std::size_t>(formats::Quake1Lump::kTexInfo)]);
        world.surfaces = ParseSurfaces(
            quake_view.lumps[static_cast<std::size_t>(formats::Quake1Lump::kFaces)],
            texinfo_raw,
            world.textures);
        world.nodes = ParseNodes(quake_view.lumps[static_cast<std::size_t>(formats::Quake1Lump::kNodes)]);
        world.leaves = ParseLeaves(quake_view.lumps[static_cast<std::size_t>(formats::Quake1Lump::kLeaves)]);
        error.clear();
        return true;
    }

    const std::string quake_error = error;
    formats::HalfLifeBspView hl_view;
    if (formats::DeserializeHalfLifeBsp(span, hl_view, error))
    {
        world.format = ParsedWorld::SourceFormat::kHalfLifeBsp;
        world.bsp_version = hl_view.header.version;
        if (!hl_view.lumps[static_cast<std::size_t>(formats::HalfLifeLump::kEntities)].empty())
        {
            const auto &entities = hl_view.lumps[static_cast<std::size_t>(formats::HalfLifeLump::kEntities)];
            world.entities_text.assign(reinterpret_cast<const char *>(entities.data), entities.size);
        }

        world.planes = ParsePlanes(hl_view.lumps[static_cast<std::size_t>(formats::HalfLifeLump::kPlanes)]);
        world.textures = ParseTextures(hl_view.lumps[static_cast<std::size_t>(formats::HalfLifeLump::kTextures)]);

        const auto texinfo_raw = CopyStructVector<QuakeTexInfo>(
            hl_view.lumps[static_cast<std::size_t>(formats::HalfLifeLump::kTexInfo)]);
        world.surfaces = ParseSurfaces(
            hl_view.lumps[static_cast<std::size_t>(formats::HalfLifeLump::kFaces)],
            texinfo_raw,
            world.textures);
        world.nodes = ParseNodes(hl_view.lumps[static_cast<std::size_t>(formats::HalfLifeLump::kNodes)]);
        world.leaves = ParseLeaves(hl_view.lumps[static_cast<std::size_t>(formats::HalfLifeLump::kLeaves)]);
        error.clear();
        return true;
    }

    error = "Unable to deserialize BSP as Quake 1 (" + quake_error + ") or Half-Life (" + error + ")";
    return false;
}

} // namespace bspc::builder

