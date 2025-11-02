#include "bsp_formats.hpp"

#include <limits>
#include <string>
#include <string_view>

namespace bspc::formats
{
namespace
{
constexpr std::size_t kLumpAlignment = 4;

std::size_t AlignSize(std::size_t value) noexcept
{
    const std::size_t remainder = value % kLumpAlignment;
    if (remainder == 0)
    {
        return value;
    }
    const std::size_t padding = kLumpAlignment - remainder;
    if (value > std::numeric_limits<std::size_t>::max() - padding)
    {
        return std::numeric_limits<std::size_t>::max();
    }
    return value + padding;
}

template <std::size_t LumpCount>
bool PopulateViews(ConstByteSpan file_data,
                   const std::array<LumpRange, LumpCount> &ranges,
                   std::array<LumpView, LumpCount> &views,
                   std::string_view format_name,
                   std::string &error)
{
    if (file_data.size == 0)
    {
        error = std::string(format_name) + " BSP data is empty";
        return false;
    }

    if (file_data.data == nullptr)
    {
        error = std::string(format_name) + " BSP buffer is null";
        return false;
    }

    const auto file_size = file_data.size;
    for (std::size_t i = 0; i < LumpCount; ++i)
    {
        const LumpRange &range = ranges[i];
        if (!range.FitsIn(file_size))
        {
            error = std::string(format_name) + " lump " + std::to_string(i) + " exceeds file bounds";
            return false;
        }

        if (range.length == 0)
        {
            views[i] = {};
            continue;
        }

        views[i].data = file_data.data + range.offset;
        views[i].size = static_cast<std::size_t>(range.length);
    }
    return true;
}

template <std::size_t LumpCount>
bool SerializeDirectory(const std::array<LumpView, LumpCount> &lumps,
                        std::size_t header_size,
                        std::vector<std::byte> &output,
                        std::array<LumpRange, LumpCount> &out_ranges,
                        std::string_view format_name,
                        std::string &error)
{
    output.clear();
    output.resize(header_size);
    std::size_t cursor = header_size;

    for (std::size_t i = 0; i < LumpCount; ++i)
    {
        const auto &view = lumps[i];
        if (view.size > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        {
            error = std::string(format_name) + " lump " + std::to_string(i) + " exceeds 32-bit length";
            return false;
        }

        const auto max_offset = static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());
        if (cursor > max_offset)
        {
            error = std::string(format_name) + " BSP exceeds 32-bit address space";
            return false;
        }

        if (view.size > 0 && cursor > max_offset - view.size)
        {
            error = std::string(format_name) + " lump " + std::to_string(i) + " exceeds 32-bit address space";
            return false;
        }

        out_ranges[i].offset = static_cast<std::int32_t>(cursor);
        out_ranges[i].length = static_cast<std::int32_t>(view.size);
        if (!view.empty())
        {
            output.insert(output.end(), view.data, view.data + view.size);
            cursor += view.size;
        }

        const std::size_t aligned = AlignSize(cursor);
        if (aligned > cursor)
        {
            const std::size_t padding = aligned - cursor;
            if (aligned > max_offset)
            {
                error = std::string(format_name) + " BSP exceeds 32-bit address space";
                return false;
            }
            output.insert(output.end(), padding, std::byte{0});
            cursor = aligned;
        }
    }

    return true;
}

} // namespace

bool DeserializeHalfLifeBsp(ConstByteSpan file_data, HalfLifeBspView &out, std::string &error)
{
    if (file_data.size < sizeof(HalfLifeBspHeader))
    {
        error = "Half-Life BSP is smaller than the header";
        return false;
    }

    if (file_data.data == nullptr)
    {
        error = "Half-Life BSP buffer is null";
        return false;
    }

    std::memcpy(&out.header, file_data.data, sizeof(HalfLifeBspHeader));
    if (out.header.version != kHalfLifeBspVersion)
    {
        error = "Unsupported Half-Life BSP version: " + std::to_string(out.header.version);
        return false;
    }

    return PopulateViews(file_data, out.header.lumps, out.lumps, "Half-Life", error);
}

bool DeserializeQuake1Bsp(ConstByteSpan file_data, Quake1BspView &out, std::string &error)
{
    if (file_data.size < sizeof(Quake1BspHeader))
    {
        error = "Quake 1 BSP is smaller than the header";
        return false;
    }

    if (file_data.data == nullptr)
    {
        error = "Quake 1 BSP buffer is null";
        return false;
    }

    std::memcpy(&out.header, file_data.data, sizeof(Quake1BspHeader));
    if (out.header.version != kQuake1BspVersion)
    {
        error = "Unsupported Quake 1 BSP version: " + std::to_string(out.header.version);
        return false;
    }

    return PopulateViews(file_data, out.header.lumps, out.lumps, "Quake 1", error);
}

bool DeserializeQuake2Bsp(ConstByteSpan file_data, Quake2BspView &out, std::string &error)
{
    if (file_data.size < sizeof(Quake2BspHeader))
    {
        error = "Quake 2 BSP is smaller than the header";
        return false;
    }

    if (file_data.data == nullptr)
    {
        error = "Quake 2 BSP buffer is null";
        return false;
    }

    std::memcpy(&out.header, file_data.data, sizeof(Quake2BspHeader));
    if (out.header.ident != kQuake2BspIdent)
    {
        error = "Quake 2 BSP ident mismatch";
        return false;
    }
    if (out.header.version != kQuake2BspVersion)
    {
        error = "Unsupported Quake 2 BSP version: " + std::to_string(out.header.version);
        return false;
    }

    return PopulateViews(file_data, out.header.lumps, out.lumps, "Quake 2", error);
}

bool DeserializeAas(ConstByteSpan file_data, AasView &out, std::string &error)
{
    if (file_data.size < sizeof(AasHeader))
    {
        error = "AAS file is smaller than the header";
        return false;
    }

    if (file_data.data == nullptr)
    {
        error = "AAS buffer is null";
        return false;
    }

    std::memcpy(&out.header, file_data.data, sizeof(AasHeader));
    if (out.header.ident != kAasIdent)
    {
        error = "AAS ident mismatch";
        return false;
    }

    if (out.header.version != kAasVersion)
    {
        error = "Unsupported AAS version: " + std::to_string(out.header.version);
        return false;
    }

    return PopulateViews(file_data, out.header.lumps, out.lumps, "AAS", error);
}

bool SerializeHalfLifeBsp(const std::array<LumpView, kHalfLifeLumpCount> &lumps,
                          std::vector<std::byte> &output,
                          std::string &error)
{
    HalfLifeBspHeader header{};
    header.version = kHalfLifeBspVersion;

    if (!SerializeDirectory(lumps, sizeof(HalfLifeBspHeader), output, header.lumps, "Half-Life", error))
    {
        return false;
    }

    std::memcpy(output.data(), &header, sizeof(HalfLifeBspHeader));
    return true;
}

bool SerializeQuake1Bsp(const std::array<LumpView, kQuake1LumpCount> &lumps,
                        std::vector<std::byte> &output,
                        std::string &error)
{
    Quake1BspHeader header{};
    header.version = kQuake1BspVersion;

    if (!SerializeDirectory(lumps, sizeof(Quake1BspHeader), output, header.lumps, "Quake 1", error))
    {
        return false;
    }

    std::memcpy(output.data(), &header, sizeof(Quake1BspHeader));
    return true;
}

bool SerializeQuake2Bsp(const std::array<LumpView, kQuake2LumpCount> &lumps,
                        std::vector<std::byte> &output,
                        std::string &error)
{
    Quake2BspHeader header{};
    header.ident = kQuake2BspIdent;
    header.version = kQuake2BspVersion;

    if (!SerializeDirectory(lumps, sizeof(Quake2BspHeader), output, header.lumps, "Quake 2", error))
    {
        return false;
    }

    std::memcpy(output.data(), &header, sizeof(Quake2BspHeader));
    return true;
}

bool SerializeAas(const AasHeader &header_template,
                  const std::array<LumpView, kAasLumpCount> &lumps,
                  std::vector<std::byte> &output,
                  std::string &error)
{
    if (header_template.ident != kAasIdent)
    {
        error = "AAS ident mismatch";
        return false;
    }
    if (header_template.version != kAasVersion)
    {
        error = "AAS version mismatch";
        return false;
    }

    AasHeader header = header_template;
    if (!SerializeDirectory(lumps, sizeof(AasHeader), output, header.lumps, "AAS", error))
    {
        return false;
    }

    std::memcpy(output.data(), &header, sizeof(AasHeader));
    return true;
}

} // namespace bspc::formats

