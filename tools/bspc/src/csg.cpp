#include "csg.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "legacy_common.hpp"

namespace bspc::legacy
{
namespace
{

std::vector<std::string> &MessageStorage()
{
    static std::vector<std::string> messages;
    return messages;
}

constexpr double kConvexTolerance = 0.01;

bool PlaneIndexValid(int planenum)
{
    return planenum >= 0 && planenum < static_cast<int>(MapPlanes().size());
}

void AppendMessage(const std::string &message)
{
    MessageStorage().push_back(message);
}

} // namespace

const std::vector<std::string> &GetBrushCheckMessages() noexcept
{
    return MessageStorage();
}

void ClearBrushCheckMessages()
{
    MessageStorage().clear();
}

void BoundBrush(BspBrush &brush)
{
    Vec3 mins{std::numeric_limits<double>::infinity(),
              std::numeric_limits<double>::infinity(),
              std::numeric_limits<double>::infinity()};
    Vec3 maxs{-std::numeric_limits<double>::infinity(),
              -std::numeric_limits<double>::infinity(),
              -std::numeric_limits<double>::infinity()};

    bool found_point = false;
    for (BrushSide &side : brush.sides)
    {
        if (!side.winding)
        {
            continue;
        }

        for (const Vec3 &point : side.winding->points)
        {
            found_point = true;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                mins[axis] = std::min(mins[axis], point[axis]);
                maxs[axis] = std::max(maxs[axis], point[axis]);
            }
        }
    }

    if (!found_point)
    {
        brush.mins = Vec3{};
        brush.maxs = Vec3{};
    }
    else
    {
        brush.mins = mins;
        brush.maxs = maxs;
    }
}

void CheckBSPBrush(BspBrush &brush)
{
    ClearBrushCheckMessages();

    if (brush.sides.empty())
    {
        AppendMessage("brush has no sides");
        throw std::runtime_error("brush has no sides");
    }

    for (const BrushSide &side : brush.sides)
    {
        if (!PlaneIndexValid(side.planenum))
        {
            AppendMessage("invalid plane index on brush");
            throw std::runtime_error("invalid plane index on brush");
        }
        if (!side.winding || side.winding->points.size() < 3)
        {
            AppendMessage("brush side missing winding");
            throw std::runtime_error("brush side missing winding");
        }
    }

    BoundBrush(brush);

    bool non_convex = false;
    auto &planes = MapPlanes();

    for (std::size_t i = 0; i < brush.sides.size() && !non_convex; ++i)
    {
        const BrushSide &side = brush.sides[i];
        const Plane &plane = planes[side.planenum];

        for (std::size_t j = 0; j < brush.sides.size(); ++j)
        {
            if (i == j)
            {
                continue;
            }

            const BrushSide &other_side = brush.sides[j];
            if (!other_side.winding)
            {
                continue;
            }

            for (const Vec3 &point : other_side.winding->points)
            {
                const double distance = DistanceToPlane(plane, point);
                if (distance > kConvexTolerance)
                {
                    non_convex = true;
                    break;
                }
            }

            if (non_convex)
            {
                break;
            }
        }
    }

    if (non_convex)
    {
        AppendMessage("non convex brush");
        throw std::runtime_error("non convex brush");
    }

    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        if (brush.mins[axis] < -kMaxMapBounds || brush.maxs[axis] > kMaxMapBounds)
        {
            AppendMessage("brush bounds out of range");
            throw std::runtime_error("brush bounds out of range");
        }

        if (brush.mins[axis] > kMaxMapBounds || brush.maxs[axis] < -kMaxMapBounds)
        {
            AppendMessage("brush has no visible sides");
            throw std::runtime_error("brush has no visible sides");
        }
    }
}

} // namespace bspc::legacy

