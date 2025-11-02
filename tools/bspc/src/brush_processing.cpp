#include "brush_processing.hpp"

#include <stdexcept>

namespace bspc::geometry
{

void BrushWorkspace::Reset(bool nocsg, bool nobrushmerge) noexcept
{
    nocsg_ = nocsg;
    nobrushmerge_ = nobrushmerge;
    brushes_.clear();
    brush_hashes_.clear();
}

std::size_t BrushWorkspace::AddBrush(Brush brush)
{
    CreateBrushWindings(brush);
    const std::uint64_t hash = HashBrush(brush);
    brush_hashes_.push_back(hash);
    brushes_.push_back(std::move(brush));
    return brushes_.size() - 1;
}

const Brush &BrushWorkspace::GetBrush(std::size_t index) const
{
    if (index >= brushes_.size())
    {
        throw std::out_of_range("brush index out of range");
    }
    return brushes_[index];
}

Brush &BrushWorkspace::MutableBrush(std::size_t index)
{
    if (index >= brushes_.size())
    {
        throw std::out_of_range("brush index out of range");
    }
    return brushes_[index];
}

bool BrushWorkspace::IsConvex(std::size_t index) const
{
    const Brush &brush = GetBrush(index);
    return BrushIsConvex(brush);
}

std::optional<std::pair<std::size_t, std::size_t>> BrushWorkspace::SplitBrush(std::size_t index, const Plane &plane, double epsilon)
{
    if (nocsg_)
    {
        return std::nullopt;
    }

    Brush &brush = MutableBrush(index);
    auto split = SplitBrush(brush, plane, epsilon);
    if (!split.has_value())
    {
        return std::nullopt;
    }

    const auto [front, back] = std::move(*split);
    std::optional<std::size_t> front_index;
    std::optional<std::size_t> back_index;

    if (!front.sides.empty())
    {
        front_index = AddBrush(front);
    }
    if (!back.sides.empty())
    {
        back_index = AddBrush(back);
    }

    if (!front_index.has_value() && !back_index.has_value())
    {
        return std::nullopt;
    }

    return std::pair{front_index.value_or(index), back_index.value_or(index)};
}

} // namespace bspc::geometry

