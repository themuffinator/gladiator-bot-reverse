#pragma once

#include <string>
#include <vector>

#include "legacy_common.hpp"

namespace bspc::legacy
{

const std::vector<std::string> &GetBrushCheckMessages() noexcept;
void ClearBrushCheckMessages();

void BoundBrush(BspBrush &brush);
void CheckBSPBrush(BspBrush &brush);

} // namespace bspc::legacy

