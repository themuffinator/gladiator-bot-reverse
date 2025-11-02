#include "bsp_builder.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
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
#include "map_parser.hpp"
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

struct TreeEmission
{
    std::vector<Quake1NodeDisk> nodes;
    std::vector<Quake1LeafDisk> leaves;
};

TreeEmission EmitTreeLumps(const TreeCollection &collection)
{
    TreeEmission emission;
    emission.nodes.resize(collection.nodes.size());
    emission.leaves.resize(collection.leaves.size());

    for (std::size_t index = 0; index < collection.nodes.size(); ++index)
    {
        const legacy::Node *node = collection.nodes[index];
        Quake1NodeDisk disk;
        disk.planenum = std::max(0, node->planenum);
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            disk.mins[axis] = ClampToShort(node->mins[axis]);
            disk.maxs[axis] = ClampToShort(node->maxs[axis]);
        }

        for (std::size_t child_index = 0; child_index < 2; ++child_index)
        {
            const legacy::Node *child = node->children[child_index];
            if (!child)
            {
                disk.children[child_index] = -1;
                continue;
            }

            const auto node_it = collection.node_indices.find(child);
            if (node_it != collection.node_indices.end())
            {
                disk.children[child_index] = static_cast<std::int16_t>(node_it->second);
                continue;
            }

            const auto leaf_it = collection.leaf_indices.find(child);
            if (leaf_it != collection.leaf_indices.end())
            {
                const auto leaf_index = static_cast<std::int32_t>(leaf_it->second);
                disk.children[child_index] = static_cast<std::int16_t>(-leaf_index - 1);
            }
            else
            {
                disk.children[child_index] = -1;
            }
        }

        emission.nodes[index] = disk;
    }
    map_brush_ptr->contents = contents;
    map_brush_ptr->mins = converted.mins;
    map_brush_ptr->maxs = converted.maxs;

    for (std::size_t index = 0; index < collection.leaves.size(); ++index)
    {
        const legacy::Node *leaf = collection.leaves[index];
        Quake1LeafDisk disk;
        if (leaf)
        {
            disk.contents = leaf->contents;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                disk.mins[axis] = ClampToShort(leaf->mins[axis]);
                disk.maxs[axis] = ClampToShort(leaf->maxs[axis]);
            }
        }
        disk.visibility = 0xFFFFu;
        emission.leaves[index] = disk;
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
    auto &entities_lump = artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kEntities)];
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
    auto &planes_lump = artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kPlanes)];
    WriteVectorToLump(plane_data, planes_lump);

    artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kTextures)].Reset();
    artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kVertices)].Reset();
    artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kVisibility)].Reset();

    const TreeCollection collection = CollectTree(tree);
    const TreeEmission emission = EmitTreeLumps(collection);

    auto &nodes_lump = artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kNodes)];
    WriteVectorToLump(emission.nodes, nodes_lump);

    artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kTexInfo)].Reset();
    artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kFaces)].Reset();
    artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kLighting)].Reset();
    artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kClipNodes)].Reset();

    auto &leaves_lump = artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kLeaves)];
    WriteVectorToLump(emission.leaves, leaves_lump);

    artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kMarkSurfaces)].Reset();
    artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kEdges)].Reset();
    artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kSurfEdges)].Reset();

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

    auto &models_lump = artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kModels)];
    WriteVectorToLump(models, models_lump);

    ValidateCount(plane_data.size(), planes.size(), "planes");
    ValidateCount(emission.nodes.size(), collection.nodes.size(), "nodes");
    ValidateCount(emission.leaves.size(), collection.leaves.size(), "leaves");
    ValidateCount(models.size(), tree.headnode ? std::size_t{1} : std::size_t{0}, "models");
}

void BspBuildArtifacts::Reset() noexcept
{
    if (tree)
    {
        legacy::Tree_Free(tree);
    }
    tree.reset();
    map_brushes.clear();

    for (auto &lump : lumps)
    {
        lump.Reset();
    }
    portal_text.clear();
    leak_text.clear();
    portal_cluster_count = 0;
    portal_count = 0;
    leak_point_count = 0;
}

BspBuildArtifacts::~BspBuildArtifacts() noexcept
{
    Reset();
}

} // namespace

bool BuildBspTree(const ParsedWorld &world, BspBuildArtifacts &out_artifacts)
{
    out_artifacts.Reset();

    legacy::ResetBrushBSP();
    legacy::ClearMapPlanes();

    log::Info("--- BSP tree ---\n");
    log::Info("processing %zu parsed brushes\n", world.brushes.size());

    std::unique_ptr<legacy::Tree> tree = legacy::Tree_Alloc();

    std::unique_ptr<legacy::Tree> tree = legacy::Tree_Alloc();

    if (!world.lines.empty())
    {
        const auto &map_geometry = *world.map_geometry;
        for (const ParsedWorld::Brush &brush : world.brushes)
        {
            if (brush.source != ParsedWorld::Brush::Source::kMapBrush &&
                brush.source != ParsedWorld::Brush::Source::kMapPatch)
            {
                continue;
            }

            if (brush.source_entity == ParsedWorld::kInvalidIndex ||
                brush.source_entity >= map_geometry.entities.size())
            {
                continue;
            }

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
    else
    {
        log::Info("no parsed map geometry available; producing empty BSP tree\n");
    }

    if (!stats.has_bounds)
    {
        stats.mins = legacy::Vec3{};
        stats.maxs = legacy::Vec3{};
    }

    tree->mins = stats.mins;
    tree->maxs = stats.maxs;
    tree->headnode->mins = stats.mins;
    tree->headnode->maxs = stats.maxs;

    if (!structural_brushes.empty())
    {
        tree->headnode->volume = std::make_unique<legacy::BspBrush>(structural_brushes.front());
    }

    for (const auto &brush : structural_brushes)
    {
        auto node = std::make_unique<legacy::Node>();
        node->planenum = legacy::kPlanenumLeaf;
        node->mins = brush.mins;
        node->maxs = brush.maxs;
        node->volume = std::make_unique<legacy::BspBrush>(brush);
        tree->extra_nodes.push_back(std::move(node));
    }

    auto &portal_stats = legacy::GetPortalStats();
    portal_stats.active = stats.total_sides;
    portal_stats.peak = stats.total_sides;
    portal_stats.memory = stats.total_sides * sizeof(legacy::Portal);

    auto &brush_stats = legacy::GetBrushBspStats();
    brush_stats = legacy::BrushBspStats{};
    brush_stats.nodes = structural_brushes.size();
    brush_stats.active_brushes = structural_brushes.size() + detail_brushes.size();
    brush_stats.solid_leaf_nodes = structural_brushes.empty() ? 0 : 1;
    brush_stats.total_sides = stats.total_sides;
    brush_stats.brush_memory = brush_stats.active_brushes * sizeof(legacy::BspBrush);
    brush_stats.peak_brush_memory = brush_stats.brush_memory;
    brush_stats.node_memory = structural_brushes.size() * sizeof(legacy::Node);
    brush_stats.peak_total_memory = brush_stats.brush_memory + brush_stats.node_memory;

    stats.flood_regions = 0;
    if (stats.structural_brushes > 0)
    {
        stats.flood_regions = 1;
    }
    stats.flood_regions += detail_brushes.size();
    if (stats.flood_regions == 0 && !world.entities.empty())
    {
        stats.flood_regions = 1;
    }

    out_artifacts.portal_slice_count = stats.total_sides;
    out_artifacts.flood_fill_regions = stats.flood_regions;

    const auto &portal_stats = legacy::GetPortalStats();
    log::Info("portal allocator peak: %d active (%d bytes)\n",
              portal_stats.peak,
              portal_stats.memory);

    WriteQuake1BspLumps(world, *tree, out_artifacts);

    log::Info("structural brushes: %zu, detail brushes: %zu\n", stats.structural_brushes, stats.detail_brushes);
    log::Info("generated %zu portal slices and %zu regions\n",
              out_artifacts.portal_slice_count,
              out_artifacts.flood_fill_regions);

    out_artifacts.tree = std::move(tree);
    return true;
}

std::array<formats::LumpView, formats::kQuake1LumpCount> MakeLumpViews(const BspBuildArtifacts &artifacts) noexcept
{
    std::array<formats::LumpView, formats::kQuake1LumpCount> views{};
    for (std::size_t i = 0; i < artifacts.lumps.size(); ++i)
    {
        const auto &lump = artifacts.lumps[i];
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
    if (artifacts.tree)
    {
        legacy::Tree_Free(artifacts.tree);
    }
    artifacts.map_brushes.clear();
}

} // namespace bspc::builder

