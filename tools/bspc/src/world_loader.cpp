#include "bsp_builder.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "filesystem_helper.h"
#include "world_parsers.hpp"

namespace bspc::builder
{
namespace
{

std::uint32_t ReadU32LE(const char *data)
{
    const unsigned char b0 = static_cast<unsigned char>(data[0]);
    const unsigned char b1 = static_cast<unsigned char>(data[1]);
    const unsigned char b2 = static_cast<unsigned char>(data[2]);
    const unsigned char b3 = static_cast<unsigned char>(data[3]);
    return static_cast<std::uint32_t>(b0 | (b1 << 8) | (b2 << 16) | (b3 << 24));
}

struct ArchiveLookup
{
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
    bool found = false;
};

ArchiveLookup FindArchiveEntry(std::ifstream &stream,
                               bool is_quake,
                               std::uint32_t directory_offset,
                               std::uint32_t directory_length,
                               std::string_view normalized_name)
{
    const std::size_t entry_size = is_quake ? 64 : 128;
    const std::size_t name_length = is_quake ? 56 : 120;

    if (directory_length == 0 || directory_length % entry_size != 0)
    {
        return {};
    }

    std::vector<char> buffer(directory_length);
    stream.seekg(directory_offset, std::ios::beg);
    stream.read(buffer.data(), static_cast<std::streamsize>(directory_length));
    if (stream.gcount() != static_cast<std::streamsize>(directory_length))
    {
        return {};
    }

    const std::size_t count = directory_length / entry_size;
    for (std::size_t i = 0; i < count; ++i)
    {
        const char *entry_data = buffer.data() + (i * entry_size);
        std::string name(entry_data, entry_data + name_length);
        const auto null_pos = name.find('\0');
        if (null_pos != std::string::npos)
        {
            name.resize(null_pos);
        }

        std::string normalized = NormalizeSeparators(name);
        while (!normalized.empty() && (normalized.front() == '/' || normalized.front() == '\\'))
        {
            normalized.erase(normalized.begin());
        }
        if (normalized == normalized_name)
        {
            ArchiveLookup result;
            result.offset = ReadU32LE(entry_data + name_length);
            result.length = ReadU32LE(entry_data + name_length + 4);
            result.found = true;
            return result;
        }
    }

    return {};
}

std::optional<std::string> ReadFileFromArchive(const InputFile &input, std::string &error)
{
    std::ifstream stream(input.archive_path, std::ios::binary);
    if (!stream)
    {
        error = "failed to open archive " + input.archive_path.generic_string();
        return std::nullopt;
    }

    char header[12];
    stream.read(header, sizeof(header));
    if (stream.gcount() != static_cast<std::streamsize>(sizeof(header)))
    {
        error = "archive header truncated";
        return std::nullopt;
    }

    const bool is_quake = std::string_view(header, 4) == "PACK";
    const bool is_sin = std::string_view(header, 4) == "SPAK";
    if (!is_quake && !is_sin)
    {
        error = "unsupported archive format";
        return std::nullopt;
    }

    const std::uint32_t directory_offset = ReadU32LE(header + 4);
    const std::uint32_t directory_length = ReadU32LE(header + 8);

    const std::string normalized_entry = NormalizeSeparators(input.archive_entry);
    const ArchiveLookup lookup = FindArchiveEntry(stream, is_quake, directory_offset, directory_length, normalized_entry);
    if (!lookup.found)
    {
        error = "archive entry not found: " + normalized_entry;
        return std::nullopt;
    }

    if (lookup.length == 0)
    {
        return std::string();
    }

    std::string data;
    data.resize(static_cast<std::size_t>(lookup.length));
    stream.seekg(lookup.offset, std::ios::beg);
    stream.read(data.data(), static_cast<std::streamsize>(lookup.length));
    if (stream.gcount() != static_cast<std::streamsize>(lookup.length))
    {
        error = "failed to read archive entry payload";
        return std::nullopt;
    }

    return data;
}

std::optional<std::string> ReadWorldFile(const InputFile &input, std::string &error)
{
    if (input.from_archive)
    {
        return ReadFileFromArchive(input, error);
    }

    std::ifstream stream(input.path, std::ios::binary);
    if (!stream)
    {
        error = "failed to open " + input.path.generic_string();
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << stream.rdbuf();
    if (!stream.good() && !stream.eof())
    {
        error = "failed to read " + input.path.generic_string();
        return std::nullopt;
    }

    return buffer.str();
}

std::string NormalizeExtension(std::string extension)
{
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension;
}

ParsedWorld::Format DetectWorldFormat(const InputFile &input, std::string_view data)
{
    std::string extension = input.path.extension().string();
    if (extension.empty() && input.from_archive)
    {
        extension = std::filesystem::path(input.archive_entry).extension().string();
    }
    extension = NormalizeExtension(extension);
    if (extension == ".map")
    {
        return ParsedWorld::Format::kMap;
    }
    if (extension == ".bsp")
    {
        return ParsedWorld::Format::kBsp;
    }

    bool saw_brace = false;
    bool saw_binary = false;
    for (unsigned char ch : data)
    {
        if (ch == '{')
        {
            saw_brace = true;
            break;
        }
        if ((ch == 0) || (std::iscntrl(ch) && ch != '\n' && ch != '\r' && ch != '\t' && ch != '\f'))
        {
            saw_binary = true;
            break;
        }
    }

    if (saw_brace)
    {
        return ParsedWorld::Format::kMap;
    }

    if (data.size() >= sizeof(std::int32_t))
    {
        std::int32_t version = 0;
        std::memcpy(&version, data.data(), sizeof(version));
        if (version == formats::kQuake1BspVersion)
        {
            return ParsedWorld::Format::kBsp;
        }
    }

    if (saw_binary)
    {
        return ParsedWorld::Format::kBsp;
    }

    return ParsedWorld::Format::kUnknown;
}

} // namespace

bool LoadWorldState(const InputFile &input, ParsedWorld &out_world, std::string &error)
{
    const auto contents = ReadWorldFile(input, error);
    if (!contents)
    {
        return false;
    }

    const ParsedWorld::Format format = DetectWorldFormat(input, *contents);
    switch (format)
    {
    case ParsedWorld::Format::kMap:
        return PopulateWorldFromMapText(input.original, *contents, out_world, error);
    case ParsedWorld::Format::kBsp:
    {
        std::vector<std::byte> buffer(contents->size());
        if (!buffer.empty())
        {
            std::memcpy(buffer.data(), contents->data(), buffer.size());
        }
        formats::ConstByteSpan span;
        span.data = buffer.data();
        span.size = buffer.size();
        const bool parsed = PopulateWorldFromBspBinary(input.original, span, out_world, error);
        return parsed;
    }
    default:
        error = "unrecognized world format";
        return false;
    }
}

} // namespace bspc::builder

