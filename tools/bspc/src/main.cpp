#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "filesystem_helper.h"
#include "logging.hpp"
#include "options.hpp"
#include "pipelines.hpp"
#include "threads.hpp"

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
    FileSystemResolver resolver;
    const bool queue_companions = EqualsIgnoreCase(extension, "map") || EqualsIgnoreCase(extension, "bsp");
    while (index + 1 < static_cast<size_t>(argc) && argv[index + 1][0] != '-')
    {
        std::string normalized = EnsureExtension(argv[index + 1], extension);
        auto resolved = resolver.ResolvePattern(normalized, extension, queue_companions);
        if (!resolved.empty())
        {
            files.insert(files.end(), resolved.begin(), resolved.end());
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
    else if (!input.from_archive)
    {
        base = input.path.parent_path();
    }
    else
    {
        base = std::filesystem::path(".");
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
    log::Write(kUsage);
}

void PrintUnknownParameter(const char *parameter)
{
    log::Warning("unknown parameter %s", parameter);
}

void PrintNoFilesFound()
{
    log::Warning("no files found");
}

void PrintFolderMissing(const std::string &path)
{
    log::Warning("the folder %s does not exist", path.c_str());
}

void PrintConversion(const char *format, const std::string &source, const std::string &destination)
{
    log::Info(format, source.c_str(), destination.c_str());
}

template <typename Callback>
void ProcessMapInput(const Options &options, const char *format, std::string_view extension, Callback &&callback)
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
        if (DetectFileType(file.path) != FileType::kMap)
        {
            log::Warning("%s is probably not a MAP file", file.original.c_str());
        }
        std::forward<Callback>(callback)(options, file, dest);
    }
}

template <typename Callback>
void ProcessBspInput(const Options &options, const char *format, std::string_view extension, Callback &&callback)
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
            log::Warning("%s is probably not a BSP file", file.original.c_str());
        }
        std::forward<Callback>(callback)(options, file, dest);
    }
}

} // namespace

int Main(int argc, char **argv)
{
    Options options;

    threads::Configure(options.threads);
    options.threads = threads::WorkerCount();

    log::Initialize();
    log::Info("BSPC version 1.2, May 20 1999 13:46:31 by Mr Elusive\n");

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
            threads::Configure(options.threads);
            options.threads = threads::WorkerCount();
            log::Info("threads = %d\n", options.threads);
        }
        else if (EqualsIgnoreCase(argument, "-noverbose"))
        {
            options.verbose = false;
            log::Info("verbose = false\n");
        }
        else if (EqualsIgnoreCase(argument, "-breathfirst") || EqualsIgnoreCase(argument, "-breath"))
        {
            options.breath_first = true;
            log::Info("breathfirst = true\n");
        }
        else if (EqualsIgnoreCase(argument, "-nobrushmerge"))
        {
            options.nobrushmerge = true;
            log::Info("nobrushmerge = true\n");
        }
        else if (EqualsIgnoreCase(argument, "-noliquids"))
        {
            options.noliquids = true;
            log::Info("noliquids = true\n");
        }
        else if (EqualsIgnoreCase(argument, "-freetree"))
        {
            options.freetree = true;
            log::Info("freetree = true\n");
        }
        else if (EqualsIgnoreCase(argument, "-nocsg"))
        {
            options.nocsg = true;
            log::Info("nocsg = true\n");
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
        log::Warning("don't know what to do");
        return 1;
    }

    switch (options.pipeline)
    {
    case Pipeline::kMapToBsp:
        ProcessMapInput(options, "map2bsp: %s to %s\n", ".bsp", pipelines::RunMapToBsp);
        break;
    case Pipeline::kMapToAas:
        ProcessMapInput(options, "map2aas: %s to %s\n", ".aas", pipelines::RunMapToAas);
        break;
    case Pipeline::kBspToMap:
        ProcessBspInput(options, "bsp2map: %s to %s\n", ".map", pipelines::RunBspToMap);
        break;
    case Pipeline::kBspToBsp:
        ProcessBspInput(options, "bsp2bsp: %s to %s\n", ".bsp", pipelines::RunBspToBsp);
        break;
    case Pipeline::kBspToAas:
        ProcessBspInput(options, "bsp2aas: %s to %s\n", ".aas", pipelines::RunBspToAas);
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

