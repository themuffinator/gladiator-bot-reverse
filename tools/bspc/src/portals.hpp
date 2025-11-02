#pragma once

#include <cstddef>

#include "legacy_common.hpp"

namespace bspc::legacy
{

struct PortalStats
{
    std::size_t active = 0;
    std::size_t peak = 0;
    std::size_t memory = 0;
};

PortalStats &GetPortalStats() noexcept;
Portal *AllocPortal();
void FreePortal(Portal *portal);
void AddPortalToNodes(Portal &portal, Node &front, Node &back);
void RemovePortalFromNode(Portal &portal, Node &node);

} // namespace bspc::legacy

