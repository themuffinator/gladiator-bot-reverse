#include "leakfile.hpp"

#include <iomanip>
#include <sstream>
#include <vector>

#include "legacy_common.hpp"

namespace bspc::legacy
{
namespace
{

Vec3 PortalCenter(const Portal &portal)
{
    if (portal.winding && !portal.winding->points.empty())
    {
        return ComputeCentroid(*portal.winding);
    }
    return Vec3{};
}

} // namespace

std::string LeakFile(const Tree &tree, std::string_view source_name)
{
    (void)source_name;
    if (!tree.outside_node.occupied)
    {
        return {};
    }

    const Node *node = &tree.outside_node;
    std::vector<Vec3> points;

    while (node->occupied > 1)
    {
        int next_value = node->occupied;
        const Portal *best_portal = nullptr;
        const Node *best_node = nullptr;

        for (Portal *portal = node->portals; portal; portal = portal->next[(portal->nodes[1] == node) ? 1 : 0])
        {
            const int side = (portal->nodes[0] == node) ? 1 : 0;
            Node *target = portal->nodes[side];
            if (!target || !target->occupied)
            {
                continue;
            }
            if (target->occupied < next_value)
            {
                next_value = target->occupied;
                best_portal = portal;
                best_node = target;
            }
        }

        if (!best_portal || !best_node)
        {
            break;
        }

        points.push_back(PortalCenter(*best_portal));
        node = best_node;
    }

    if (node->occupant)
    {
        points.push_back(node->occupant->origin);
    }

    if (points.empty())
    {
        return {};
    }

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6);
    for (const Vec3 &point : points)
    {
        stream << point.x << ' ' << point.y << ' ' << point.z << '\n';
    }

    return stream.str();
}

} // namespace bspc::legacy

