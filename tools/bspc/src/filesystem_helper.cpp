#include "filesystem_helper.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <functional>
#include <set>

namespace bspc
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

bool SegmentHasWildcards(std::string_view text)
{
    return text.find_first_of("*?") != std::string_view::npos;
}

std::vector<std::string> SplitSegments(const std::string &path)
{
    std::vector<std::string> segments;
    std::string current;
    for (char ch : path)
    {
        if (ch == '/')
        {
            segments.push_back(current);
            current.clear();
        }
        else
        {
            current.push_back(ch);
        }
    }

    segments.push_back(current);
    return segments;
}

} // namespace

FileSystemResolver::FileSystemResolver() = default;

std::string NormalizeSeparators(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }

    for (size_t i = 0; i < lhs.size(); ++i)
    {
        const unsigned char a = static_cast<unsigned char>(lhs[i]);
        const unsigned char b = static_cast<unsigned char>(rhs[i]);
        if (std::tolower(a) != std::tolower(b))
        {
            return false;
        }
    }

    return true;
}

bool HasWildcards(std::string_view text)
{
    return text.find_first_of("*?") != std::string_view::npos;
}

namespace
{

bool WildcardMatchImpl(std::string_view pattern, std::string_view text, size_t pi, size_t ti)
{
    while (pi < pattern.size())
    {
        const char pc = static_cast<char>(std::tolower(static_cast<unsigned char>(pattern[pi])));
        if (pc == '*')
        {
            while (pi < pattern.size() && pattern[pi] == '*')
            {
                ++pi;
            }

            if (pi == pattern.size())
            {
                return true;
            }

            while (ti < text.size())
            {
                if (WildcardMatchImpl(pattern, text, pi, ti))
                {
                    return true;
                }
                ++ti;
            }
            return false;
        }

        if (ti >= text.size())
        {
            return false;
        }

        const char tc = static_cast<char>(std::tolower(static_cast<unsigned char>(text[ti])));
        if (pc == '?')
        {
            ++pi;
            ++ti;
            continue;
        }

        if (pc != tc)
        {
            return false;
        }

        ++pi;
        ++ti;
    }

    return ti == text.size();
}

} // namespace

bool WildcardMatch(std::string_view pattern, std::string_view text)
{
    return WildcardMatchImpl(pattern, text, 0, 0);
}

bool FileSystemResolver::ExtensionMatches(const std::filesystem::path &path,
                                          std::string_view required_extension)
{
    if (required_extension.empty())
    {
        return true;
    }

    std::string expected;
    expected.reserve(required_extension.size() + 1);
    expected.push_back('.');
    expected.append(required_extension.begin(), required_extension.end());
    return EqualsIgnoreCase(path.extension().string(), expected);
}

std::vector<std::filesystem::path> FileSystemResolver::BuildCompanions(const std::filesystem::path &path)
{
    std::vector<std::filesystem::path> companions;
    std::filesystem::path prt = path;
    prt.replace_extension(".prt");
    companions.push_back(prt);

    std::filesystem::path lin = path;
    lin.replace_extension(".lin");
    companions.push_back(lin);
    return companions;
}

std::optional<FileSystemResolver::ArchivePattern> FileSystemResolver::SplitArchivePattern(const std::string &pattern)
{
    const std::string normalized = NormalizeSeparators(pattern);
    std::string lower = normalized;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    const std::vector<std::pair<std::string, std::size_t>> extensions = {
        {".pak", 4},
        {".sin", 4},
    };

    for (const auto &ext : extensions)
    {
        std::size_t pos = lower.find(ext.first);
        if (pos == std::string::npos)
        {
            continue;
        }

        const std::size_t end = pos + ext.second;
        if (end < lower.size())
        {
            const char next = lower[end];
            if (next != '/' && next != '\\')
            {
                continue;
            }
        }

        ArchivePattern result;
        result.archive_path = normalized.substr(0, end);
        if (end < normalized.size())
        {
            result.inner_pattern = normalized.substr(end + 1);
        }
        return result;
    }

    return std::nullopt;
}

std::optional<FileSystemResolver::ArchiveDirectory> FileSystemResolver::ReadArchiveDirectory(
    const std::filesystem::path &archive_path)
{
    std::ifstream file(archive_path, std::ios::binary);
    if (!file)
    {
        return std::nullopt;
    }

    char header[12];
    file.read(header, sizeof(header));
    if (file.gcount() != static_cast<std::streamsize>(sizeof(header)))
    {
        return std::nullopt;
    }

    const std::string magic(header, header + 4);
    const bool is_quake = magic == "PACK";
    const bool is_sin = magic == "SPAK";
    if (!is_quake && !is_sin)
    {
        return std::nullopt;
    }

    const std::uint32_t directory_offset = ReadU32LE(header + 4);
    const std::uint32_t directory_length = ReadU32LE(header + 8);

    if (directory_length == 0)
    {
        ArchiveDirectory directory;
        directory.archive_path = archive_path;
        return directory;
    }

    const std::size_t entry_size = is_quake ? 64 : 128;
    const std::size_t name_length = is_quake ? 56 : 120;
    if (directory_length % entry_size != 0)
    {
        return std::nullopt;
    }

    std::vector<char> buffer(directory_length);
    file.seekg(directory_offset, std::ios::beg);
    file.read(buffer.data(), directory_length);
    if (file.gcount() != static_cast<std::streamsize>(directory_length))
    {
        return std::nullopt;
    }

    const std::size_t count = directory_length / entry_size;
    ArchiveDirectory directory;
    directory.archive_path = archive_path;
    directory.entries.reserve(count);

    for (std::size_t i = 0; i < count; ++i)
    {
        const char *entry_data = buffer.data() + (i * entry_size);
        std::string name(entry_data, entry_data + name_length);
        const auto null_pos = name.find('\0');
        if (null_pos != std::string::npos)
        {
            name.resize(null_pos);
        }

        std::string normalized_name = NormalizeSeparators(name);
        while (!normalized_name.empty() && (normalized_name.front() == '/' || normalized_name.front() == '\\'))
        {
            normalized_name.erase(normalized_name.begin());
        }

        ArchiveEntry entry;
        entry.name = name;
        entry.normalized_name = std::move(normalized_name);
        entry.offset = ReadU32LE(entry_data + name_length);
        entry.length = ReadU32LE(entry_data + name_length + 4);
        directory.entries.push_back(std::move(entry));
    }

    return directory;
}

std::vector<std::filesystem::path> FileSystemResolver::ExpandFileSystemPattern(const std::string &pattern) const
{
    const std::string normalized = NormalizeSeparators(pattern);
    if (!HasWildcards(normalized))
    {
        return {std::filesystem::path(normalized)};
    }

    std::vector<std::filesystem::path> results;
    const auto segments = SplitSegments(normalized);

    std::filesystem::path root;
    std::size_t start_index = 0;
    if (!segments.empty() && segments[0].empty())
    {
        root = std::filesystem::path("/");
        start_index = 1;
    }
    else if (!segments.empty() && segments[0].size() == 2 && segments[0][1] == ':')
    {
        root = std::filesystem::path(segments[0] + "/");
        start_index = 1;
    }
    else
    {
        root = std::filesystem::path(".");
    }

    std::function<void(const std::filesystem::path &, std::size_t)> traverse;
    traverse = [&](const std::filesystem::path &base, std::size_t index) {
        if (index >= segments.size())
        {
            results.push_back(base);
            return;
        }

        const std::string &segment = segments[index];
        if (segment.empty())
        {
            traverse(base, index + 1);
            return;
        }

        if (!SegmentHasWildcards(segment))
        {
            std::filesystem::path next = base;
            next /= segment;
            traverse(next, index + 1);
            return;
        }

        std::filesystem::path directory = base;
        if (directory.empty())
        {
            directory = std::filesystem::path(".");
        }

        std::error_code ec;
        std::filesystem::directory_iterator iter(directory, ec);
        if (ec)
        {
            return;
        }

        for (const auto &entry : iter)
        {
            const std::string candidate = entry.path().filename().string();
            if (WildcardMatch(segment, candidate))
            {
                traverse(entry.path(), index + 1);
            }
        }
    };

    traverse(root, start_index);
    return results;
}

std::vector<InputFile> FileSystemResolver::ExpandArchivePattern(const std::string &original_pattern,
                                                                const ArchivePattern &pattern,
                                                                std::string_view required_extension,
                                                                bool queue_companions) const
{
    std::vector<InputFile> results;

    const auto archives = ExpandFileSystemPattern(pattern.archive_path);
    for (const auto &archive_path : archives)
    {
        const auto directory = ReadArchiveDirectory(archive_path);
        if (!directory)
        {
            continue;
        }

        for (const auto &entry : directory->entries)
        {
            const std::string normalized_entry = NormalizeSeparators(entry.normalized_name);
            if (!pattern.inner_pattern.empty())
            {
                if (!WildcardMatch(pattern.inner_pattern, normalized_entry))
                {
                    continue;
                }
            }

            std::filesystem::path pseudo_path = archive_path;
            pseudo_path /= normalized_entry;

            if (!ExtensionMatches(pseudo_path, required_extension))
            {
                continue;
            }

            InputFile file;
            file.original = pseudo_path.generic_string();
            file.path = std::move(pseudo_path);
            file.from_archive = true;
            file.archive_path = archive_path;
            file.archive_entry = normalized_entry;
            if (queue_companions)
            {
                file.companions = BuildCompanions(file.path);
            }

            results.push_back(std::move(file));
        }
    }

    return results;
}

std::vector<InputFile> FileSystemResolver::ResolvePattern(const std::string &pattern,
                                                          std::string_view required_extension,
                                                          bool queue_companions) const
{
    std::vector<InputFile> results;
    const std::string normalized = NormalizeSeparators(pattern);

    if (const auto archive_pattern = SplitArchivePattern(normalized))
    {
        auto archive_results = ExpandArchivePattern(normalized, *archive_pattern, required_extension, queue_companions);
        if (!archive_results.empty())
        {
            std::set<std::string> seen;
            for (auto &file : archive_results)
            {
                if (seen.insert(file.original).second)
                {
                    results.push_back(std::move(file));
                }
            }
            return results;
        }
    }

    if (!HasWildcards(normalized))
    {
        std::filesystem::path path(normalized);
        if (ExtensionMatches(path, required_extension))
        {
            InputFile file;
            file.original = normalized;
            file.path = std::move(path);
            if (queue_companions)
            {
                file.companions = BuildCompanions(file.path);
            }
            results.push_back(std::move(file));
        }
        return results;
    }

    const auto expanded = ExpandFileSystemPattern(normalized);
    std::set<std::string> seen;
    for (const auto &candidate : expanded)
    {
        if (!ExtensionMatches(candidate, required_extension))
        {
            continue;
        }

        const std::string key = candidate.generic_string();
        if (!seen.insert(key).second)
        {
            continue;
        }

        InputFile file;
        file.original = key;
        file.path = candidate;
        if (queue_companions)
        {
            file.companions = BuildCompanions(file.path);
        }
        results.push_back(std::move(file));
    }

    return results;
}

} // namespace bspc

