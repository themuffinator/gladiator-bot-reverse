#include <array>
#include <cassert>
#include <iostream>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "bsp_builder.hpp"
#include "bsp_formats.hpp"
#include "filesystem_helper.h"

#ifndef PROJECT_SOURCE_DIR
#error "PROJECT_SOURCE_DIR must be defined so regression tests can resolve asset paths."
#endif

namespace
{

std::filesystem::path AssetDir()
{
    return std::filesystem::path(PROJECT_SOURCE_DIR) / "tests/support/assets/bspc";
}

std::filesystem::path TempDir()
{
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "bspc_world_loader_tests";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    return dir;
}

void TestLoadMap()
{
    bspc::InputFile input;
    input.path = AssetDir() / "simple_room.map";
    input.original = input.path.generic_string();

    bspc::builder::ParsedWorld world;
    std::string error;
    const bool loaded = bspc::builder::LoadWorldState(input, world, error);
    if (!loaded)
    {
        std::cerr << "LoadWorldState failed: " << error << std::endl;
    }
    assert(loaded);
    assert(world.format == bspc::builder::ParsedWorld::Format::kMap);
    assert(world.entities.size() == 1);
    assert(world.brushes.size() == 1);
    assert(world.planes.size() == 6);
    assert(world.textures.size() == 1);
    assert(world.map_info.has_value());
    assert(world.map_info->entity_count == 1);
    assert(world.map_info->brush_count == 1);
    assert(world.map_info->plane_count == 6);

    const bspc::builder::ParsedWorld::Entity &worldspawn = world.entities.front();
    auto classname = worldspawn.FindProperty("classname");
    assert(classname.has_value());
    assert(*classname == "worldspawn");
    assert(worldspawn.brushes.size() == 1);

    const bspc::builder::ParsedWorld::Brush &brush = world.brushes.front();
    assert(brush.source == bspc::builder::ParsedWorld::Brush::Source::kMapBrush);
    assert(brush.sides.size() == 6);
    assert(brush.patch.has_value() == false);
    assert(world.textures.front().name == "textures/common/caulk");
}

struct Quake1PlaneDisk
{
    float normal[3];
    float dist;
    std::int32_t type;
};

struct Quake1TexInfoDisk
{
    float vecs[2][4];
    std::int32_t miptex;
    std::int32_t flags;
};

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

std::vector<std::byte> BuildTexturesLump()
{
    std::vector<std::byte> lump;
    lump.resize(sizeof(MiptexHeader) + sizeof(std::int32_t) + sizeof(MiptexDisk));
    auto *header = reinterpret_cast<MiptexHeader *>(lump.data());
    header->count = 1;
    auto *offsets = reinterpret_cast<std::int32_t *>(lump.data() + sizeof(MiptexHeader));
    offsets[0] = static_cast<std::int32_t>(sizeof(MiptexHeader) + sizeof(std::int32_t));
    auto *miptex = reinterpret_cast<MiptexDisk *>(lump.data() + offsets[0]);
    std::memset(miptex, 0, sizeof(MiptexDisk));
    std::memcpy(miptex->name, "STONE", 5);
    miptex->width = 64;
    miptex->height = 64;
    return lump;
}

std::vector<std::byte> BuildPlanesLump()
{
    std::vector<std::byte> lump(sizeof(Quake1PlaneDisk));
    auto *plane = reinterpret_cast<Quake1PlaneDisk *>(lump.data());
    plane->normal[0] = 0.0f;
    plane->normal[1] = 0.0f;
    plane->normal[2] = 1.0f;
    plane->dist = 0.0f;
    plane->type = 0;
    return lump;
}

std::vector<std::byte> BuildTexInfoLump()
{
    std::vector<std::byte> lump(sizeof(Quake1TexInfoDisk));
    auto *info = reinterpret_cast<Quake1TexInfoDisk *>(lump.data());
    std::memset(info, 0, sizeof(Quake1TexInfoDisk));
    info->miptex = 0;
    info->flags = 0;
    return lump;
}

std::vector<std::byte> BuildFacesLump()
{
    std::vector<std::byte> lump(sizeof(Quake1FaceDisk));
    auto *face = reinterpret_cast<Quake1FaceDisk *>(lump.data());
    face->planenum = 0;
    face->side = 0;
    face->firstedge = 0;
    face->numedges = 3;
    face->texinfo = 0;
    std::memset(face->styles, 0, sizeof(face->styles));
    face->lightofs = -1;
    return lump;
}

std::vector<std::byte> BuildModelsLump()
{
    std::vector<std::byte> lump(sizeof(Quake1ModelDisk));
    auto *model = reinterpret_cast<Quake1ModelDisk *>(lump.data());
    std::memset(model, 0, sizeof(Quake1ModelDisk));
    model->mins[0] = -16.0f;
    model->mins[1] = -16.0f;
    model->mins[2] = 0.0f;
    model->maxs[0] = 16.0f;
    model->maxs[1] = 16.0f;
    model->maxs[2] = 32.0f;
    model->firstface = 0;
    model->numfaces = 1;
    return lump;
}

void TestLoadBsp()
{
    const std::filesystem::path temp_dir = TempDir();
    const std::filesystem::path bsp_path = temp_dir / "generated.bsp";

    std::array<std::vector<std::byte>, bspc::formats::kQuake1LumpCount> storage;
    std::array<bspc::formats::LumpView, bspc::formats::kQuake1LumpCount> lumps{};

    const std::string entities = "{\n\"classname\" \"worldspawn\"\n\"message\" \"Generated\"\n}\n{\n\"classname\" \"info_player_start\"\n}\n";
    auto set_lump = [&](bspc::formats::Quake1Lump lump_id, std::vector<std::byte> data) {
        const std::size_t index = static_cast<std::size_t>(lump_id);
        storage[index] = std::move(data);
        lumps[index].data = storage[index].data();
        lumps[index].size = storage[index].size();
    };

    std::vector<std::byte> entity_bytes(entities.size());
    if (!entity_bytes.empty())
    {
        std::memcpy(entity_bytes.data(), entities.data(), entities.size());
    }
    set_lump(bspc::formats::Quake1Lump::kEntities, std::move(entity_bytes));
    set_lump(bspc::formats::Quake1Lump::kTextures, BuildTexturesLump());
    set_lump(bspc::formats::Quake1Lump::kPlanes, BuildPlanesLump());
    set_lump(bspc::formats::Quake1Lump::kTexInfo, BuildTexInfoLump());
    set_lump(bspc::formats::Quake1Lump::kFaces, BuildFacesLump());
    set_lump(bspc::formats::Quake1Lump::kModels, BuildModelsLump());

    std::vector<std::byte> bsp_data;
    std::string error;
    const bool serialized = bspc::formats::SerializeQuake1Bsp(lumps, bsp_data, error);
    assert(serialized);
    assert(error.empty());

    std::ofstream stream(bsp_path, std::ios::binary);
    assert(stream);
    stream.write(reinterpret_cast<const char *>(bsp_data.data()), static_cast<std::streamsize>(bsp_data.size()));
    stream.close();

    bspc::InputFile input;
    input.path = bsp_path;
    input.original = input.path.generic_string();

    bspc::builder::ParsedWorld world;
    const bool loaded = bspc::builder::LoadWorldState(input, world, error);
    if (!loaded)
    {
        std::cerr << "LoadWorldState failed: " << error << std::endl;
    }
    assert(loaded);
    assert(world.format == bspc::builder::ParsedWorld::Format::kBsp);
    assert(world.entities.size() >= 2);
    assert(world.brushes.size() == 1);
    assert(world.surfaces.size() == 1);
    assert(world.planes.size() == 1);
    assert(world.textures.size() == 1);
    assert(world.bsp_info.has_value());
    assert(world.bsp_info->plane_count == 1);
    assert(world.bsp_info->model_count == 1);

    const bspc::builder::ParsedWorld::Entity &worldspawn = world.entities.front();
    auto classname = worldspawn.FindProperty("classname");
    assert(classname.has_value());
    assert(*classname == "worldspawn");
    auto message = worldspawn.FindProperty("message");
    assert(message.has_value());
    assert(*message == "Generated");
    assert(worldspawn.brushes.size() == 1);
    assert(world.brushes.front().surface_indices.size() == 1);
    assert(world.textures.front().name == "STONE");
}

void TestQuake2Serialization()
{
    std::array<std::vector<std::byte>, bspc::formats::kQuake2LumpCount> storage;
    std::array<bspc::formats::LumpView, bspc::formats::kQuake2LumpCount> lumps{};

    auto set_lump = [&](bspc::formats::Quake2Lump lump_id, std::vector<std::byte> data) {
        const std::size_t index = static_cast<std::size_t>(lump_id);
        storage[index] = std::move(data);
        lumps[index].data = storage[index].data();
        lumps[index].size = storage[index].size();
    };

    const std::string entities = "{\n\"classname\" \"worldspawn\"\n}\n";
    std::vector<std::byte> entity_bytes(entities.size());
    if (!entity_bytes.empty())
    {
        std::memcpy(entity_bytes.data(), entities.data(), entities.size());
    }
    set_lump(bspc::formats::Quake2Lump::kEntities, std::move(entity_bytes));

    bspc::formats::Quake2Plane plane{};
    plane.normal[2] = 1.0f;
    std::vector<std::byte> plane_bytes(sizeof(plane));
    std::memcpy(plane_bytes.data(), &plane, sizeof(plane));
    set_lump(bspc::formats::Quake2Lump::kPlanes, std::move(plane_bytes));

    bspc::formats::Quake2Node node{};
    node.planenum = 0;
    node.children[0] = -1;
    node.children[1] = -1;
    std::vector<std::byte> node_bytes(sizeof(node));
    std::memcpy(node_bytes.data(), &node, sizeof(node));
    set_lump(bspc::formats::Quake2Lump::kNodes, std::move(node_bytes));

    bspc::formats::Quake2Leaf leaf{};
    leaf.contents = 1;
    leaf.cluster = -1;
    leaf.area = -1;
    std::vector<std::byte> leaf_bytes(sizeof(leaf));
    std::memcpy(leaf_bytes.data(), &leaf, sizeof(leaf));
    set_lump(bspc::formats::Quake2Lump::kLeaves, std::move(leaf_bytes));

    bspc::formats::Quake2Model model{};
    model.headnode = 0;
    std::vector<std::byte> model_bytes(sizeof(model));
    std::memcpy(model_bytes.data(), &model, sizeof(model));
    set_lump(bspc::formats::Quake2Lump::kModels, std::move(model_bytes));

    std::vector<std::byte> bsp_data;
    std::string error;
    const bool serialized = bspc::formats::SerializeQuake2Bsp(lumps, bsp_data, error);
    assert(serialized);
    assert(error.empty());

    bspc::formats::ConstByteSpan span;
    span.data = reinterpret_cast<const std::byte *>(bsp_data.data());
    span.size = bsp_data.size();

    bspc::formats::Quake2BspView view{};
    const bool parsed = bspc::formats::DeserializeQuake2Bsp(span, view, error);
    assert(parsed);
    assert(error.empty());
    assert(view.header.ident == bspc::formats::kQuake2BspIdent);
    assert(view.header.version == bspc::formats::kQuake2BspVersion);

    std::vector<std::byte> round_trip;
    const bool reserialized = bspc::formats::SerializeQuake2Bsp(view.lumps, round_trip, error);
    assert(reserialized);
    assert(error.empty());
    assert(round_trip == bsp_data);
}

} // namespace

int main()
{
    TestLoadMap();
    TestLoadBsp();
    TestQuake2Serialization();
    return 0;
}

