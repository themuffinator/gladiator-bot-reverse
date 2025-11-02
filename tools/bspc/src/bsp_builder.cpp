#include "bsp_builder.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string_view>

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

    return static_cast<std::size_t>(std::count(leak_text.begin(), leak_text.end(), '\n'));
}

void BspBuildArtifacts::Reset() noexcept
{
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

    legacy::PortalFileResult portal_file = legacy::WritePortalFile(*tree);
    out_artifacts.portal_text = std::move(portal_file.text);
    out_artifacts.portal_cluster_count = portal_file.metrics.cluster_count;
    out_artifacts.portal_count = portal_file.metrics.portal_count;

    out_artifacts.leak_text = legacy::LeakFile(*tree, world.source_name);
    out_artifacts.leak_point_count = CountLeakPoints(out_artifacts.leak_text);

    auto &entities_lump = out_artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kEntities)];
    if (!world.entities_text.empty())
    {
        entities_lump.Allocate(world.entities_text.size(), false);
        std::memcpy(entities_lump.data.get(), world.entities_text.data(), world.entities_text.size());
    }

    log::Info("BSP tree populated\n");

    legacy::Tree_Free(*tree);

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
    artifacts.Reset();
}

} // namespace bspc::builder

