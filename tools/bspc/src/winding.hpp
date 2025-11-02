#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "math.hpp"
#include "plane.hpp"

namespace bspc::geometry
{

struct Winding
{
    std::vector<math::Vec3> points;

    [[nodiscard]] bool empty() const noexcept { return points.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return points.size(); }
    [[nodiscard]] math::Vec3 &operator[](std::size_t index) noexcept { return points[index]; }
    [[nodiscard]] const math::Vec3 &operator[](std::size_t index) const noexcept { return points[index]; }
};

Winding BaseWindingForPlane(const math::Vec3 &normal, double dist);

void ClipWindingEpsilon(const Winding &in, const math::Vec3 &normal, double dist, double epsilon, Winding &front, Winding &back);
void ChopWindingInPlace(std::optional<Winding> &winding, const math::Vec3 &normal, double dist, double epsilon);
std::optional<Winding> ChopWinding(std::optional<Winding> winding, const math::Vec3 &normal, double dist, double epsilon);

double WindingArea(const Winding &winding);
void WindingBounds(const Winding &winding, math::Vec3 &mins, math::Vec3 &maxs);
bool WindingIsTiny(const Winding &winding);
bool WindingIsHuge(const Winding &winding);
bool WindingsNonConvex(const Winding &a, const Winding &b, const math::Vec3 &normal_a, const math::Vec3 &normal_b, double dist_a, double dist_b);

} // namespace bspc::geometry

