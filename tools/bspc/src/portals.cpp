#include "portals.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

namespace bspc::legacy
{
namespace
{

std::vector<std::unique_ptr<Portal>> &PortalStorage()
{
    static std::vector<std::unique_ptr<Portal>> portals;
    return portals;
}

PortalStats g_stats{};

void UnlinkFromNode(Portal &portal, Node &node)
{
    Portal **link = &node.portals;
    while (*link)
    {
        if (*link == &portal)
        {
            *link = portal.next[(portal.nodes[1] == &node) ? 1 : 0];
            break;
        }
        Portal *current = *link;
        const int side = (current->nodes[1] == &node) ? 1 : 0;
        link = &current->next[side];
    }
}

} // namespace

PortalStats &GetPortalStats() noexcept
{
    return g_stats;
}

Portal *AllocPortal()
{
    auto portal = std::make_unique<Portal>();
    Portal *result = portal.get();
    PortalStorage().push_back(std::move(portal));

    ++g_stats.active;
    g_stats.peak = std::max(g_stats.peak, g_stats.active);
    g_stats.memory = PortalStorage().size() * sizeof(Portal);

    return result;
}

void FreePortal(Portal *portal)
{
    if (!portal)
    {
        return;
    }

    if (portal->nodes[0])
    {
        UnlinkFromNode(*portal, *portal->nodes[0]);
    }
    if (portal->nodes[1])
    {
        UnlinkFromNode(*portal, *portal->nodes[1]);
    }

    auto &storage = PortalStorage();
    auto it = std::find_if(storage.begin(), storage.end(), [&](const auto &entry) {
        return entry.get() == portal;
    });

    if (it != storage.end())
    {
        storage.erase(it);
        if (g_stats.active > 0)
        {
            --g_stats.active;
        }
        g_stats.memory = storage.size() * sizeof(Portal);
    }
}

void AddPortalToNodes(Portal &portal, Node &front, Node &back)
{
    if (portal.nodes[0] || portal.nodes[1])
    {
        throw std::runtime_error("portal already linked to nodes");
    }

    portal.nodes[0] = &front;
    portal.nodes[1] = &back;

    portal.next[0] = front.portals;
    front.portals = &portal;

    portal.next[1] = back.portals;
    back.portals = &portal;
}

void RemovePortalFromNode(Portal &portal, Node &node)
{
    UnlinkFromNode(portal, node);
    if (portal.nodes[0] == &node)
    {
        portal.nodes[0] = nullptr;
    }
    if (portal.nodes[1] == &node)
    {
        portal.nodes[1] = nullptr;
    }
}

} // namespace bspc::legacy

