#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bspc
{

struct InputFile
{
    std::string original;
    std::filesystem::path path;
    bool from_archive = false;
    std::filesystem::path archive_path;
    std::string archive_entry;
    std::vector<std::filesystem::path> companions;
};

std::string NormalizeSeparators(std::string path);
bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs);
bool HasWildcards(std::string_view text);
bool WildcardMatch(std::string_view pattern, std::string_view text);

class FileSystemResolver
{
public:
    FileSystemResolver();

    std::vector<InputFile> ResolvePattern(const std::string &pattern,
                                          std::string_view required_extension,
                                          bool queue_companions) const;

private:
    struct ArchiveEntry
    {
        std::string name;
        std::string normalized_name;
        std::uint32_t offset = 0;
        std::uint32_t length = 0;
    };

    struct ArchiveDirectory
    {
        std::filesystem::path archive_path;
        std::vector<ArchiveEntry> entries;
    };

    struct ArchivePattern
    {
        std::string archive_path;
        std::string inner_pattern;
    };

    static std::optional<ArchivePattern> SplitArchivePattern(const std::string &pattern);
    static std::optional<ArchiveDirectory> ReadArchiveDirectory(const std::filesystem::path &archive_path);
    static std::vector<std::filesystem::path> BuildCompanions(const std::filesystem::path &path);
    static bool ExtensionMatches(const std::filesystem::path &path, std::string_view required_extension);

    std::vector<std::filesystem::path> ExpandFileSystemPattern(const std::string &pattern) const;
    std::vector<InputFile> ExpandArchivePattern(const std::string &original_pattern,
                                                const ArchivePattern &pattern,
                                                std::string_view required_extension,
                                                bool queue_companions) const;
};

} // namespace bspc

