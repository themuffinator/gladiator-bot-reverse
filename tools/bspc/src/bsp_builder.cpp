#include "bsp_builder.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>
#include <numeric>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <limits>

#include "logging.hpp"

#include "brush_bsp.hpp"
#include "legacy_common.hpp"
#include "leakfile.hpp"
#include "portals.hpp"
#include "prtfile.hpp"
#include "tree.hpp"

namespace bspc::builder
{
namespace
{

std::size_t CountLeakPoints(std::string_view leak_text) noexcept
{
    if (leak_text.empty())
    {
        return 0;
    }

    return report;
}

struct Quake1PlaneDisk
{
    float normal[3] = {0.0f, 0.0f, 0.0f};
    float distance = 0.0f;
    std::int32_t type = 0;
};

struct Quake1NodeDisk
{
    std::int32_t planenum = 0;
    std::int16_t children[2] = {0, 0};
    std::int16_t mins[3] = {0, 0, 0};
    std::int16_t maxs[3] = {0, 0, 0};
    std::uint16_t first_face = 0;
    std::uint16_t face_count = 0;
};

struct Quake1LeafDisk
{
    std::int32_t contents = 0;
    std::int16_t mins[3] = {0, 0, 0};
    std::int16_t maxs[3] = {0, 0, 0};
    std::uint16_t first_mark_surface = 0;
    std::uint16_t mark_surface_count = 0;
    std::uint16_t visibility = 0;
    std::uint8_t ambient_level[4] = {0, 0, 0, 0};
};

struct Quake1ModelDisk
{
    float mins[3] = {0.0f, 0.0f, 0.0f};
    float maxs[3] = {0.0f, 0.0f, 0.0f};
    float origin[3] = {0.0f, 0.0f, 0.0f};
    std::int32_t headnode[4] = {-1, -1, -1, -1};
    std::int32_t first_face = 0;
    std::int32_t face_count = 0;
};

template <typename T>
void WriteVectorToLump(const std::vector<T> &source, formats::OwnedLump &lump)
{
    if (source.empty())
    {
        lump.Reset();
        return;
    }

    const std::size_t size_bytes = source.size() * sizeof(T);
    lump.Allocate(size_bytes, false);
    std::memcpy(lump.data.get(), source.data(), size_bytes);
}

void WriteTextLump(std::string_view text, formats::OwnedLump &lump)
{
    if (text.empty())
    {
        lump.Reset();
        return;
    }

    lump.Allocate(text.size(), false);
    std::memcpy(lump.data.get(), text.data(), text.size());
}

std::int16_t ClampToShort(double value) noexcept
{
    constexpr double kMinShort = static_cast<double>(std::numeric_limits<std::int16_t>::min());
    constexpr double kMaxShort = static_cast<double>(std::numeric_limits<std::int16_t>::max());
    const double clamped = std::clamp(value, kMinShort, kMaxShort);
    return static_cast<std::int16_t>(std::lround(clamped));
}

struct TreeCollection
{
    std::vector<const legacy::Node *> nodes;
    std::vector<const legacy::Node *> leaves;
    std::unordered_map<const legacy::Node *, std::size_t> node_indices;
    std::unordered_map<const legacy::Node *, std::size_t> leaf_indices;
};

void RegisterLeaf(const legacy::Node &node, TreeCollection &collection)
{
    const auto [it, inserted] = collection.leaf_indices.emplace(&node, collection.leaves.size());
    if (inserted)
    {
        collection.leaves.push_back(&node);
    }
}

void CollectTreeRecursive(const legacy::Node &node, TreeCollection &collection)
{
    if (node.planenum == legacy::kPlanenumLeaf)
    {
        RegisterLeaf(node, collection);
        return;
    }

    const auto [it, inserted] = collection.node_indices.emplace(&node, collection.nodes.size());
    if (inserted)
    {
        collection.nodes.push_back(&node);
    }

    for (const legacy::Node *child : node.children)
    {
        if (child)
        {
            CollectTreeRecursive(*child, collection);
        }
    }
}

TreeCollection CollectTree(const legacy::Tree &tree)
{
    TreeCollection collection;
    RegisterLeaf(tree.outside_node, collection);

    if (tree.headnode)
    {
        CollectTreeRecursive(*tree.headnode, collection);
    }

    return collection;
}

std::int16_t ClampIntToShort(int value) noexcept
{
    const int min_value = static_cast<int>(std::numeric_limits<std::int16_t>::min());
    const int max_value = static_cast<int>(std::numeric_limits<std::int16_t>::max());
    const int clamped = std::clamp(value, min_value, max_value);
    return static_cast<std::int16_t>(clamped);
}

struct TreeEmission
{
    struct NodeInfo
    {
        std::int32_t planenum = 0;
        std::int32_t children[2] = {-1, -1};
        std::int16_t mins[3] = {0, 0, 0};
        std::int16_t maxs[3] = {0, 0, 0};
        std::uint16_t first_face = 0;
        std::uint16_t face_count = 0;
    };

    struct LeafInfo
    {
        std::int32_t contents = 0;
        std::int16_t cluster = -1;
        std::int16_t area = -1;
        std::int16_t mins[3] = {0, 0, 0};
        std::int16_t maxs[3] = {0, 0, 0};
    };

    std::vector<NodeInfo> nodes;
    std::vector<LeafInfo> leaves;
};

TreeEmission EmitTreeLumps(const TreeCollection &collection)
{
    TreeEmission emission;
    emission.nodes.resize(collection.nodes.size());
    emission.leaves.resize(collection.leaves.size());

    for (std::size_t index = 0; index < collection.nodes.size(); ++index)
    {
        const legacy::Node *node = collection.nodes[index];
        TreeEmission::NodeInfo info;
        info.planenum = std::max(0, node->planenum);
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            info.mins[axis] = ClampToShort(node->mins[axis]);
            info.maxs[axis] = ClampToShort(node->maxs[axis]);
        }

        for (std::size_t child_index = 0; child_index < 2; ++child_index)
        {
            const legacy::Node *child = node->children[child_index];
            if (!child)
            {
                info.children[child_index] = -1;
                continue;
            }

            const auto node_it = collection.node_indices.find(child);
            if (node_it != collection.node_indices.end())
            {
                info.children[child_index] = static_cast<std::int32_t>(node_it->second);
                continue;
            }

            const auto leaf_it = collection.leaf_indices.find(child);
            if (leaf_it != collection.leaf_indices.end())
            {
                const auto leaf_index = static_cast<std::int32_t>(leaf_it->second);
                info.children[child_index] = -leaf_index - 1;
            }
            else
            {
                info.children[child_index] = -1;
            }
        }

        emission.nodes[index] = info;
    }

    for (std::size_t index = 0; index < collection.leaves.size(); ++index)
    {
        const legacy::Node *leaf = collection.leaves[index];
        TreeEmission::LeafInfo info;
        if (leaf)
        {
            info.contents = leaf->contents;
            info.cluster = ClampIntToShort(leaf->cluster);
            info.area = ClampIntToShort(leaf->area);
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                info.mins[axis] = ClampToShort(leaf->mins[axis]);
                info.maxs[axis] = ClampToShort(leaf->maxs[axis]);
            }
        }
        emission.leaves[index] = info;
    }

    return emission;
}

void ValidateCount(std::size_t actual, std::size_t expected, std::string_view label)
{
#ifndef NDEBUG
    assert(actual == expected && "BSP lump count mismatch");
#else
    if (actual != expected)
    {
        log::Warning("%.*s count mismatch: expected %zu, got %zu\n",
                     static_cast<int>(label.size()),
                     label.data(),
                     expected,
                     actual);
    }
#endif
}

void WriteQuake1BspLumps(const ParsedWorld &world,
                         const legacy::Tree &tree,
                         BspBuildArtifacts &artifacts)
{
    auto &entities_lump = artifacts.quake1_lumps[static_cast<std::size_t>(formats::Quake1Lump::kEntities)];
    WriteTextLump(world.entities_text, entities_lump);

    const auto &planes = legacy::MapPlanes();
    std::vector<Quake1PlaneDisk> plane_data;
    plane_data.reserve(planes.size());
    for (const legacy::Plane &plane : planes)
    {
        Quake1PlaneDisk disk;
        disk.normal[0] = static_cast<float>(plane.normal.x);
        disk.normal[1] = static_cast<float>(plane.normal.y);
        disk.normal[2] = static_cast<float>(plane.normal.z);
        disk.distance = static_cast<float>(plane.dist);
        disk.type = plane.type;
        plane_data.push_back(disk);
    }
    auto &planes_lump = artifacts.quake1_lumps[static_cast<std::size_t>(formats::Quake1Lump::kPlanes)];
    WriteVectorToLump(plane_data, planes_lump);

    artifacts.quake1_lumps[static_cast<std::size_t>(formats::Quake1Lump::kTextures)].Reset();
    artifacts.quake1_lumps[static_cast<std::size_t>(formats::Quake1Lump::kVertices)].Reset();
    artifacts.quake1_lumps[static_cast<std::size_t>(formats::Quake1Lump::kVisibility)].Reset();

    const TreeCollection collection = CollectTree(tree);
    const TreeEmission emission = EmitTreeLumps(collection);

    std::vector<Quake1NodeDisk> node_data;
    node_data.reserve(emission.nodes.size());
    for (const auto &info : emission.nodes)
    {
        Quake1NodeDisk disk{};
        disk.planenum = info.planenum;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            disk.mins[axis] = info.mins[axis];
            disk.maxs[axis] = info.maxs[axis];
        }
        for (std::size_t child_index = 0; child_index < 2; ++child_index)
        {
            const std::int32_t child = info.children[child_index];
            assert(child >= std::numeric_limits<std::int16_t>::min() &&
                   child <= std::numeric_limits<std::int16_t>::max());
            disk.children[child_index] = static_cast<std::int16_t>(child);
        }
        disk.first_face = info.first_face;
        disk.face_count = info.face_count;
        node_data.push_back(disk);
    }
    auto &nodes_lump = artifacts.quake1_lumps[static_cast<std::size_t>(formats::Quake1Lump::kNodes)];
    WriteVectorToLump(node_data, nodes_lump);

    artifacts.quake1_lumps[static_cast<std::size_t>(formats::Quake1Lump::kTexInfo)].Reset();
    artifacts.quake1_lumps[static_cast<std::size_t>(formats::Quake1Lump::kFaces)].Reset();
    artifacts.quake1_lumps[static_cast<std::size_t>(formats::Quake1Lump::kLighting)].Reset();
    artifacts.quake1_lumps[static_cast<std::size_t>(formats::Quake1Lump::kClipNodes)].Reset();

    std::vector<Quake1LeafDisk> leaf_data;
    leaf_data.reserve(emission.leaves.size());
    for (const auto &info : emission.leaves)
    {
        Quake1LeafDisk disk{};
        disk.contents = info.contents;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            disk.mins[axis] = info.mins[axis];
            disk.maxs[axis] = info.maxs[axis];
        }
        disk.visibility = 0xFFFFu;
        leaf_data.push_back(disk);
    }
    auto &leaves_lump = artifacts.quake1_lumps[static_cast<std::size_t>(formats::Quake1Lump::kLeaves)];
    WriteVectorToLump(leaf_data, leaves_lump);

    artifacts.quake1_lumps[static_cast<std::size_t>(formats::Quake1Lump::kMarkSurfaces)].Reset();
    artifacts.quake1_lumps[static_cast<std::size_t>(formats::Quake1Lump::kEdges)].Reset();
    artifacts.quake1_lumps[static_cast<std::size_t>(formats::Quake1Lump::kSurfEdges)].Reset();

    std::vector<Quake1ModelDisk> models;
    if (tree.headnode)
    {
        Quake1ModelDisk model;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            model.mins[axis] = static_cast<float>(tree.headnode->mins[axis]);
            model.maxs[axis] = static_cast<float>(tree.headnode->maxs[axis]);
        }
        const auto node_it = collection.node_indices.find(tree.headnode.get());
        if (node_it != collection.node_indices.end())
        {
            model.headnode[0] = static_cast<std::int32_t>(node_it->second);
        }
        else
        {
            const auto leaf_it = collection.leaf_indices.find(tree.headnode.get());
            if (leaf_it != collection.leaf_indices.end())
            {
                model.headnode[0] = -static_cast<std::int32_t>(leaf_it->second) - 1;
            }
        }
        models.push_back(model);
    }

    auto &models_lump = artifacts.quake1_lumps[static_cast<std::size_t>(formats::Quake1Lump::kModels)];
    WriteVectorToLump(models, models_lump);

    ValidateCount(plane_data.size(), planes.size(), "planes");
    ValidateCount(node_data.size(), collection.nodes.size(), "nodes");
    ValidateCount(leaf_data.size(), collection.leaves.size(), "leaves");
    ValidateCount(models.size(), tree.headnode ? std::size_t{1} : std::size_t{0}, "models");
}

void WriteQuake2BspLumps(const ParsedWorld &world,
                         const legacy::Tree &tree,
                         BspBuildArtifacts &artifacts)
{
    auto &entities_lump = artifacts.quake2_lumps[static_cast<std::size_t>(formats::Quake2Lump::kEntities)];
    WriteTextLump(world.entities_text, entities_lump);

    const auto &planes = legacy::MapPlanes();
    std::vector<formats::Quake2Plane> plane_data;
    plane_data.reserve(planes.size());
    for (const legacy::Plane &plane : planes)
    {
        formats::Quake2Plane disk{};
        disk.normal[0] = static_cast<float>(plane.normal.x);
        disk.normal[1] = static_cast<float>(plane.normal.y);
        disk.normal[2] = static_cast<float>(plane.normal.z);
        disk.dist = static_cast<float>(plane.dist);
        disk.type = plane.type;
        plane_data.push_back(disk);
    }
    auto &planes_lump = artifacts.quake2_lumps[static_cast<std::size_t>(formats::Quake2Lump::kPlanes)];
    WriteVectorToLump(plane_data, planes_lump);

    artifacts.quake2_lumps[static_cast<std::size_t>(formats::Quake2Lump::kVertices)].Reset();
    artifacts.quake2_lumps[static_cast<std::size_t>(formats::Quake2Lump::kVisibility)].Reset();

    const TreeCollection collection = CollectTree(tree);
    const TreeEmission emission = EmitTreeLumps(collection);

    std::vector<formats::Quake2Node> node_data;
    node_data.reserve(emission.nodes.size());
    for (const auto &info : emission.nodes)
    {
        formats::Quake2Node node{};
        node.planenum = info.planenum;
        for (std::size_t child_index = 0; child_index < 2; ++child_index)
        {
            node.children[child_index] = info.children[child_index];
        }
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            node.mins[axis] = info.mins[axis];
            node.maxs[axis] = info.maxs[axis];
        }
        node.first_face = info.first_face;
        node.face_count = info.face_count;
        node_data.push_back(node);
    }
    auto &nodes_lump = artifacts.quake2_lumps[static_cast<std::size_t>(formats::Quake2Lump::kNodes)];
    WriteVectorToLump(node_data, nodes_lump);

    std::vector<formats::Quake2Leaf> leaf_data;
    leaf_data.reserve(emission.leaves.size());
    for (const auto &info : emission.leaves)
    {
        formats::Quake2Leaf leaf{};
        leaf.contents = info.contents;
        leaf.cluster = info.cluster;
        leaf.area = info.area;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            leaf.mins[axis] = info.mins[axis];
            leaf.maxs[axis] = info.maxs[axis];
        }
        leaf_data.push_back(leaf);
    }
    auto &leaves_lump = artifacts.quake2_lumps[static_cast<std::size_t>(formats::Quake2Lump::kLeaves)];
    WriteVectorToLump(leaf_data, leaves_lump);

    auto &texinfo_lump = artifacts.quake2_lumps[static_cast<std::size_t>(formats::Quake2Lump::kTexInfo)];
    texinfo_lump.Reset();

    std::vector<formats::Quake2Face> face_data;
    face_data.reserve(world.surfaces.size());
    for (const auto &surface : world.surfaces)
    {
        if (surface.plane_index >= plane_data.size())
        {
            continue;
        }
        formats::Quake2Face face{};
        face.plane_index = static_cast<std::uint16_t>(surface.plane_index);
        face.plane_side = surface.plane_side ? 1 : 0;
        face.first_edge = surface.first_edge;
        face.edge_count = static_cast<std::int16_t>(surface.edge_count);
        face.texinfo = surface.texinfo;
        for (std::size_t style = 0; style < surface.styles.size(); ++style)
        {
            face.styles[style] = surface.styles[style];
        }
        face.lightmap_offset = surface.lightmap_offset;
        face_data.push_back(face);
    }
    auto &faces_lump = artifacts.quake2_lumps[static_cast<std::size_t>(formats::Quake2Lump::kFaces)];
    WriteVectorToLump(face_data, faces_lump);

    artifacts.quake2_lumps[static_cast<std::size_t>(formats::Quake2Lump::kLighting)].Reset();
    artifacts.quake2_lumps[static_cast<std::size_t>(formats::Quake2Lump::kLeafFaces)].Reset();
    artifacts.quake2_lumps[static_cast<std::size_t>(formats::Quake2Lump::kLeafBrushes)].Reset();
    artifacts.quake2_lumps[static_cast<std::size_t>(formats::Quake2Lump::kEdges)].Reset();
    artifacts.quake2_lumps[static_cast<std::size_t>(formats::Quake2Lump::kSurfEdges)].Reset();

    std::vector<formats::Quake2Model> models;
    if (tree.headnode)
    {
        formats::Quake2Model model{};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            model.mins[axis] = static_cast<float>(tree.headnode->mins[axis]);
            model.maxs[axis] = static_cast<float>(tree.headnode->maxs[axis]);
        }
        const auto node_it = collection.node_indices.find(tree.headnode.get());
        if (node_it != collection.node_indices.end())
        {
            model.headnode = static_cast<std::int32_t>(node_it->second);
        }
        else
        {
            const auto leaf_it = collection.leaf_indices.find(tree.headnode.get());
            if (leaf_it != collection.leaf_indices.end())
            {
                model.headnode = -static_cast<std::int32_t>(leaf_it->second) - 1;
            }
        }
        models.push_back(model);
    }
    auto &models_lump = artifacts.quake2_lumps[static_cast<std::size_t>(formats::Quake2Lump::kModels)];
    WriteVectorToLump(models, models_lump);

    artifacts.quake2_lumps[static_cast<std::size_t>(formats::Quake2Lump::kBrushes)].Reset();
    artifacts.quake2_lumps[static_cast<std::size_t>(formats::Quake2Lump::kBrushSides)].Reset();
    artifacts.quake2_lumps[static_cast<std::size_t>(formats::Quake2Lump::kPop)].Reset();
    artifacts.quake2_lumps[static_cast<std::size_t>(formats::Quake2Lump::kAreas)].Reset();
    artifacts.quake2_lumps[static_cast<std::size_t>(formats::Quake2Lump::kAreaPortals)].Reset();

    ValidateCount(plane_data.size(), planes.size(), "planes");
    ValidateCount(node_data.size(), collection.nodes.size(), "nodes");
    ValidateCount(leaf_data.size(), collection.leaves.size(), "leaves");
    ValidateCount(models.size(), tree.headnode ? std::size_t{1} : std::size_t{0}, "models");
}

void BspBuildArtifacts::Reset() noexcept
{
    for (auto &lump : quake1_lumps)
    {
        lump.Reset();
    }
    for (auto &lump : quake2_lumps)
    {
        lump.Reset();
    }
    portal_text.clear();
    leak_text.clear();
    portal_cluster_count = 0;
    portal_count = 0;
    leak_point_count = 0;
}

} // namespace

bool BuildBspTree(const ParsedWorld &world, BspBuildArtifacts &out_artifacts)
{
    out_artifacts.Reset();

    legacy::ResetBrushBSP();
    legacy::ClearMapPlanes();

    log::Info("--- BSP tree ---\n");
    log::Info("processing %zu world lines\n", world.lines.size());

    std::unique_ptr<legacy::Tree> tree = legacy::Tree_Alloc();

    if (!world.lines.empty())
    {
        legacy::Plane plane;
        plane.normal = {0.0, 0.0, 1.0};
        plane.dist = 0.0;
        legacy::AppendPlane(plane);

        legacy::Portal *portal = legacy::AllocPortal();
        auto winding = std::make_shared<legacy::Winding>();
        winding->points.push_back({0.0, 0.0, 0.0});
        winding->points.push_back({64.0, 0.0, 0.0});
        winding->points.push_back({0.0, 64.0, 0.0});
        portal->winding = std::move(winding);
        portal->plane = plane;
        portal->onnode = tree->headnode.get();

        legacy::Node &leaf = *tree->headnode;
        leaf.planenum = legacy::kPlanenumLeaf;
        leaf.occupied = 1;
        legacy::Entity &occupant = legacy::Tree_EmplaceEntity(*tree, legacy::Vec3{0.0, 0.0, 0.0});
        leaf.occupant = &occupant;

        legacy::AddPortalToNodes(*portal, leaf, tree->outside_node);
        tree->outside_node.occupied = 2;
    }

    const auto &portal_stats = legacy::GetPortalStats();
    log::Info("portal allocator peak: %d active (%d bytes)\n",
              portal_stats.peak,
              portal_stats.memory);

    WriteQuake1BspLumps(world, *tree, out_artifacts);
    WriteQuake2BspLumps(world, *tree, out_artifacts);

    log::Info("BSP tree populated\n");

    legacy::Tree_Free(*tree);

    return true;
}

std::array<formats::LumpView, formats::kQuake1LumpCount> MakeQuake1LumpViews(const BspBuildArtifacts &artifacts) noexcept
{
    std::array<formats::LumpView, formats::kQuake1LumpCount> views{};
    for (std::size_t i = 0; i < artifacts.quake1_lumps.size(); ++i)
    {
        const auto &lump = artifacts.quake1_lumps[i];
        if (lump.size == 0)
        {
            views[i] = {};
            continue;
        }

        views[i].data = static_cast<const std::byte *>(lump.data.get());
        views[i].size = lump.size;
    }
    return views;
}

std::array<formats::LumpView, formats::kQuake2LumpCount> MakeQuake2LumpViews(const BspBuildArtifacts &artifacts) noexcept
{
    std::array<formats::LumpView, formats::kQuake2LumpCount> views{};
    for (std::size_t i = 0; i < artifacts.quake2_lumps.size(); ++i)
    {
        const auto &lump = artifacts.quake2_lumps[i];
        if (lump.size == 0)
        {
            views[i] = {};
            continue;
        }

        views[i].data = static_cast<const std::byte *>(lump.data.get());
        views[i].size = lump.size;
    }
    return views;
}

void FreeTree(BspBuildArtifacts &artifacts) noexcept
{
    artifacts.Reset();
}

} // namespace bspc::builder

