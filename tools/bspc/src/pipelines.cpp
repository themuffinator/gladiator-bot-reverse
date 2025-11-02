#include "pipelines.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>
#include <vector>

#include "bsp_builder.hpp"
#include "bsp_formats.hpp"
#include "logging.hpp"
#include "map_parser.hpp"
#include "memory.h"
#include "options.hpp"
#include "filesystem_helper.h"

namespace bspc::pipelines
{
namespace
{

class ScopedTimer
{
public:
    ScopedTimer() noexcept
        : start_(std::chrono::steady_clock::now()),
          finished_(false)
    {
    }

    ScopedTimer(const ScopedTimer &) = delete;
    ScopedTimer &operator=(const ScopedTimer &) = delete;

    ~ScopedTimer()
    {
        Finish();
    }

    void Finish()
    {
        if (finished_)
        {
            return;
        }

        const auto end = std::chrono::steady_clock::now();
        const double elapsed_seconds = std::chrono::duration<double>(end - start_).count();
        log::Info("%5.0f seconds elapsed\n", elapsed_seconds);
        finished_ = true;
    }

private:
    std::chrono::steady_clock::time_point start_;
    bool finished_;
};

std::filesystem::path ReplaceExtension(const std::filesystem::path &path, std::string_view extension)
{
    std::filesystem::path result = path;
    result.replace_extension(extension);
    return result;
}

void RemoveFileIfExists(const std::filesystem::path &path)
{
    if (path.empty())
    {
        return;
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec && ec != std::errc::no_such_file_or_directory)
    {
        log::Warning("failed to remove %s (%s)", path.generic_string().c_str(), ec.message().c_str());
    }
}

void RemoveLegacyCompanions(const std::filesystem::path &bsp_destination)
{
    RemoveFileIfExists(ReplaceExtension(bsp_destination, ".prt"));
    RemoveFileIfExists(ReplaceExtension(bsp_destination, ".lin"));
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

void TouchEmptyFile(const std::filesystem::path &path)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream)
    {
        log::Warning("failed to write %s", path.generic_string().c_str());
    }
}

void EmitAasPlaceholder(const InputFile &input, const std::filesystem::path &destination)
{
    const std::string text = "placeholder AAS generated from " + input.original + "\n";
    WriteTextFile(destination, text);
}

// sub_4081e0 (map2bsp/bsp2bsp) in the legacy binary invoked the following high level
// sequence:
//  1. preprocess brushes and purge stale portal/line outputs
//  2. build the BSP tree and write the BSP file
//  3. emit the .prt/.lin diagnostics and report the elapsed time
// We mirror that structure here with placeholder operations to keep the
// reconstructed tool modular.
bool WriteBinaryFile(const std::filesystem::path &path, const std::vector<std::byte> &data)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream)
    {
        log::Warning("failed to write %s", path.generic_string().c_str());
        return false;
    }

    if (!data.empty())
    {
        stream.write(reinterpret_cast<const char *>(data.data()),
                     static_cast<std::streamsize>(data.size()));
    }

    if (!stream.good())
    {
        log::Warning("failed to write %s", path.generic_string().c_str());
        return false;
    }

    log::Info("wrote %s\n", path.generic_string().c_str());
    return true;
}

bool WriteBspFile(const std::filesystem::path &destination,
                  const builder::ParsedWorld &world,
                  const builder::BspBuildArtifacts &artifacts)
{
    std::vector<std::byte> output;
    std::string error;
    bool serialized = false;

    switch (world.bsp_type)
    {
    case builder::ParsedWorld::BspType::kQuake2:
    {
        const auto views = builder::MakeQuake2LumpViews(artifacts);
        serialized = formats::SerializeQuake2Bsp(views, output, error);
        break;
    }
    case builder::ParsedWorld::BspType::kQuake1:
    default:
    {
        const auto views = builder::MakeQuake1LumpViews(artifacts);
        serialized = formats::SerializeQuake1Bsp(views, output, error);
        break;
    }
    }

    if (!serialized)
    {
        log::Warning("failed to serialize BSP: %s", error.c_str());
        return false;
    }

    return WriteBinaryFile(destination, output);
}

void RunBspCompilation(const Options &options, const InputFile &input, const std::filesystem::path &destination)
{
    if (EqualsIgnoreCase(input.path.extension().generic_string(), ".map"))
    {
        auto parsed_map = map::ParseMapFromFile(input, options);
        if (parsed_map)
        {
            log::Info("parsed %zu entities, %zu brushes, %zu materials\n",
                      parsed_map->summary.entities,
                      parsed_map->summary.brushes,
                      parsed_map->summary.unique_materials);
        }
    }

    RemoveLegacyCompanions(destination);

    builder::ParsedWorld world;
    std::string error;
    if (!builder::LoadWorldState(input, world, error))
    {
        log::Warning("failed to load world %s (%s)", input.original.c_str(), error.c_str());
        TouchEmptyFile(destination);
        TouchEmptyFile(ReplaceExtension(destination, ".prt"));
        TouchEmptyFile(ReplaceExtension(destination, ".lin"));
        return;
    }

    builder::BspBuildArtifacts artifacts;
    if (!builder::BuildBspTree(world, artifacts))
    {
        log::Warning("failed to build BSP tree for %s", input.original.c_str());
        TouchEmptyFile(destination);
        TouchEmptyFile(ReplaceExtension(destination, ".prt"));
        TouchEmptyFile(ReplaceExtension(destination, ".lin"));
        return;
    }

    if (WriteBspFile(destination, world, artifacts))
    {
        log::Info("BSP build wrote %s (%zu clusters, %zu portals)\n",
                  destination.generic_string().c_str(),
                  artifacts.portal_cluster_count,
                  artifacts.portal_count);
    }

    WriteTextFile(ReplaceExtension(destination, ".prt"), artifacts.portal_text);
    WriteTextFile(ReplaceExtension(destination, ".lin"), artifacts.leak_text);

    if (options.freetree)
    {
        builder::FreeTree(artifacts);
        log::Info("freed BSP tree\n");
    }
}

// sub_408370 (map2aas/bsp2aas) performed the BSP load followed by temporary AAS
// arena initialisation, reachability generation, file write, and shutdown. The
// reconstruction currently stubs those steps but preserves the sequencing hooks.
void RunAasCompilation(const InputFile &input, const std::filesystem::path &destination)
{
    memory::InitTmpAasArena();
    EmitAasPlaceholder(input, destination);
    memory::ShutdownTmpAasArena();
}

} // namespace

void RunMapToBsp(const Options &options, const InputFile &input, const std::string &destination_path)
{
    const std::filesystem::path bsp_destination(destination_path);
    ScopedTimer timer;
    RunBspCompilation(options, input, bsp_destination);
    timer.Finish();
}

void RunBspToBsp(const Options &options, const InputFile &input, const std::string &destination_path)
{
    const std::filesystem::path bsp_destination(destination_path);
    ScopedTimer timer;
    RunBspCompilation(options, input, bsp_destination);
    timer.Finish();
}

void RunMapToAas(const Options &options, const InputFile &input, const std::string &destination_path)
{
    const std::filesystem::path aas_destination(destination_path);
    std::filesystem::path bsp_destination = aas_destination;
    bsp_destination.replace_extension(".bsp");

    ScopedTimer timer;
    RunBspCompilation(options, input, bsp_destination);
    RunAasCompilation(input, aas_destination);
    timer.Finish();
}

void RunBspToAas(const Options &options, const InputFile &input, const std::string &destination_path)
{
    const std::filesystem::path aas_destination(destination_path);
    ScopedTimer timer;
    RunAasCompilation(input, aas_destination);
    timer.Finish();
}

} // namespace bspc::pipelines

