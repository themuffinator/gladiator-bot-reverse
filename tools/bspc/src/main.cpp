#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace bspc
{
namespace
{

constexpr std::string_view kUsage =
    "Usage:   bspc [-<switch> [-<switch> ...]]\n"
    "Example 1: bspc -bsp2aas znt2dm?.bsp -output e:\\quake2\\gladiator\n"
    "Example 2: bspc -bsp2aas pak1.pak\\maps/q2dm*.bsp -output e:\\quake2\\gladiator\n\n"
    "Switches:\n"
    "   map2bsp <[pakfilefilter/]filefilter> = convert MAP to BSP\n"
    "   map2aas <[pakfilefilter/]filefilter> = convert MAP to AAS\n"
    "   bsp2map <[pakfilefilter/]filefilter> = convert BSP to MAP\n"
    "   bsp2bsp <[pakfilefilter/]filefilter> = convert BSP to BSP\n"
    "   bsp2aas <[pakfilefilter/]filefilter> = convert BSP to AAS\n"
    "   output <output path>                = set output path\n"
    "   noverbose                           = disable verbose output\n"
    "   threads                             = number of threads to use\n"
    "   breath                              = breath first bsp building\n"
    "   nobrushmerge                        = don't merge brushes\n"
    "   noliquids                           = don't write liquids to map\n"
    "   freetree                            = free the bsp tree\n"
    "   nocsg                               = disables brush chopping\n\n";

enum class Pipeline
{
    kNone = 0,
    kMapToBsp = 1,
    kMapToAas = 2,
    kBspToMap = 3,
    kBspToBsp = 4,
    kBspToAas = 5,
};

enum class FileType
{
    kUnknown,
    kMap,
    kBsp,
};

struct InputFile
{
    std::string original;
    std::filesystem::path path;
};

struct Options
{
    bool verbose = true;
    bool breath_first = false;
    bool nobrushmerge = false;
    bool noliquids = false;
    bool freetree = false;
    bool nocsg = false;
    int threads = 1;
    std::string output_path;
    Pipeline pipeline = Pipeline::kNone;
    std::vector<InputFile> files;
    bool parse_ok = true;
};

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
        unsigned char a = static_cast<unsigned char>(lhs[i]);
        unsigned char b = static_cast<unsigned char>(rhs[i]);
        if (std::tolower(a) != std::tolower(b))
        {
            return false;
        }
    }
    return true;
}

FileType DetectFileType(const std::filesystem::path &path)
{
    auto ext = path.extension().string();
    if (ext.empty())
    {
        return FileType::kUnknown;
    }
    std::string lower;
    lower.resize(ext.size());
    std::transform(ext.begin(), ext.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (lower == ".map")
    {
        return FileType::kMap;
    }
    if (lower == ".bsp")
    {
        return FileType::kBsp;
    }
    return FileType::kUnknown;
}

bool HasWildcards(std::string_view text)
{
    return text.find_first_of("*?") != std::string_view::npos;
}

bool WildcardMatchImpl(std::string_view pattern, std::string_view text, size_t pi, size_t ti)
{
    while (pi < pattern.size())
    {
        char pc = static_cast<char>(std::tolower(static_cast<unsigned char>(pattern[pi])));
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
        char tc = static_cast<char>(std::tolower(static_cast<unsigned char>(text[ti])));
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

bool WildcardMatch(std::string_view pattern, std::string_view text)
{
    return WildcardMatchImpl(pattern, text, 0, 0);
}

std::vector<InputFile> ExpandPattern(const std::string &pattern)
{
    std::vector<InputFile> files;
    const std::string normalized = NormalizeSeparators(pattern);
    if (!HasWildcards(normalized))
    {
        InputFile file{pattern, std::filesystem::path(normalized)};
        files.push_back(file);
        return files;
    }

    const auto separator_pos = normalized.find_last_of('/');
    std::filesystem::path directory;
    std::string mask;
    if (separator_pos == std::string::npos)
    {
        directory = std::filesystem::path(".");
        mask = normalized;
    }
    else
    {
        directory = std::filesystem::path(normalized.substr(0, separator_pos));
        mask = normalized.substr(separator_pos + 1);
        if (directory.empty())
        {
            directory = std::filesystem::path(".");
        }
    }

    std::error_code ec;
    std::filesystem::directory_iterator it(directory, ec);
    if (ec)
    {
        return files;
    }

    for (const auto &entry : it)
    {
        const std::string candidate = entry.path().filename().string();
        if (WildcardMatch(mask, candidate))
        {
            std::filesystem::path expanded = directory / candidate;
            InputFile file{expanded.generic_string(), expanded};
            files.push_back(std::move(file));
        }
    }

    return files;
}

std::string EnsureExtension(const std::string &pattern, std::string_view desired_ext)
{
    if (desired_ext.empty())
    {
        return pattern;
    }

    std::string result = NormalizeSeparators(pattern);
    const auto separator_pos = result.find_last_of('/');
    const auto dot_pos = result.find_last_of('.');
    const bool dot_after_sep = (dot_pos != std::string::npos) &&
                               (separator_pos == std::string::npos || dot_pos > separator_pos);
    if (!dot_after_sep)
    {
        result.push_back('.');
        result.append(desired_ext);
        return result;
    }

    result.erase(dot_pos + 1);
    result.append(desired_ext);
    return result;
}

std::vector<InputFile> CollectArgumentFiles(int argc, char **argv, size_t &index, std::string_view extension)
{
    std::vector<InputFile> files;
    while (index + 1 < static_cast<size_t>(argc) && argv[index + 1][0] != '-')
    {
        std::string normalized = EnsureExtension(argv[index + 1], extension);
        auto expanded = ExpandPattern(normalized);
        if (!expanded.empty())
        {
            files.insert(files.end(), expanded.begin(), expanded.end());
        }
        ++index;
    }
    return files;
}

std::string ComputeDestination(const Options &options, const InputFile &input, std::string_view extension)
{
    std::filesystem::path base;
    if (!options.output_path.empty())
    {
        base = std::filesystem::path(options.output_path);
    }
    else
    {
        base = input.path.parent_path();
    }

    if (!base.empty())
    {
        std::error_code create_ec;
        if (!std::filesystem::exists(base))
        {
            std::filesystem::create_directories(base, create_ec);
        }
    }

    std::filesystem::path destination = base;
    if (!input.path.stem().empty())
    {
        if (!destination.empty())
        {
            destination /= input.path.stem();
        }
        else
        {
            destination = input.path.stem();
        }
    }
    else
    {
        if (!destination.empty())
        {
            destination /= input.path.filename();
        }
        else
        {
            destination = input.path.filename();
        }
    }

    destination.replace_extension(extension);
    return destination.generic_string();
}

void PrintUsage()
{
    std::fputs(kUsage.data(), stdout);
}

void PrintUnknownParameter(const char *parameter)
{
    std::printf("unknows parameter %s\n", parameter);
}

void PrintNoFilesFound()
{
    std::printf("no files found\n");
}

void PrintFolderMissing(const std::string &path)
{
    std::printf("the folder %s does not exist\n", path.c_str());
}

void PrintConversion(const char *format, const std::string &source, const std::string &destination)
{
    std::printf(format, source.c_str(), destination.c_str());
}

void ProcessMapInput(const Options &options, const char *format)
{
    if (options.files.empty())
    {
        PrintNoFilesFound();
        return;
    }

    for (const auto &file : options.files)
    {
        const std::string dest = ComputeDestination(options, file, ".bsp");
        PrintConversion(format, file.original, dest);
        if (DetectFileType(file.path) != FileType::kMap)
        {
            std::printf("%s is probably not a MAP file\n", file.original.c_str());
        }
    }
}

void ProcessMapToAas(const Options &options)
{
    if (options.files.empty())
    {
        PrintNoFilesFound();
        return;
    }

    for (const auto &file : options.files)
    {
        const std::string dest = ComputeDestination(options, file, ".aas");
        PrintConversion("map2aas: %s to %s\n", file.original, dest);
        if (DetectFileType(file.path) != FileType::kMap)
        {
            std::printf("%s is probably not a MAP file\n", file.original.c_str());
        }
    }
}

void ProcessBspInput(const Options &options, const char *format, std::string_view extension)
{
    if (options.files.empty())
    {
        PrintNoFilesFound();
        return;
    }

    for (const auto &file : options.files)
    {
        const std::string dest = ComputeDestination(options, file, extension);
        PrintConversion(format, file.original, dest);
        if (DetectFileType(file.path) != FileType::kBsp)
        {
            std::printf("%s is probably not a BSP file\n", file.original.c_str());
        }
    }
}

} // namespace

int Main(int argc, char **argv)
{
    Options options;

    std::printf("BSPC version 1.2, May 20 1999 13:46:31 by Mr Elusive\n");

    if (argc <= 1)
    {
        PrintUsage();
        return 1;
    }

    size_t index = 1;
    while (index < static_cast<size_t>(argc))
    {
        const std::string_view argument = argv[index];
        if (EqualsIgnoreCase(argument, "-threads"))
        {
            if (index + 1 >= static_cast<size_t>(argc))
            {
                options.parse_ok = false;
                break;
            }
            const char *value = argv[++index];
            options.threads = std::atoi(value);
            std::printf("threads = %d\n", options.threads);
        }
        else if (EqualsIgnoreCase(argument, "-noverbose"))
        {
            options.verbose = false;
            std::printf("verbose = false\n");
        }
        else if (EqualsIgnoreCase(argument, "-breathfirst") || EqualsIgnoreCase(argument, "-breath"))
        {
            options.breath_first = true;
            std::printf("breathfirst = true\n");
        }
        else if (EqualsIgnoreCase(argument, "-nobrushmerge"))
        {
            options.nobrushmerge = true;
            std::printf("nobrushmerge = true\n");
        }
        else if (EqualsIgnoreCase(argument, "-noliquids"))
        {
            options.noliquids = true;
            std::printf("noliquids = true\n");
        }
        else if (EqualsIgnoreCase(argument, "-freetree"))
        {
            options.freetree = true;
            std::printf("freetree = true\n");
        }
        else if (EqualsIgnoreCase(argument, "-nocsg"))
        {
            options.nocsg = true;
            std::printf("nocsg = true\n");
        }
        else if (EqualsIgnoreCase(argument, "-output"))
        {
            if (index + 1 >= static_cast<size_t>(argc))
            {
                options.parse_ok = false;
                break;
            }
            const char *path = argv[++index];
            options.output_path = NormalizeSeparators(path);
            std::error_code exists_ec;
            if (!options.output_path.empty() && !std::filesystem::exists(options.output_path, exists_ec))
            {
                PrintFolderMissing(options.output_path);
            }
        }
        else if (EqualsIgnoreCase(argument, "-map2bsp"))
        {
            if (index + 1 >= static_cast<size_t>(argc))
            {
                options.parse_ok = false;
                break;
            }
            options.pipeline = Pipeline::kMapToBsp;
            options.files = CollectArgumentFiles(argc, argv, index, "map");
        }
        else if (EqualsIgnoreCase(argument, "-map2aas"))
        {
            if (index + 1 >= static_cast<size_t>(argc))
            {
                options.parse_ok = false;
                break;
            }
            options.pipeline = Pipeline::kMapToAas;
            options.files = CollectArgumentFiles(argc, argv, index, "map");
        }
        else if (EqualsIgnoreCase(argument, "-bsp2map"))
        {
            if (index + 1 >= static_cast<size_t>(argc))
            {
                options.parse_ok = false;
                break;
            }
            options.pipeline = Pipeline::kBspToMap;
            options.files = CollectArgumentFiles(argc, argv, index, "bsp");
        }
        else if (EqualsIgnoreCase(argument, "-bsp2bsp"))
        {
            if (index + 1 >= static_cast<size_t>(argc))
            {
                options.parse_ok = false;
                break;
            }
            options.pipeline = Pipeline::kBspToBsp;
            options.files = CollectArgumentFiles(argc, argv, index, "bsp");
        }
        else if (EqualsIgnoreCase(argument, "-bsp2aas"))
        {
            if (index + 1 >= static_cast<size_t>(argc))
            {
                options.parse_ok = false;
                break;
            }
            options.pipeline = Pipeline::kBspToAas;
            options.files = CollectArgumentFiles(argc, argv, index, "bsp");
        }
        else if (!argument.empty() && argument[0] == '-')
        {
            PrintUnknownParameter(argv[index]);
            options.parse_ok = false;
            break;
        }
        else
        {
            PrintUnknownParameter(argv[index]);
            options.parse_ok = false;
            break;
        }
        ++index;
    }

    if (!options.parse_ok)
    {
        PrintUsage();
        return 1;
    }

    if (options.pipeline == Pipeline::kNone)
    {
        std::printf("don't know what to do\n");
        return 1;
    }

    switch (options.pipeline)
    {
    case Pipeline::kMapToBsp:
        ProcessMapInput(options, "map2bsp: %s to %s\n");
        break;
    case Pipeline::kMapToAas:
        ProcessMapToAas(options);
        break;
    case Pipeline::kBspToMap:
        ProcessBspInput(options, "bsp2map: %s to %s\n", ".map");
        break;
    case Pipeline::kBspToBsp:
        ProcessBspInput(options, "bsp2bsp: %s to %s\n", ".bsp");
        break;
    case Pipeline::kBspToAas:
        ProcessBspInput(options, "bsp2aas: %s to %s\n", ".aas");
        break;
    default:
        break;
    }

    return 0;
}

} // namespace bspc

int main(int argc, char **argv)
{
    return bspc::Main(argc, argv);
}

