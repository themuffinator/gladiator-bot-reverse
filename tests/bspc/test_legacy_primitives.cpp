#include <cassert>
#include <stdexcept>
#include <string>

#include "brush_bsp.hpp"
#include "csg.hpp"
#include "leakfile.hpp"
#include "portals.hpp"
#include "tree.hpp"

namespace legacy = bspc::legacy;

legacy::Vec3 MakeVec(double x, double y, double z)
{
    return legacy::Vec3{x, y, z};
}

void BuildCubePlanes()
{
    legacy::ClearMapPlanes();
    legacy::Plane px{MakeVec(1.0, 0.0, 0.0), 64.0};
    legacy::Plane nx{MakeVec(-1.0, 0.0, 0.0), 64.0};
    legacy::Plane py{MakeVec(0.0, 1.0, 0.0), 64.0};
    legacy::Plane ny{MakeVec(0.0, -1.0, 0.0), 64.0};
    legacy::Plane pz{MakeVec(0.0, 0.0, 1.0), 64.0};
    legacy::Plane nz{MakeVec(0.0, 0.0, -1.0), 64.0};
    legacy::AppendPlane(px);
    legacy::AppendPlane(nx);
    legacy::AppendPlane(py);
    legacy::AppendPlane(ny);
    legacy::AppendPlane(pz);
    legacy::AppendPlane(nz);
}

legacy::WindingPtr MakeQuad(const legacy::Vec3 &a,
                             const legacy::Vec3 &b,
                             const legacy::Vec3 &c,
                             const legacy::Vec3 &d)
{
    auto winding = std::make_shared<legacy::Winding>();
    winding->points.push_back(a);
    winding->points.push_back(b);
    winding->points.push_back(c);
    winding->points.push_back(d);
    return winding;
}

legacy::BspBrush MakeValidCubeBrush()
{
    legacy::BspBrush brush;
    brush.sides.resize(6);
    brush.sides[0].planenum = 0;
    brush.sides[0].winding = MakeQuad(MakeVec(64.0, -64.0, -64.0),
                                      MakeVec(64.0, 64.0, -64.0),
                                      MakeVec(64.0, 64.0, 64.0),
                                      MakeVec(64.0, -64.0, 64.0));
    brush.sides[1].planenum = 1;
    brush.sides[1].winding = MakeQuad(MakeVec(-64.0, -64.0, -64.0),
                                      MakeVec(-64.0, -64.0, 64.0),
                                      MakeVec(-64.0, 64.0, 64.0),
                                      MakeVec(-64.0, 64.0, -64.0));
    brush.sides[2].planenum = 2;
    brush.sides[2].winding = MakeQuad(MakeVec(-64.0, 64.0, -64.0),
                                      MakeVec(-64.0, 64.0, 64.0),
                                      MakeVec(64.0, 64.0, 64.0),
                                      MakeVec(64.0, 64.0, -64.0));
    brush.sides[3].planenum = 3;
    brush.sides[3].winding = MakeQuad(MakeVec(-64.0, -64.0, -64.0),
                                      MakeVec(64.0, -64.0, -64.0),
                                      MakeVec(64.0, -64.0, 64.0),
                                      MakeVec(-64.0, -64.0, 64.0));
    brush.sides[4].planenum = 4;
    brush.sides[4].winding = MakeQuad(MakeVec(-64.0, -64.0, 64.0),
                                      MakeVec(64.0, -64.0, 64.0),
                                      MakeVec(64.0, 64.0, 64.0),
                                      MakeVec(-64.0, 64.0, 64.0));
    brush.sides[5].planenum = 5;
    brush.sides[5].winding = MakeQuad(MakeVec(-64.0, -64.0, -64.0),
                                      MakeVec(-64.0, 64.0, -64.0),
                                      MakeVec(64.0, 64.0, -64.0),
                                      MakeVec(64.0, -64.0, -64.0));
    return brush;
}

void TestPortalAllocation()
{
    auto &stats = legacy::GetPortalStats();
    assert(stats.active == 0);

    legacy::Portal *first = legacy::AllocPortal();
    assert(legacy::GetPortalStats().active == 1);

    legacy::Portal *second = legacy::AllocPortal();
    assert(legacy::GetPortalStats().active == 2);
    assert(legacy::GetPortalStats().peak == 2);

    legacy::FreePortal(first);
    assert(legacy::GetPortalStats().active == 1);
    legacy::FreePortal(second);
    assert(legacy::GetPortalStats().active == 0);
}

void TestBrushValidation()
{
    BuildCubePlanes();
    legacy::BspBrush brush = MakeValidCubeBrush();
    legacy::CheckBSPBrush(brush);
    assert(legacy::GetBrushCheckMessages().empty());

    legacy::BspBrush broken = brush;
    broken.sides[0].winding->points[0] = MakeVec(64.0, -64.0, 256.0);
    bool threw = false;
    try
    {
        legacy::CheckBSPBrush(broken);
    }
    catch (const std::runtime_error &)
    {
        threw = true;
    }
    assert(threw);
}

void TestLeakFile()
{
    std::unique_ptr<legacy::Tree> tree = legacy::Tree_Alloc();
    legacy::Portal *portal = legacy::AllocPortal();
    auto winding = std::make_shared<legacy::Winding>();
    winding->points.push_back(MakeVec(0.0, 0.0, 0.0));
    winding->points.push_back(MakeVec(16.0, 0.0, 0.0));
    winding->points.push_back(MakeVec(0.0, 16.0, 0.0));
    portal->winding = winding;

    legacy::Node &leaf = legacy::Tree_EmplaceLeaf(*tree);
    leaf.occupied = 1;
    legacy::Entity &entity = legacy::Tree_EmplaceEntity(*tree, MakeVec(10.0, 20.0, 30.0));
    leaf.occupant = &entity;
    legacy::AddPortalToNodes(*portal, tree->outside_node, leaf);
    tree->outside_node.occupied = 2;

    std::string leak = legacy::LeakFile(*tree, "unit_test.map");
    assert(!leak.empty());
    assert(leak.find("10.000000 20.000000 30.000000") != std::string::npos);

    legacy::Tree_Free(*tree);
    assert(legacy::GetPortalStats().active == 0);
}

int main()
{
    TestPortalAllocation();
    TestBrushValidation();
    TestLeakFile();
    return 0;
}

