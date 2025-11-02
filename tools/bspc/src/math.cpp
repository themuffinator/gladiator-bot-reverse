#include "math.hpp"

#include <algorithm>
#include <cmath>

namespace bspc::math
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
}

Vec3 MakeVec3(double x, double y, double z) noexcept
{
    return Vec3{x, y, z};
}

void VectorClear(Vec3 &v) noexcept
{
    v = Vec3{0.0, 0.0, 0.0};
}

void VectorCopy(const Vec3 &in, Vec3 &out) noexcept
{
    out = in;
}

Vec3 VectorAdd(const Vec3 &lhs, const Vec3 &rhs) noexcept
{
    return Vec3{lhs[0] + rhs[0], lhs[1] + rhs[1], lhs[2] + rhs[2]};
}

Vec3 VectorSubtract(const Vec3 &lhs, const Vec3 &rhs) noexcept
{
    return Vec3{lhs[0] - rhs[0], lhs[1] - rhs[1], lhs[2] - rhs[2]};
}

Vec3 VectorScale(const Vec3 &value, double scale) noexcept
{
    return Vec3{value[0] * scale, value[1] * scale, value[2] * scale};
}

Vec3 VectorMA(const Vec3 &start, double scale, const Vec3 &direction) noexcept
{
    return Vec3{start[0] + scale * direction[0],
                start[1] + scale * direction[1],
                start[2] + scale * direction[2]};
}

double DotProduct(const Vec3 &lhs, const Vec3 &rhs) noexcept
{
    return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
}

Vec3 CrossProduct(const Vec3 &lhs, const Vec3 &rhs) noexcept
{
    return Vec3{lhs[1] * rhs[2] - lhs[2] * rhs[1],
                lhs[2] * rhs[0] - lhs[0] * rhs[2],
                lhs[0] * rhs[1] - lhs[1] * rhs[0]};
}

double VectorLengthSquared(const Vec3 &value) noexcept
{
    return DotProduct(value, value);
}

double VectorLength(const Vec3 &value) noexcept
{
    return std::sqrt(VectorLengthSquared(value));
}

double VectorNormalize(Vec3 &value) noexcept
{
    const double length = VectorLength(value);
    if (length == 0.0)
    {
        VectorClear(value);
        return 0.0;
    }

    const double inv = 1.0 / length;
    value[0] *= inv;
    value[1] *= inv;
    value[2] *= inv;
    return length;
}

double VectorNormalize2(const Vec3 &in, Vec3 &out) noexcept
{
    out = in;
    return VectorNormalize(out);
}

bool VectorCompare(const Vec3 &lhs, const Vec3 &rhs, double epsilon) noexcept
{
    return std::abs(lhs[0] - rhs[0]) < epsilon &&
           std::abs(lhs[1] - rhs[1]) < epsilon &&
           std::abs(lhs[2] - rhs[2]) < epsilon;
}

void VectorInverse(Vec3 &value) noexcept
{
    value[0] = -value[0];
    value[1] = -value[1];
    value[2] = -value[2];
}

void ClearBounds(Vec3 &mins, Vec3 &maxs) noexcept
{
    mins = Vec3{99999.0, 99999.0, 99999.0};
    maxs = Vec3{-99999.0, -99999.0, -99999.0};
}

void AddPointToBounds(const Vec3 &point, Vec3 &mins, Vec3 &maxs) noexcept
{
    for (std::size_t i = 0; i < 3; ++i)
    {
        mins[i] = std::min(mins[i], point[i]);
        maxs[i] = std::max(maxs[i], point[i]);
    }
}

double RadiusFromBounds(const Vec3 &mins, const Vec3 &maxs) noexcept
{
    Vec3 corner{};
    for (std::size_t i = 0; i < 3; ++i)
    {
        const double a = std::abs(mins[i]);
        const double b = std::abs(maxs[i]);
        corner[i] = a > b ? a : b;
    }
    return VectorLength(corner);
}

double Q_rint(double value) noexcept
{
    return std::floor(value + 0.5);
}

void AngleVectors(const Vec3 &angles, Vec3 *forward, Vec3 *right, Vec3 *up) noexcept
{
    const double yaw = angles[kYaw] * (kPi * 2.0 / 360.0);
    const double sy = std::sin(yaw);
    const double cy = std::cos(yaw);

    const double pitch = angles[kPitch] * (kPi * 2.0 / 360.0);
    const double sp = std::sin(pitch);
    const double cp = std::cos(pitch);

    const double roll = angles[kRoll] * (kPi * 2.0 / 360.0);
    const double sr = std::sin(roll);
    const double cr = std::cos(roll);

    if (forward != nullptr)
    {
        *forward = Vec3{cp * cy, cp * sy, -sp};
    }
    if (right != nullptr)
    {
        *right = Vec3{(-sr * sp * cy) + (-cr * -sy),
                      (-sr * sp * sy) + (-cr * cy),
                      -sr * cp};
    }
    if (up != nullptr)
    {
        *up = Vec3{(cr * sp * cy) + (-sr * -sy),
                   (cr * sp * sy) + (-sr * cy),
                   cr * cp};
    }
}

void AxisClear(Mat3 &axis) noexcept
{
    axis = IdentityMatrix();
}

Mat3 IdentityMatrix() noexcept
{
    return Mat3{Vec3{1.0, 0.0, 0.0}, Vec3{0.0, 1.0, 0.0}, Vec3{0.0, 0.0, 1.0}};
}

Mat3 ConcatRotations(const Mat3 &a, const Mat3 &b) noexcept
{
    Mat3 result{};
    for (std::size_t i = 0; i < 3; ++i)
    {
        for (std::size_t j = 0; j < 3; ++j)
        {
            result[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
        }
    }
    return result;
}

Vec3 TransformVector(const Mat3 &matrix, const Vec3 &vector) noexcept
{
    return Vec3{matrix[0][0] * vector[0] + matrix[0][1] * vector[1] + matrix[0][2] * vector[2],
                matrix[1][0] * vector[0] + matrix[1][1] * vector[1] + matrix[1][2] * vector[2],
                matrix[2][0] * vector[0] + matrix[2][1] * vector[1] + matrix[2][2] * vector[2]};
}

double DistanceToPlane(const Vec3 &point, const Vec3 &normal, double dist) noexcept
{
    return DotProduct(point, normal) - dist;
}

Vec3 ProjectPointOntoPlane(const Vec3 &point, const Vec3 &normal, double dist) noexcept
{
    const double d = DistanceToPlane(point, normal, dist);
    return VectorMA(point, -d, normal);
}

Vec3 ProjectVectorOntoPlane(const Vec3 &vector, const Vec3 &normal) noexcept
{
    const double dot = DotProduct(vector, normal);
    return VectorMA(vector, -dot, normal);
}

void SnapVector(Vec3 &normal) noexcept
{
    for (std::size_t i = 0; i < 3; ++i)
    {
        if (std::abs(normal[i] - 1.0) < kNormalEpsilon)
        {
            VectorClear(normal);
            normal[i] = 1.0;
            return;
        }
        if (std::abs(normal[i] + 1.0) < kNormalEpsilon)
        {
            VectorClear(normal);
            normal[i] = -1.0;
            return;
        }
    }
}

void SnapPlane(Vec3 &normal, double &dist) noexcept
{
    SnapVector(normal);
    if (std::abs(dist - Q_rint(dist)) < kDistEpsilon)
    {
        dist = Q_rint(dist);
    }
}

} // namespace bspc::math

