#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "brush.hpp"

namespace bspc::geometry
{

class BrushWorkspace
{
public:
    void Reset(bool nocsg, bool nobrushmerge) noexcept;

    std::size_t AddBrush(Brush brush);
    std::size_t BrushCount() const noexcept { return brushes_.size(); }
    const Brush &GetBrush(std::size_t index) const;
    Brush &MutableBrush(std::size_t index);

    bool IsConvex(std::size_t index) const;
    std::optional<std::pair<std::size_t, std::size_t>> SplitBrush(std::size_t index, const Plane &plane, double epsilon = 0.0);

private:
    bool nocsg_ = false;
    bool nobrushmerge_ = false;
    std::vector<Brush> brushes_;
    std::vector<std::uint64_t> brush_hashes_;
};

} // namespace bspc::geometry

