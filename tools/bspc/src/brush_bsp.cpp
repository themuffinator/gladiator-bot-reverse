#include "brush_bsp.hpp"

#include <algorithm>
#include <limits>

namespace bspc::legacy
{
namespace
{

BrushBspStats g_stats{};

} // namespace

BrushBspStats &GetBrushBspStats() noexcept
{
    return g_stats;
}

void ResetBrushBSP() noexcept
{
    g_stats = BrushBspStats{};
}

} // namespace bspc::legacy

