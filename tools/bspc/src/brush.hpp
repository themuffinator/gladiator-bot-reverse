#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "math.hpp"
#include "plane.hpp"
#include "winding.hpp"

namespace bspc::geometry
{

struct BrushSide
{
    Plane plane;
    bool bevel = false;
    bool visible = true;
    Winding winding;
};

struct Brush
{
    std::vector<BrushSide> sides;
    math::Vec3 mins = math::MakeVec3(0.0, 0.0, 0.0);
    math::Vec3 maxs = math::MakeVec3(0.0, 0.0, 0.0);
};

enum BrushPlaneSide
{
    kPlaneFront = 1,
    kPlaneBack = 2,
    kPlaneBoth = kPlaneFront | kPlaneBack,
};

void CreateBrushWindings(Brush &brush);
void BoundBrush(Brush &brush);
double BrushVolume(const Brush &brush);
int BrushMostlyOnSide(const Brush &brush, const Plane &plane);
bool BrushIsConvex(const Brush &brush);
std::optional<std::pair<Brush, Brush>> SplitBrush(const Brush &brush, const Plane &plane, double epsilon = 0.0);
std::uint64_t HashBrush(const Brush &brush, double normal_epsilon = math::kNormalEpsilon, double dist_epsilon = math::kDistEpsilon);

Brush MakeBoxBrush(const math::Vec3 &mins, const math::Vec3 &maxs);

} // namespace bspc::geometry

