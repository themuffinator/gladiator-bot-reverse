#include "brush.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace bspc::geometry
{

namespace
{

BrushSide MakeBrushSide(const Plane &plane, bool bevel)
{
    BrushSide side;
    side.plane = plane;
    side.bevel = bevel;
    return side;
}

} // namespace

void CreateBrushWindings(Brush &brush)
{
    for (std::size_t i = 0; i < brush.sides.size(); ++i)
    {
        BrushSide &side = brush.sides[i];
        Winding winding = BaseWindingForPlane(side.plane.normal, side.plane.dist);
        if (winding.empty())
        {
            side.winding.points.clear();
            continue;
        }

        for (std::size_t j = 0; j < brush.sides.size(); ++j)
        {
            if (i == j)
            {
                continue;
            }
            if (brush.sides[j].bevel)
            {
                continue;
            }

            const Plane clip_plane = InvertPlane(brush.sides[j].plane);
            std::optional<Winding> winding_opt = winding;
            ChopWindingInPlace(winding_opt, clip_plane.normal, clip_plane.dist, 0.0);
            if (!winding_opt.has_value())
            {
                winding.points.clear();
                break;
            }
            winding = std::move(*winding_opt);
        }

        side.winding = std::move(winding);
    }

    BoundBrush(brush);
}

void BoundBrush(Brush &brush)
{
    math::ClearBounds(brush.mins, brush.maxs);
    for (BrushSide &side : brush.sides)
    {
        if (side.winding.empty())
        {
            continue;
        }
        for (const auto &point : side.winding.points)
        {
            math::AddPointToBounds(point, brush.mins, brush.maxs);
        }
    }
}

double BrushVolume(const Brush &brush)
{
    if (brush.sides.empty())
    {
        return 0.0;
    }

    const Winding *reference = nullptr;
    std::size_t reference_index = 0;
    for (std::size_t i = 0; i < brush.sides.size(); ++i)
    {
        if (!brush.sides[i].winding.empty())
        {
            reference = &brush.sides[i].winding;
            reference_index = i;
            break;
        }
    }
    if (reference == nullptr)
    {
        return 0.0;
    }

    const math::Vec3 corner = reference->points[0];
    double volume = 0.0;
    for (std::size_t i = reference_index; i < brush.sides.size(); ++i)
    {
        const BrushSide &side = brush.sides[i];
        if (side.winding.empty())
        {
            continue;
        }

        const double d = -(math::DotProduct(corner, side.plane.normal) - side.plane.dist);
        const double area = WindingArea(side.winding);
        volume += d * area;
    }

    return volume / 3.0;
}

int BrushMostlyOnSide(const Brush &brush, const Plane &plane)
{
    double max_distance = 0.0;
    int side = kPlaneFront;

    for (const BrushSide &brush_side : brush.sides)
    {
        if (brush_side.winding.empty())
        {
            continue;
        }
        for (const auto &point : brush_side.winding.points)
        {
            const double d = math::DotProduct(point, plane.normal) - plane.dist;
            if (d > max_distance)
            {
                max_distance = d;
                side = kPlaneFront;
            }
            if (-d > max_distance)
            {
                max_distance = -d;
                side = kPlaneBack;
            }
        }
    }

    return side;
}

bool BrushIsConvex(const Brush &brush)
{
    for (std::size_t i = 0; i < brush.sides.size(); ++i)
    {
        const BrushSide &a = brush.sides[i];
        if (a.winding.empty())
        {
            continue;
        }
        for (std::size_t j = 0; j < brush.sides.size(); ++j)
        {
            if (i == j)
            {
                continue;
            }
            const BrushSide &b = brush.sides[j];
            if (b.winding.empty())
            {
                continue;
            }
            if (WindingsNonConvex(a.winding, b.winding, a.plane.normal, b.plane.normal, a.plane.dist, b.plane.dist))
            {
                return false;
            }
        }
    }
    return true;
}

std::optional<std::pair<Brush, Brush>> SplitBrush(const Brush &brush, const Plane &plane, double epsilon)
{
    double front_max = 0.0;
    double back_max = 0.0;

    for (const BrushSide &side : brush.sides)
    {
        if (side.winding.empty())
        {
            continue;
        }
        for (const auto &point : side.winding.points)
        {
            const double d = math::DotProduct(point, plane.normal) - plane.dist;
            if (d > front_max)
            {
                front_max = d;
            }
            if (d < back_max)
            {
                back_max = d;
            }
        }
    }

    if (front_max < 0.2 - epsilon)
    {
        return std::pair{Brush{}, brush};
    }
    if (back_max > -0.2 + epsilon)
    {
        return std::pair{brush, Brush{}};
    }

    Winding mid = BaseWindingForPlane(plane.normal, plane.dist);
    for (const BrushSide &side : brush.sides)
    {
        if (side.bevel)
        {
            continue;
        }
        std::optional<Winding> mid_opt = mid;
        ChopWindingInPlace(mid_opt, side.plane.normal, side.plane.dist, 0.0);
        if (!mid_opt.has_value())
        {
            break;
        }
        mid = std::move(*mid_opt);
    }

    if (mid.empty() || WindingIsTiny(mid))
    {
        const int side = BrushMostlyOnSide(brush, plane);
        if (side == kPlaneFront)
        {
            return std::pair{brush, Brush{}};
        }
        if (side == kPlaneBack)
        {
            return std::pair{Brush{}, brush};
        }
        return std::nullopt;
    }

    Brush front;
    Brush back;
    front.sides.reserve(brush.sides.size() + 1);
    back.sides.reserve(brush.sides.size() + 1);

    for (const BrushSide &side : brush.sides)
    {
        if (side.winding.empty())
        {
            continue;
        }

        Winding front_winding;
        Winding back_winding;
        ClipWindingEpsilon(side.winding, plane.normal, plane.dist, epsilon, front_winding, back_winding);

        if (!front_winding.empty())
        {
            BrushSide new_side = side;
            new_side.winding = std::move(front_winding);
            front.sides.push_back(std::move(new_side));
        }
        if (!back_winding.empty())
        {
            BrushSide new_side = side;
            new_side.winding = std::move(back_winding);
            back.sides.push_back(std::move(new_side));
        }
    }

    if (front.sides.size() < 3 || back.sides.size() < 3)
    {
        return std::nullopt;
    }

    BoundBrush(front);
    BoundBrush(back);

    auto check_bounds = [](const Brush &b) {
        for (int i = 0; i < 3; ++i)
        {
            if (b.mins[i] < -math::kBogusRange || b.maxs[i] > math::kBogusRange)
            {
                return false;
            }
        }
        return true;
    };

    if (!check_bounds(front) || !check_bounds(back))
    {
        return std::nullopt;
    }

    BrushSide front_mid = MakeBrushSide(InvertPlane(plane), false);
    front_mid.visible = false;
    front_mid.winding = mid;
    front.sides.push_back(front_mid);

    BrushSide back_mid = MakeBrushSide(plane, false);
    back_mid.visible = false;
    back_mid.winding = std::move(mid);
    back.sides.push_back(std::move(back_mid));

    if (BrushVolume(front) < 1.0)
    {
        front.sides.clear();
    }
    if (BrushVolume(back) < 1.0)
    {
        back.sides.clear();
    }

    if (front.sides.empty() && back.sides.empty())
    {
        return std::nullopt;
    }

    return std::pair{std::move(front), std::move(back)};
}

std::uint64_t HashBrush(const Brush &brush, double normal_epsilon, double dist_epsilon)
{
    std::vector<std::uint64_t> plane_hashes;
    plane_hashes.reserve(brush.sides.size());
    for (const BrushSide &side : brush.sides)
    {
        plane_hashes.push_back(HashPlane(side.plane, normal_epsilon, dist_epsilon));
    }
    std::sort(plane_hashes.begin(), plane_hashes.end());

    std::uint64_t hash = 1469598103934665603ull;
    for (std::uint64_t value : plane_hashes)
    {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    return hash;
}

Brush MakeBoxBrush(const math::Vec3 &mins, const math::Vec3 &maxs)
{
    Brush brush;
    brush.sides.reserve(6);

    for (int axis = 0; axis < 3; ++axis)
    {
        math::Vec3 normal = math::MakeVec3(0.0, 0.0, 0.0);
        normal[axis] = 1.0;
        Plane max_plane = MakePlane(normal, maxs[axis]);
        brush.sides.push_back(MakeBrushSide(max_plane, false));

        normal[axis] = -1.0;
        Plane min_plane = MakePlane(normal, -mins[axis]);
        brush.sides.push_back(MakeBrushSide(min_plane, false));
    }

    CreateBrushWindings(brush);
    return brush;
}

} // namespace bspc::geometry

