#include "winding.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace bspc::geometry
{

namespace
{

enum Side
{
    kSideFront = 0,
    kSideBack = 1,
    kSideOn = 2,
};

math::Vec3 InterpolateEdge(const math::Vec3 &a, const math::Vec3 &b, double da, double db, const math::Vec3 &normal, double dist)
{
    const double dot = da / (da - db);
    math::Vec3 mid{};
    for (std::size_t i = 0; i < 3; ++i)
    {
        if (normal[i] == 1.0)
        {
            mid[i] = dist;
        }
        else if (normal[i] == -1.0)
        {
            mid[i] = -dist;
        }
        else
        {
            mid[i] = a[i] + dot * (b[i] - a[i]);
        }
    }
    return mid;
}

} // namespace

Winding BaseWindingForPlane(const math::Vec3 &normal, double dist)
{
    const int dominant_axis = [&]() {
        double max_value = -math::kBogusRange;
        int index = -1;
        for (int i = 0; i < 3; ++i)
        {
            const double value = std::abs(normal[i]);
            if (value > max_value)
            {
                max_value = value;
                index = i;
            }
        }
        return index;
    }();

    if (dominant_axis == -1)
    {
        return {};
    }

    math::Vec3 vup = math::MakeVec3(0.0, 0.0, 0.0);
    switch (dominant_axis)
    {
        case 0:
        case 1:
            vup[2] = 1.0;
            break;
        case 2:
            vup[0] = 1.0;
            break;
        default:
            break;
    }

    double d = math::DotProduct(vup, normal);
    vup = math::VectorMA(vup, -d, normal);
    math::VectorNormalize(vup);

    math::Vec3 origin = math::VectorScale(normal, dist);
    math::Vec3 vright = math::CrossProduct(vup, normal);

    vup = math::VectorScale(vup, math::kBogusRange);
    vright = math::VectorScale(vright, math::kBogusRange);

    Winding winding{};
    winding.points.resize(4);

    winding.points[0] = math::VectorAdd(math::VectorSubtract(origin, vright), vup);
    winding.points[1] = math::VectorAdd(math::VectorAdd(origin, vright), vup);
    winding.points[2] = math::VectorSubtract(math::VectorAdd(origin, vright), vup);
    winding.points[3] = math::VectorSubtract(math::VectorSubtract(origin, vright), vup);
    return winding;
}

void ClipWindingEpsilon(const Winding &in, const math::Vec3 &normal, double dist, double epsilon, Winding &front, Winding &back)
{
    front.points.clear();
    back.points.clear();

    if (in.empty())
    {
        return;
    }

    const std::size_t count = in.size();
    std::vector<double> distances(count + 1, 0.0);
    std::vector<int> sides(count + 1, kSideOn);
    int counts[3] = {0, 0, 0};

    for (std::size_t i = 0; i < count; ++i)
    {
        const math::Vec3 &point = in.points[i];
        const double d = math::DotProduct(point, normal) - dist;
        distances[i] = d;
        if (d > epsilon)
        {
            sides[i] = kSideFront;
        }
        else if (d < -epsilon)
        {
            sides[i] = kSideBack;
        }
        else
        {
            sides[i] = kSideOn;
        }
        counts[sides[i]]++;
    }

    sides[count] = sides[0];
    distances[count] = distances[0];

    if (counts[kSideFront] == 0)
    {
        back.points = in.points;
        return;
    }
    if (counts[kSideBack] == 0)
    {
        front.points = in.points;
        return;
    }

    for (std::size_t i = 0; i < count; ++i)
    {
        const std::size_t next = (i + 1) % count;
        const int side = sides[i];
        const int next_side = sides[i + 1];
        const math::Vec3 &point = in.points[i];

        if (side == kSideOn)
        {
            front.points.push_back(point);
            back.points.push_back(point);
            continue;
        }

        if (side == kSideFront)
        {
            front.points.push_back(point);
        }
        else if (side == kSideBack)
        {
            back.points.push_back(point);
        }

        if (next_side == kSideOn || next_side == side)
        {
            continue;
        }

        const math::Vec3 &next_point = in.points[next];
        const math::Vec3 mid = InterpolateEdge(point, next_point, distances[i], distances[i + 1], normal, dist);
        front.points.push_back(mid);
        back.points.push_back(mid);
    }
}

void ChopWindingInPlace(std::optional<Winding> &winding, const math::Vec3 &normal, double dist, double epsilon)
{
    if (!winding.has_value() || winding->empty())
    {
        return;
    }

    Winding front;
    Winding back;
    ClipWindingEpsilon(*winding, normal, dist, epsilon, front, back);
    if (front.empty())
    {
        winding.reset();
        return;
    }
    winding = std::move(front);
}

std::optional<Winding> ChopWinding(std::optional<Winding> winding, const math::Vec3 &normal, double dist, double epsilon)
{
    ChopWindingInPlace(winding, normal, dist, epsilon);
    return winding;
}

double WindingArea(const Winding &winding)
{
    if (winding.size() < 3)
    {
        return 0.0;
    }

    double total = 0.0;
    for (std::size_t i = 2; i < winding.size(); ++i)
    {
        const math::Vec3 d1 = math::VectorSubtract(winding.points[i - 1], winding.points[0]);
        const math::Vec3 d2 = math::VectorSubtract(winding.points[i], winding.points[0]);
        const math::Vec3 cross = math::CrossProduct(d1, d2);
        total += 0.5 * math::VectorLength(cross);
    }
    return total;
}

void WindingBounds(const Winding &winding, math::Vec3 &mins, math::Vec3 &maxs)
{
    math::ClearBounds(mins, maxs);
    for (const auto &point : winding.points)
    {
        math::AddPointToBounds(point, mins, maxs);
    }
}

bool WindingIsTiny(const Winding &winding)
{
    if (winding.size() < 3)
    {
        return true;
    }

    int edges = 0;
    for (std::size_t i = 0; i < winding.size(); ++i)
    {
        const std::size_t next = (i + 1) % winding.size();
        const math::Vec3 delta = math::VectorSubtract(winding.points[next], winding.points[i]);
        const double length = math::VectorLength(delta);
        if (length > math::kEdgeLengthEpsilon)
        {
            if (++edges == 3)
            {
                return false;
            }
        }
    }
    return true;
}

bool WindingIsHuge(const Winding &winding)
{
    for (const auto &point : winding.points)
    {
        for (double component : point)
        {
            if (component < -math::kBogusRange + 1.0 || component > math::kBogusRange - 1.0)
            {
                return true;
            }
        }
    }
    return false;
}

bool WindingsNonConvex(const Winding &a, const Winding &b, const math::Vec3 &normal_a, const math::Vec3 &normal_b, double dist_a, double dist_b)
{
    if (a.empty() || b.empty())
    {
        return false;
    }

    for (const auto &point : a.points)
    {
        if (math::DotProduct(normal_b, point) - dist_b > math::kConvexEpsilon)
        {
            return true;
        }
    }
    for (const auto &point : b.points)
    {
        if (math::DotProduct(normal_a, point) - dist_a > math::kConvexEpsilon)
        {
            return true;
        }
    }
    return false;
}

} // namespace bspc::geometry

