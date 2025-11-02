#include "plane.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace bspc::geometry
{

namespace
{

int DominantAxis(const math::Vec3 &normal) noexcept
{
    double max_value = -1.0;
    int axis = -1;
    for (int i = 0; i < 3; ++i)
    {
        const double value = std::abs(normal[i]);
        if (value > max_value)
        {
            max_value = value;
            axis = i;
        }
    }
    return axis;
}

} // namespace

int PlaneTypeForNormal(const math::Vec3 &normal) noexcept
{
    if (normal[0] == 1.0 || normal[0] == -1.0)
    {
        return 0;
    }
    if (normal[1] == 1.0 || normal[1] == -1.0)
    {
        return 1;
    }
    if (normal[2] == 1.0 || normal[2] == -1.0)
    {
        return 2;
    }

    const int axis = DominantAxis(normal);
    switch (axis)
    {
        case 0:
            return 3; // PLANE_ANYX
        case 1:
            return 4; // PLANE_ANYY
        default:
            return 5; // PLANE_ANYZ
    }
}

int PlaneSignBits(const math::Vec3 &normal) noexcept
{
    int bits = 0;
    for (int i = 2; i >= 0; --i)
    {
        bits <<= 1;
        bits |= (normal[i] < 0.0) ? 1 : 0;
    }
    return bits;
}

bool PlaneEqual(const Plane &plane, const math::Vec3 &normal, double dist, double normal_epsilon, double dist_epsilon) noexcept
{
    return std::abs(plane.normal[0] - normal[0]) < normal_epsilon &&
           std::abs(plane.normal[1] - normal[1]) < normal_epsilon &&
           std::abs(plane.normal[2] - normal[2]) < normal_epsilon &&
           std::abs(plane.dist - dist) < dist_epsilon;
}

Plane MakePlane(const math::Vec3 &normal, double dist) noexcept
{
    Plane plane{};
    plane.normal = normal;
    plane.dist = dist;
    plane.type = PlaneTypeForNormal(normal);
    plane.sign_bits = PlaneSignBits(normal);
    return plane;
}

Plane InvertPlane(const Plane &plane) noexcept
{
    Plane inverted = plane;
    inverted.normal = math::VectorScale(plane.normal, -1.0);
    inverted.dist = -plane.dist;
    inverted.sign_bits = PlaneSignBits(inverted.normal);
    return inverted;
}

double PlaneDistance(const Plane &plane, const math::Vec3 &point) noexcept
{
    return math::DistanceToPlane(point, plane.normal, plane.dist);
}

std::uint64_t HashPlane(const Plane &plane, double normal_epsilon, double dist_epsilon) noexcept
{
    const double nx = plane.normal[0];
    const double ny = plane.normal[1];
    const double nz = plane.normal[2];

    const std::int64_t qx = static_cast<std::int64_t>(std::llround(nx / normal_epsilon));
    const std::int64_t qy = static_cast<std::int64_t>(std::llround(ny / normal_epsilon));
    const std::int64_t qz = static_cast<std::int64_t>(std::llround(nz / normal_epsilon));
    const std::int64_t qd = static_cast<std::int64_t>(std::llround(plane.dist / dist_epsilon));

    std::uint64_t hash = 1469598103934665603ull; // FNV offset basis
    auto fold = [&hash](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };

    fold(static_cast<std::uint64_t>(qx));
    fold(static_cast<std::uint64_t>(qy));
    fold(static_cast<std::uint64_t>(qz));
    fold(static_cast<std::uint64_t>(qd));
    return hash;
}

} // namespace bspc::geometry

