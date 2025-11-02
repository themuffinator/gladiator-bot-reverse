#include "bsp_to_map.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "filesystem_helper.h"
#include "logging.hpp"

namespace bspc::bsp_to_map
{
namespace
{

struct LumpRange
{
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
};

std::uint32_t ReadU32LE(const std::byte *data) noexcept
{
    const auto b0 = static_cast<std::uint32_t>(data[0]);
    const auto b1 = static_cast<std::uint32_t>(data[1]);
    const auto b2 = static_cast<std::uint32_t>(data[2]);
    const auto b3 = static_cast<std::uint32_t>(data[3]);
    return b0 | (b1 << 8U) | (b2 << 16U) | (b3 << 24U);
}

bool DetermineLumpDirectory(std::string_view ident,
                            std::uint32_t version,
                            std::size_t file_size,
                            LumpRange &entities,
                            std::string &error,
                            const std::byte *directory)
{
    std::size_t lump_count = 0;
    if (ident == "IBSP")
    {
        switch (version)
        {
        case 29:
            lump_count = 15;
            break;
        case 38:
            lump_count = 19;
            break;
        case 46:
            lump_count = 17;
            break;
        default:
            error = "unsupported IBSP version " + std::to_string(version);
            return false;
        }
    }
    else if (ident == "RBSP")
    {
        switch (version)
        {
        case 47:
            lump_count = 17;
            break;
        default:
            error = "unsupported RBSP version " + std::to_string(version);
            return false;
        }
    }
    else
    {
        error = "unsupported BSP ident '" + std::string(ident) + "'";
        return false;
    }

    if (lump_count == 0)
    {
        error = "BSP contains no lumps";
        return false;
    }

    const std::size_t directory_bytes = lump_count * sizeof(std::uint32_t) * 2;
    if (file_size < 8 + directory_bytes)
    {
        error = "BSP header truncated";
        return false;
    }

    const std::uint32_t offset = ReadU32LE(directory);
    const std::uint32_t length = ReadU32LE(directory + sizeof(std::uint32_t));

    if (static_cast<std::uint64_t>(offset) + static_cast<std::uint64_t>(length) > file_size)
    {
        error = "entities lump exceeds BSP bounds";
        return false;
    }

    entities.offset = offset;
    entities.length = length;
    return true;
}

bool ExtractEntities(const std::vector<std::byte> &data, std::string &entities_text, std::string &error)
{
    if (data.size() < 8)
    {
        error = "BSP is smaller than the header";
        return false;
    }

    char ident_chars[5] = {0, 0, 0, 0, 0};
    std::memcpy(ident_chars, data.data(), 4);
    const std::string ident(ident_chars, 4);

    const std::uint32_t version = ReadU32LE(data.data() + 4);

    LumpRange entities{};
    if (!DetermineLumpDirectory(ident, version, data.size(), entities, error, data.data() + 8))
    {
        return false;
    }

    if (entities.length == 0)
    {
        entities_text.clear();
        return true;
    }

    entities_text.assign(reinterpret_cast<const char *>(data.data() + entities.offset), entities.length);
    return true;
}

bool WriteTextFile(const std::filesystem::path &path, std::string_view contents)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream)
    {
        log::Warning("failed to write %s", path.generic_string().c_str());
        return false;
    }

    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!contents.empty() && contents.back() != '\n')
    {
        stream.put('\n');
    }

    if (!stream.good())
    {
        log::Warning("failed to write %s", path.generic_string().c_str());
        return false;
    }

    log::Info("wrote %s\n", path.generic_string().c_str());
    return true;
}

} // namespace

bool ConvertBspToMap(const InputFile &input, const std::filesystem::path &destination, std::string &error)
{
    std::vector<std::byte> buffer;
    if (!ReadFile(input, buffer, false))
    {
        error = "failed to read BSP input";
        return false;
    }

    std::string entities_text;
    if (!ExtractEntities(buffer, entities_text, error))
    {
        return false;
    }

    entities_text = NormalizeNewlines(std::move(entities_text));
    if (!WriteTextFile(destination, entities_text))
    {
        error = "failed to write MAP output";
        return false;
    }

    return true;
}

} // namespace bspc::bsp_to_map

