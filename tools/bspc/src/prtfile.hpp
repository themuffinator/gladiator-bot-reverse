#pragma once

#include <cstddef>
#include <string>

namespace bspc::legacy
{

struct Tree;

struct PortalFileMetrics
{
    std::size_t cluster_count = 0;
    std::size_t portal_count = 0;
};

struct PortalFileResult
{
    std::string text;
    PortalFileMetrics metrics{};
};

PortalFileResult WritePortalFile(Tree &tree);

} // namespace bspc::legacy

