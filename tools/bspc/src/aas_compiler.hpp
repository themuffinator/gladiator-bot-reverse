#pragma once

#include <array>
#include <string>

#include "bsp_builder.hpp"
#include "bsp_formats.hpp"

namespace bspc::aas
{
struct CompilationResult
{
    formats::AasHeader header{};
    std::array<formats::OwnedLump, formats::kAasLumpCount> lumps{};
};

// Builds a placeholder AAS dataset mirroring the legacy pipeline sequencing.
bool BuildPlaceholderAas(const builder::ParsedWorld &world, CompilationResult &out, std::string &error);

// Converts the owned lump buffers into read-only views suitable for serialization.
std::array<formats::LumpView, formats::kAasLumpCount> MakeLumpViews(const CompilationResult &result) noexcept;
} // namespace bspc::aas
