#pragma once

#include <cstddef>
#include <cstdint>

#include "math.hpp"

namespace bspc::geometry
{

struct Plane
{
    math::Vec3 normal{};
    double dist = 0.0;
    int type = 0;
    int sign_bits = 0;
};

int PlaneTypeForNormal(const math::Vec3 &normal) noexcept;
int PlaneSignBits(const math::Vec3 &normal) noexcept;
bool PlaneEqual(const Plane &plane, const math::Vec3 &normal, double dist, double normal_epsilon = math::kNormalEpsilon, double dist_epsilon = math::kDistEpsilon) noexcept;

Plane MakePlane(const math::Vec3 &normal, double dist) noexcept;
Plane InvertPlane(const Plane &plane) noexcept;

double PlaneDistance(const Plane &plane, const math::Vec3 &point) noexcept;

std::uint64_t HashPlane(const Plane &plane, double normal_epsilon = math::kNormalEpsilon, double dist_epsilon = math::kDistEpsilon) noexcept;

} // namespace bspc::geometry

