#include "aas_compiler.hpp"

#include <cstdint>
#include <cstring>

namespace bspc::aas
{
namespace
{
struct PlaceholderBBox
{
    std::int32_t presencetype = 0;
    std::int32_t flags = 0;
    float mins[3]{0.0f, 0.0f, 0.0f};
    float maxs[3]{0.0f, 0.0f, 0.0f};
};

void WriteData(formats::OwnedLump &lump, const void *data, std::size_t size)
{
    lump.Reset();
    if (size == 0 || data == nullptr)
    {
        return;
    }

    lump.Allocate(size, false);
    std::memcpy(lump.data.get(), data, size);
}
} // namespace

bool BuildPlaceholderAas(const builder::ParsedWorld &world, CompilationResult &out, std::string &error)
{
    (void)world;
    error.clear();

    out.header = {};
    out.header.ident = formats::kAasIdent;
    out.header.version = formats::kAasVersion;

    // Seed the bounding box table with the default Gladiator player hull so the
    // runtime can still load the generated file while the remaining generation
    // routines are ported.
    constexpr PlaceholderBBox kDefaultBBox{
        .presencetype = 1,
        .flags = 0,
        .mins = {-16.0f, -16.0f, -24.0f},
        .maxs = {16.0f, 16.0f, 32.0f},
    };
    WriteData(out.lumps[static_cast<std::size_t>(formats::AasLump::kBBoxes)], &kDefaultBBox, sizeof(kDefaultBBox));

    // The remaining lumps are emitted as empty buffers for now. They will be
    // populated by follow-up ports of the reachability and clustering stages.
    for (auto index = 0U; index < out.lumps.size(); ++index)
    {
        if (index == static_cast<std::size_t>(formats::AasLump::kBBoxes))
        {
            continue;
        }
        out.lumps[index].Reset();
    }

    return true;
}

std::array<formats::LumpView, formats::kAasLumpCount> MakeLumpViews(const CompilationResult &result) noexcept
{
    std::array<formats::LumpView, formats::kAasLumpCount> views{};
    for (std::size_t i = 0; i < result.lumps.size(); ++i)
    {
        const auto &owned = result.lumps[i];
        views[i] = formats::LumpView{static_cast<const std::byte *>(owned.data.get()), owned.size};
    }
    return views;
}
} // namespace bspc::aas
