#pragma once

#include <array>
#include <cstddef>

namespace bspc::math
{

using Vec3 = std::array<double, 3>;
using Mat3 = std::array<Vec3, 3>;

// The tolerances were recovered from the original BSPC executable. They govern
// floating point comparisons performed during plane snapping and winding
// clipping.
constexpr double kNormalEpsilon = 0.0001;
constexpr double kDistEpsilon = 0.02;
constexpr double kOnEpsilon = 0.1;
constexpr double kConvexEpsilon = 0.2;
constexpr double kEdgeLengthEpsilon = 0.2;
constexpr double kBogusRange = 65535.0 + 128.0;

enum AngleAxis
{
    kPitch = 0,
    kYaw = 1,
    kRoll = 2,
};

Vec3 MakeVec3(double x, double y, double z) noexcept;

void VectorClear(Vec3 &v) noexcept;
void VectorCopy(const Vec3 &in, Vec3 &out) noexcept;
Vec3 VectorAdd(const Vec3 &lhs, const Vec3 &rhs) noexcept;
Vec3 VectorSubtract(const Vec3 &lhs, const Vec3 &rhs) noexcept;
Vec3 VectorScale(const Vec3 &value, double scale) noexcept;
Vec3 VectorMA(const Vec3 &start, double scale, const Vec3 &direction) noexcept;
double DotProduct(const Vec3 &lhs, const Vec3 &rhs) noexcept;
Vec3 CrossProduct(const Vec3 &lhs, const Vec3 &rhs) noexcept;
double VectorLengthSquared(const Vec3 &value) noexcept;
double VectorLength(const Vec3 &value) noexcept;
double VectorNormalize(Vec3 &value) noexcept;
double VectorNormalize2(const Vec3 &in, Vec3 &out) noexcept;
bool VectorCompare(const Vec3 &lhs, const Vec3 &rhs, double epsilon = kNormalEpsilon) noexcept;
void VectorInverse(Vec3 &value) noexcept;

void ClearBounds(Vec3 &mins, Vec3 &maxs) noexcept;
void AddPointToBounds(const Vec3 &point, Vec3 &mins, Vec3 &maxs) noexcept;
double RadiusFromBounds(const Vec3 &mins, const Vec3 &maxs) noexcept;

double Q_rint(double value) noexcept;

void AngleVectors(const Vec3 &angles, Vec3 *forward, Vec3 *right, Vec3 *up) noexcept;
void AxisClear(Mat3 &axis) noexcept;
Mat3 IdentityMatrix() noexcept;
Mat3 ConcatRotations(const Mat3 &a, const Mat3 &b) noexcept;
Vec3 TransformVector(const Mat3 &matrix, const Vec3 &vector) noexcept;

double DistanceToPlane(const Vec3 &point, const Vec3 &normal, double dist) noexcept;
Vec3 ProjectPointOntoPlane(const Vec3 &point, const Vec3 &normal, double dist) noexcept;
Vec3 ProjectVectorOntoPlane(const Vec3 &vector, const Vec3 &normal) noexcept;
void SnapVector(Vec3 &normal) noexcept;
void SnapPlane(Vec3 &normal, double &dist) noexcept;

} // namespace bspc::math

