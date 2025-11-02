#include "legacy_common.hpp"

#include <algorithm>
#include <cassert>

namespace bspc::legacy
{
namespace
{

std::vector<Plane> &GlobalPlaneStorage()
{
    static std::vector<Plane> planes;
    return planes;
}

} // namespace

double &Vec3::operator[](std::size_t index) noexcept
{
    assert(index < 3);
    if (index == 0)
    {
        return x;
    }
    if (index == 1)
    {
        return y;
    }
    return z;
}

double Vec3::operator[](std::size_t index) const noexcept
{
    assert(index < 3);
    if (index == 0)
    {
        return x;
    }
    if (index == 1)
    {
        return y;
    }
    return z;
}

Vec3 operator+(const Vec3 &lhs, const Vec3 &rhs) noexcept
{
    return Vec3{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

Vec3 operator-(const Vec3 &lhs, const Vec3 &rhs) noexcept
{
    return Vec3{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vec3 operator*(const Vec3 &value, double scalar) noexcept
{
    return Vec3{value.x * scalar, value.y * scalar, value.z * scalar};
}

double Dot(const Vec3 &lhs, const Vec3 &rhs) noexcept
{
    return (lhs.x * rhs.x) + (lhs.y * rhs.y) + (lhs.z * rhs.z);
}

std::vector<Plane> &MapPlanes()
{
    return GlobalPlaneStorage();
}

void ClearMapPlanes()
{
    GlobalPlaneStorage().clear();
}

int AppendPlane(const Plane &plane)
{
    auto &planes = GlobalPlaneStorage();
    planes.push_back(plane);
    return static_cast<int>(planes.size() - 1);
}

double DistanceToPlane(const Plane &plane, const Vec3 &point) noexcept
{
    return Dot(plane.normal, point) - plane.dist;
}

Vec3 ComputeCentroid(const Winding &winding)
{
    if (winding.points.empty())
    {
        return Vec3{};
    }

    Vec3 sum{};
    for (const Vec3 &point : winding.points)
    {
        sum = sum + point;
    }

    const double scale = 1.0 / static_cast<double>(winding.points.size());
    return sum * scale;
}

} // namespace bspc::legacy

