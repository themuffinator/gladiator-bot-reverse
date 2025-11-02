#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bspc::legacy
{

constexpr double kClipEpsilon = 0.1;
constexpr double kMaxMapBounds = 65535.0;
constexpr int kPlanenumLeaf = -1;

struct Vec3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    double &operator[](std::size_t index) noexcept;
    double operator[](std::size_t index) const noexcept;
};

Vec3 operator+(const Vec3 &lhs, const Vec3 &rhs) noexcept;
Vec3 operator-(const Vec3 &lhs, const Vec3 &rhs) noexcept;
Vec3 operator*(const Vec3 &value, double scalar) noexcept;
double Dot(const Vec3 &lhs, const Vec3 &rhs) noexcept;

struct Winding
{
    std::vector<Vec3> points;
};

using WindingPtr = std::shared_ptr<Winding>;

struct Plane
{
    Vec3 normal{};
    double dist = 0.0;
    int type = 0;
    int sign_bits = 0;
};

struct BrushSide;
struct BspBrush;
struct Face;
struct Node;

struct BrushSide
{
    int planenum = -1;
    int texinfo = 0;
    WindingPtr winding;
    BrushSide *original = nullptr;
    int contents = 0;
    int surface_flags = 0;
    unsigned short flags = 0;
};

struct MapBrush
{
    int entitynum = 0;
    int brushnum = 0;
    int contents = 0;
    Vec3 mins{};
    Vec3 maxs{};
};

struct BspBrush
{
    std::vector<BrushSide> sides;
    MapBrush *original = nullptr;
    Vec3 mins{};
    Vec3 maxs{};
};

struct Face
{
};

struct Entity
{
    std::string classname;
    Vec3 origin{};
};

struct Portal
{
    Plane plane{};
    Node *onnode = nullptr;
    std::array<Node *, 2> nodes{nullptr, nullptr};
    std::array<Portal *, 2> next{nullptr, nullptr};
    WindingPtr winding;
    bool sidefound = false;
    BrushSide *side = nullptr;
    std::array<Face *, 2> face{nullptr, nullptr};
};

struct Node
{
    int planenum = kPlanenumLeaf;
    Node *parent = nullptr;
    Vec3 mins{};
    Vec3 maxs{};
    std::unique_ptr<BspBrush> volume;
    bool detail_separator = false;
    BrushSide *side = nullptr;
    std::array<std::unique_ptr<Node>, 2> owned_children{};
    std::array<Node *, 2> children{nullptr, nullptr};
    Face *faces = nullptr;
    BspBrush *brushlist = nullptr;
    int contents = 0;
    int occupied = 0;
    Entity *occupant = nullptr;
    int cluster = -1;
    int area = -1;
    Portal *portals = nullptr;
};

struct Tree
{
    std::unique_ptr<Node> headnode;
    Node outside_node{};
    Vec3 mins{};
    Vec3 maxs{};
    std::vector<std::unique_ptr<Node>> extra_nodes;
    std::vector<std::unique_ptr<Entity>> owned_entities;
};

std::vector<Plane> &MapPlanes();
void ClearMapPlanes();
int AppendPlane(const Plane &plane);
double DistanceToPlane(const Plane &plane, const Vec3 &point) noexcept;
Vec3 ComputeCentroid(const Winding &winding);

} // namespace bspc::legacy

