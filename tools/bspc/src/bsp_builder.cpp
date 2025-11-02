#include "bsp_builder.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "logging.hpp"

#include "brush_bsp.hpp"
#include "csg.hpp"
#include "legacy_common.hpp"
#include "leakfile.hpp"
#include "map_parser.hpp"
#include "portals.hpp"
#include "tree.hpp"

namespace bspc::builder
{
namespace
{

constexpr int kContentsDetail = 0x08000000;
constexpr int kSurfaceDetail = 0x08000000;

legacy::Vec3 ToLegacyVec(const map::Vec3 &value) noexcept
{
    return legacy::Vec3{static_cast<double>(value.x),
                        static_cast<double>(value.y),
                        static_cast<double>(value.z)};
}

legacy::Vec3 ToLegacyVec(const ParsedWorld::Vec3 &value) noexcept
{
    return legacy::Vec3{static_cast<double>(value.x),
                        static_cast<double>(value.y),
                        static_cast<double>(value.z)};
}

legacy::Plane ToLegacyPlane(const ParsedWorld::Plane &plane) noexcept
{
    legacy::Plane converted{};
    converted.normal = {static_cast<double>(plane.normal[0]),
                        static_cast<double>(plane.normal[1]),
                        static_cast<double>(plane.normal[2])};
    converted.dist = static_cast<double>(plane.distance);
    return converted;
}

legacy::WindingPtr BuildWindingFromPlane(const map::Plane &plane)
{
    if (!plane.has_vertices)
    {
        return {};
    }

    auto winding = std::make_shared<legacy::Winding>();
    for (const map::Vec3 &vertex : plane.vertices)
    {
        winding->points.push_back(ToLegacyVec(vertex));
    }

    if (winding->points.size() < 3)
    {
        return {};
    }

    return winding;
}

bool BrushIsDetail(const map::Brush &map_brush, const ParsedWorld::Brush &brush)
{
    if (map_brush.type == map::Brush::Type::kPatch)
    {
        return true;
    }

    if (brush.contains_liquid)
    {
        return true;
    }

    for (const map::Plane &plane : map_brush.planes)
    {
        if ((plane.contents & kContentsDetail) != 0)
        {
            return true;
        }
        if ((plane.surface_flags & kSurfaceDetail) != 0)
        {
            return true;
        }
    }

    return false;
}

struct BuildStats
{
    std::size_t structural_brushes = 0;
    std::size_t detail_brushes = 0;
    std::size_t total_sides = 0;
    std::size_t flood_regions = 0;
    legacy::Vec3 mins{std::numeric_limits<double>::infinity(),
                      std::numeric_limits<double>::infinity(),
                      std::numeric_limits<double>::infinity()};
    legacy::Vec3 maxs{-std::numeric_limits<double>::infinity(),
                      -std::numeric_limits<double>::infinity(),
                      -std::numeric_limits<double>::infinity()};
    bool has_bounds = false;
};

void AccumulateBounds(BuildStats &stats, const legacy::BspBrush &brush)
{
    stats.has_bounds = true;
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        stats.mins[axis] = std::min(stats.mins[axis], brush.mins[axis]);
        stats.maxs[axis] = std::max(stats.maxs[axis], brush.maxs[axis]);
    }
}

std::string FormatVec(const legacy::Vec3 &vec)
{
    std::ostringstream stream;
    stream << vec.x << ' ' << vec.y << ' ' << vec.z;
    return stream.str();
}

bool ConvertBrush(const ParsedWorld::Brush &brush,
                  const map::Brush &map_brush,
                  const std::vector<int> &plane_lookup,
                  std::vector<std::unique_ptr<legacy::MapBrush>> &map_storage,
                  legacy::BspBrush &out_brush)
{
    if (map_brush.planes.empty() || brush.sides.size() != map_brush.planes.size())
    {
        return false;
    }

    legacy::BspBrush converted;
    converted.sides.reserve(brush.sides.size());

    for (std::size_t i = 0; i < brush.sides.size(); ++i)
    {
        const auto &side = brush.sides[i];
        if (side.plane_index >= plane_lookup.size())
        {
            return false;
        }

        const int planenum = plane_lookup[side.plane_index];
        if (planenum < 0)
        {
            return false;
        }

        legacy::BrushSide converted_side;
        converted_side.planenum = planenum;
        converted_side.contents = side.contents;
        converted_side.surface_flags = side.surface_flags;
        converted_side.value = side.value;
        converted_side.winding = BuildWindingFromPlane(map_brush.planes[i]);
        if (!converted_side.winding)
        {
            return false;
        }
        converted.sides.push_back(std::move(converted_side));
    }

    converted.mins = ToLegacyVec(map_brush.mins);
    converted.maxs = ToLegacyVec(map_brush.maxs);

    try
    {
        legacy::CheckBSPBrush(converted);
    }
    catch (const std::runtime_error &)
    {
        return false;
    }

    auto map_brush_ptr = std::make_unique<legacy::MapBrush>();
    map_brush_ptr->entitynum = static_cast<int>(brush.source_entity);
    map_brush_ptr->brushnum = static_cast<int>(brush.source_brush);
    int contents = 0;
    for (const map::Plane &plane : map_brush.planes)
    {
        contents |= plane.contents;
    }
    map_brush_ptr->contents = contents;
    map_brush_ptr->mins = converted.mins;
    map_brush_ptr->maxs = converted.maxs;

    map_storage.push_back(std::move(map_brush_ptr));
    converted.original = map_storage.back().get();

    out_brush = std::move(converted);
    return true;
}

void WriteCountsToLump(const std::vector<std::size_t> &counts, formats::OwnedLump &lump)
{
    if (counts.empty())
    {
        lump.Reset();
        return;
    }

    lump.Allocate(counts.size() * sizeof(std::uint32_t), false);
    auto *dest = static_cast<std::uint32_t *>(lump.data.get());
    for (std::size_t i = 0; i < counts.size(); ++i)
    {
        dest[i] = static_cast<std::uint32_t>(counts[i] & 0xFFFFFFFFu);
    }
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
    portal_slice_count = 0;
    flood_fill_regions = 0;
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

    std::vector<int> plane_lookup(world.planes.size(), -1);
    for (std::size_t i = 0; i < world.planes.size(); ++i)
    {
        plane_lookup[i] = legacy::AppendPlane(ToLegacyPlane(world.planes[i]));
    }

    std::vector<legacy::BspBrush> structural_brushes;
    std::vector<legacy::BspBrush> detail_brushes;
    structural_brushes.reserve(world.brushes.size());
    detail_brushes.reserve(world.brushes.size());

    BuildStats stats;

    if (world.map_geometry)
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

            const map::Entity &map_entity = map_geometry.entities[brush.source_entity];
            if (brush.source_brush == ParsedWorld::kInvalidIndex ||
                brush.source_brush >= map_entity.brushes.size())
            {
                continue;
            }

            const map::Brush &map_brush = map_entity.brushes[brush.source_brush];
            if (map_brush.type == map::Brush::Type::kPatch)
            {
                ++stats.detail_brushes;
                continue;
            }

            legacy::BspBrush legacy_brush;
            if (!ConvertBrush(brush, map_brush, plane_lookup, out_artifacts.map_brushes, legacy_brush))
            {
                continue;
            }

            if (BrushIsDetail(map_brush, brush))
            {
                ++stats.detail_brushes;
                detail_brushes.push_back(std::move(legacy_brush));
            }
            else
            {
                ++stats.structural_brushes;
                stats.total_sides += legacy_brush.sides.size();
                AccumulateBounds(stats, legacy_brush);
                structural_brushes.push_back(std::move(legacy_brush));
            }
        }
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

    std::ostringstream portal_stream;
    portal_stream << "# BSPC portal diagnostics\n";
    portal_stream << "source: " << world.source_name << "\n";
    portal_stream << "structural_brushes: " << stats.structural_brushes << "\n";
    portal_stream << "detail_brushes: " << stats.detail_brushes << "\n";
    portal_stream << "portals: " << stats.total_sides << "\n";
    portal_stream << "bounds_min: " << FormatVec(stats.mins) << "\n";
    portal_stream << "bounds_max: " << FormatVec(stats.maxs) << "\n";
    for (std::size_t i = 0; i < structural_brushes.size(); ++i)
    {
        portal_stream << "brush " << i << " sides " << structural_brushes[i].sides.size();
        portal_stream << " bounds " << FormatVec(structural_brushes[i].mins) << " -> ";
        portal_stream << FormatVec(structural_brushes[i].maxs) << "\n";
    }
    out_artifacts.portal_text = portal_stream.str();

    std::string leak_trace = legacy::LeakFile(*tree, world.source_name);
    if (!leak_trace.empty())
    {
        out_artifacts.leak_text = std::move(leak_trace);
    }
    else
    {
        std::ostringstream flood_stream;
        flood_stream << "# BSPC visibility diagnostics\n";
        flood_stream << "source: " << world.source_name << "\n";
        flood_stream << "regions: " << out_artifacts.flood_fill_regions << "\n";
        flood_stream << "structural_brushes: " << stats.structural_brushes << "\n";
        flood_stream << "detail_brushes: " << stats.detail_brushes << "\n";
        out_artifacts.leak_text = flood_stream.str();
    }

    auto &entities_lump = out_artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kEntities)];
    if (!world.entities_text.empty())
    {
        entities_lump.Allocate(world.entities_text.size(), false);
        std::memcpy(entities_lump.data.get(), world.entities_text.data(), world.entities_text.size());
    }

    auto &planes_lump = out_artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kPlanes)];
    std::vector<std::size_t> plane_counts;
    plane_counts.reserve(structural_brushes.size());
    for (const auto &brush : structural_brushes)
    {
        plane_counts.push_back(brush.sides.size());
    }
    WriteCountsToLump(plane_counts, planes_lump);

    auto &visibility_lump = out_artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kVisibility)];
    std::vector<std::size_t> region_counts;
    if (out_artifacts.flood_fill_regions > 0)
    {
        region_counts.push_back(out_artifacts.flood_fill_regions);
    }
    WriteCountsToLump(region_counts, visibility_lump);

    auto &nodes_lump = out_artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kNodes)];
    std::vector<std::size_t> brush_counts;
    if (stats.structural_brushes > 0 || stats.detail_brushes > 0)
    {
        brush_counts.push_back(stats.structural_brushes);
        brush_counts.push_back(stats.detail_brushes);
    }
    WriteCountsToLump(brush_counts, nodes_lump);

    auto &models_lump = out_artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kModels)];
    if (stats.structural_brushes > 0)
    {
        models_lump.Allocate(sizeof(std::uint32_t));
        auto *dest = static_cast<std::uint32_t *>(models_lump.data.get());
        *dest = static_cast<std::uint32_t>(stats.structural_brushes);
    }
    else
    {
        models_lump.Reset();
    }

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

