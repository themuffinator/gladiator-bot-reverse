#include "prtfile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <utility>

#include "logging.hpp"
#include "legacy_common.hpp"
#include "q_shared.h"
#include "portals.hpp"
#include "tree.hpp"

// The original BSPC sources use CONTENTS_Q2TRANSLUCENT to represent the translucent
// flag. The shared Quake headers exposed to the reconstructed tool only provide
// CONTENTS_TRANSLUCENT, so alias the constant for clarity when porting logic.
#ifndef CONTENTS_Q2TRANSLUCENT
#define CONTENTS_Q2TRANSLUCENT CONTENTS_TRANSLUCENT
#endif

namespace bspc::legacy
{
namespace
{

constexpr int kLastVisibleContents = 64;

int VisibleContents(int contents) noexcept
{
    for (int mask = 1; mask <= kLastVisibleContents; mask <<= 1)
    {
        if (contents & mask)
        {
            return mask;
        }
    }
    return 0;
}

int ClusterContents(const Node *node) noexcept
{
    if (!node)
    {
        return 0;
    }

    if (node->planenum == kPlanenumLeaf)
    {
        return node->contents;
    }

    const int c1 = ClusterContents(node->children[0]);
    const int c2 = ClusterContents(node->children[1]);
    int combined = c1 | c2;

    if (!(c1 & CONTENTS_SOLID) || !(c2 & CONTENTS_SOLID))
    {
        combined &= ~CONTENTS_SOLID;
    }

    return combined;
}

bool PortalVisFlood(const Portal &portal) noexcept
{
    if (!portal.onnode)
    {
        return false;
    }

    int c1 = ClusterContents(portal.nodes[0]);
    int c2 = ClusterContents(portal.nodes[1]);

    if (!VisibleContents(c1 ^ c2))
    {
        return true;
    }

    if (c1 & (CONTENTS_Q2TRANSLUCENT | CONTENTS_DETAIL))
    {
        c1 = 0;
    }
    if (c2 & (CONTENTS_Q2TRANSLUCENT | CONTENTS_DETAIL))
    {
        c2 = 0;
    }

    if ((c1 | c2) & CONTENTS_SOLID)
    {
        return false;
    }

    if (!(c1 ^ c2))
    {
        return true;
    }

    if (!VisibleContents(c1 ^ c2))
    {
        return true;
    }

    return false;
}

Vec3 Cross(const Vec3 &a, const Vec3 &b) noexcept
{
    return Vec3{(a.y * b.z) - (a.z * b.y),
                (a.z * b.x) - (a.x * b.z),
                (a.x * b.y) - (a.y * b.x)};
}

bool ComputeWindingPlane(const Winding &winding, Vec3 &out_normal, double &out_dist) noexcept
{
    if (winding.points.size() < 3)
    {
        return false;
    }

    const Vec3 &a = winding.points[0];
    const Vec3 &b = winding.points[1];
    const Vec3 &c = winding.points[2];
    Vec3 normal = Cross(b - a, c - a);
    const double length = std::sqrt(Dot(normal, normal));
    if (length == 0.0)
    {
        return false;
    }

    const double inv_length = 1.0 / length;
    normal = normal * inv_length;
    out_normal = normal;
    out_dist = Dot(normal, a);
    return true;
}

void FillLeafNumbers(Node &node, int cluster) noexcept
{
    if (node.planenum == kPlanenumLeaf)
    {
        if (node.contents & CONTENTS_SOLID)
        {
            node.cluster = -1;
        }
        else
        {
            node.cluster = cluster;
        }
        return;
    }

    node.cluster = cluster;
    if (node.children[0])
    {
        FillLeafNumbers(*node.children[0], cluster);
    }
    if (node.children[1])
    {
        FillLeafNumbers(*node.children[1], cluster);
    }
}

void NumberLeafs(Node &node, int &cluster_count, int &portal_count) noexcept
{
    if (node.planenum != kPlanenumLeaf && !node.detail_separator)
    {
        node.cluster = -99;
        if (node.children[0])
        {
            NumberLeafs(*node.children[0], cluster_count, portal_count);
        }
        if (node.children[1])
        {
            NumberLeafs(*node.children[1], cluster_count, portal_count);
        }
        return;
    }

    if (node.contents & CONTENTS_SOLID)
    {
        node.cluster = -1;
        return;
    }

    FillLeafNumbers(node, cluster_count);
    ++cluster_count;

    for (Portal *portal = node.portals; portal; )
    {
        const int side = (portal->nodes[1] == &node) ? 1 : 0;
        if (portal->nodes[0] == &node)
        {
            if (PortalVisFlood(*portal))
            {
                ++portal_count;
            }
        }
        portal = portal->next[side];
    }
}

std::string FormatFloat(double value)
{
    const double rounded = std::round(value);
    if (std::fabs(value - rounded) < 0.001)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(rounded));
        return std::string(buffer);
    }

    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%f", value);
    return std::string(buffer);
}

void WritePortalFileRecursive(Node &node, std::ostringstream &stream)
{
    if (node.planenum != kPlanenumLeaf && !node.detail_separator)
    {
        if (node.children[0])
        {
            WritePortalFileRecursive(*node.children[0], stream);
        }
        if (node.children[1])
        {
            WritePortalFileRecursive(*node.children[1], stream);
        }
        return;
    }

    if (node.contents & CONTENTS_SOLID)
    {
        return;
    }

    for (Portal *portal = node.portals; portal; )
    {
        const int side = (portal->nodes[1] == &node) ? 1 : 0;
        if (portal->winding && portal->nodes[0] == &node)
        {
            if (PortalVisFlood(*portal))
            {
                Vec3 winding_normal{};
                double winding_dist = 0.0;
                if (ComputeWindingPlane(*portal->winding, winding_normal, winding_dist))
                {
                    (void)winding_dist;
                    const double dot = Dot(portal->plane.normal, winding_normal);
                    int front_cluster = portal->nodes[0] ? portal->nodes[0]->cluster : -1;
                    int back_cluster = portal->nodes[1] ? portal->nodes[1]->cluster : -1;

                    if (dot < 0.99)
                    {
                        std::swap(front_cluster, back_cluster);
                    }

                    stream << portal->winding->points.size() << ' ' << front_cluster << ' ' << back_cluster << ' ';
                    for (const Vec3 &point : portal->winding->points)
                    {
                        stream << '(' << FormatFloat(point.x) << ' ' << FormatFloat(point.y) << ' ' << FormatFloat(point.z) << ") ";
                    }
                    stream << '\n';
                }
            }
        }
        portal = portal->next[side];
    }
}

} // namespace

PortalFileResult WritePortalFile(Tree &tree)
{
    PortalFileResult result;

    log::Info("--- WritePortalFile ---\n");

    int cluster_count = 0;
    int portal_count = 0;

    if (tree.headnode)
    {
        NumberLeafs(*tree.headnode, cluster_count, portal_count);
    }

    std::ostringstream stream;
    stream << "PRT1\n";
    stream << cluster_count << '\n';
    stream << portal_count << '\n';

    if (tree.headnode)
    {
        WritePortalFileRecursive(*tree.headnode, stream);
    }

    result.text = stream.str();
    result.metrics.cluster_count = static_cast<std::size_t>(std::max(cluster_count, 0));
    result.metrics.portal_count = static_cast<std::size_t>(std::max(portal_count, 0));

    log::Info("%5d visclusters\n", cluster_count);
    log::Info("%5d visportals\n", portal_count);

    return result;
}

} // namespace bspc::legacy

