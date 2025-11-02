#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "legacy_common.hpp"

namespace bspc::legacy
{

struct BrushBspStats
{
    std::size_t nodes = 0;
    std::size_t nonvis = 0;
    std::size_t active_brushes = 0;
    std::size_t solid_leaf_nodes = 0;
    std::size_t total_sides = 0;
    std::size_t brush_memory = 0;
    std::size_t peak_brush_memory = 0;
    std::size_t node_memory = 0;
    std::size_t peak_total_memory = 0;
};

BrushBspStats &GetBrushBspStats() noexcept;
void ResetBrushBSP() noexcept;

} // namespace bspc::legacy

