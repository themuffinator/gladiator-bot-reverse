#include "bsp_builder.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numeric>
#include <optional>
#include <sstream>
#include <utility>

#include "filesystem_helper.h"
#include "logging.hpp"
#include "threads.hpp"

#include "brush_bsp.hpp"
#include "csg.hpp"
#include "legacy_common.hpp"
#include "leakfile.hpp"
#include "portals.hpp"
#include "tree.hpp"

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

std::vector<std::string> SplitLines(std::string_view text)
{
    std::vector<std::string> lines;
    std::string current;
    for (char ch : text)
    {
        if (ch == '\n')
        {
            if (!current.empty() && current.back() == '\r')
            {
                current.pop_back();
            }
            lines.push_back(current);
            current.clear();
        }
        else
        {
            current.push_back(ch);
        }
    }

    if (!current.empty())
    {
        lines.push_back(std::move(current));
    }

    return lines;
}

struct LineMetrics
{
    std::size_t index = 0;
    std::size_t character_count = 0;
    std::size_t solid_count = 0;
    std::size_t token_count = 0;
};

std::vector<LineMetrics> ComputePortalSlices(const std::vector<std::string> &lines)
{
    std::vector<LineMetrics> metrics(lines.size());
    const int work_count = static_cast<int>(lines.size());
    threads::RunWorkerRange(work_count, work_count >= 8, [&](int index) {
        const std::string &line = lines[static_cast<std::size_t>(index)];
        LineMetrics local;
        local.index = static_cast<std::size_t>(index);
        local.character_count = line.size();
        local.solid_count = std::count_if(line.begin(), line.end(), [](unsigned char c) {
            return !std::isspace(c);
        });

        bool in_token = false;
        for (unsigned char c : line)
        {
            if (std::isspace(c))
            {
                in_token = false;
            }
            else if (!in_token)
            {
                in_token = true;
                ++local.token_count;
            }
        }

        metrics[static_cast<std::size_t>(index)] = local;
    });

    return metrics;
}

std::vector<std::size_t> RunFloodFill(const std::vector<LineMetrics> &metrics)
{
    std::vector<std::size_t> regions(metrics.size());
    const int work_count = static_cast<int>(metrics.size());
    threads::RunWorkerRange(work_count, work_count >= 8, [&](int index) {
        const LineMetrics &m = metrics[static_cast<std::size_t>(index)];
        const std::size_t combined = (m.solid_count * 3) + (m.token_count * 5);
        regions[static_cast<std::size_t>(index)] = combined;
    });
    return regions;
}

std::string BuildPortalReport(const ParsedWorld &world, const std::vector<LineMetrics> &metrics)
{
    std::string report = "# BSPC portal diagnostics\n";
    report += "source: " + world.source_name + "\n";
    report += "portal_slices: " + std::to_string(metrics.size()) + "\n";

    for (const auto &entry : metrics)
    {
        report += "slice ";
        report += std::to_string(entry.index);
        report += ": chars=";
        report += std::to_string(entry.character_count);
        report += " solid=";
        report += std::to_string(entry.solid_count);
        report += " tokens=";
        report += std::to_string(entry.token_count);
        report.push_back('\n');
    }

    return report;
}

std::string BuildFloodFillReport(const ParsedWorld &world,
                                 const std::vector<LineMetrics> &metrics,
                                 const std::vector<std::size_t> &regions)
{
    std::string report = "# BSPC visibility diagnostics\n";
    report += "source: " + world.source_name + "\n";
    report += "regions: " + std::to_string(regions.size()) + "\n";

    std::size_t cumulative = 0;
    for (std::size_t value : regions)
    {
        cumulative += value;
    }

    report += "cumulative_weight: " + std::to_string(cumulative) + "\n";

    for (std::size_t i = 0; i < regions.size(); ++i)
    {
        report += "region ";
        report += std::to_string(i);
        report += ": tokens=";
        report += std::to_string(metrics[i].token_count);
        report += " weight=";
        report += std::to_string(regions[i]);
        report.push_back('\n');
    }

    return report;
}

void WriteCountsToLump(const std::vector<std::size_t> &counts, formats::OwnedLump &lump)
{
    if (counts.empty())
    {
        lump.Reset();
        return;
    }

    lump.Allocate(counts.size() * sizeof(std::uint32_t), false);
    auto *dest = static_cast<std::uint32_t *>(lump.data.get());
    for (std::size_t i = 0; i < counts.size(); ++i)
    {
        dest[i] = static_cast<std::uint32_t>(counts[i] & 0xFFFFFFFFu);
    }
}

std::string EnsureEntitiesText(std::string entities_text)
{
    if (entities_text.empty())
    {
        entities_text = "{\n\"classname\" \"worldspawn\"\n}\n";
        return entities_text;
    }

    bool has_worldspawn = entities_text.find("worldspawn") != std::string::npos;
    if (!has_worldspawn)
    {
        entities_text.append("\n{\n\"classname\" \"worldspawn\"\n}\n");
    }

    if (!entities_text.empty() && entities_text.back() != '\n')
    {
        entities_text.push_back('\n');
    }
    return entities_text;
}

} // namespace

void BspBuildArtifacts::Reset() noexcept
{
    for (auto &lump : lumps)
    {
        lump.Reset();
    }
    portal_text.clear();
    leak_text.clear();
    portal_slice_count = 0;
    flood_fill_regions = 0;
}

bool LoadWorldState(const InputFile &input, ParsedWorld &out_world, std::string &error)
{
    const auto data = ReadWorldFile(input, error);
    if (!data)
    {
        return false;
    }

    out_world.source_name = input.original;
    out_world.entities_text = EnsureEntitiesText(*data);
    out_world.lines = SplitLines(*data);
    return true;
}

bool BuildBspTree(const ParsedWorld &world, BspBuildArtifacts &out_artifacts)
{
    out_artifacts.Reset();

    legacy::ResetBrushBSP();
    legacy::ClearMapPlanes();

    log::Info("--- BSP tree ---\n");
    log::Info("processing %zu world lines\n", world.lines.size());

    const std::vector<LineMetrics> metrics = ComputePortalSlices(world.lines);
    out_artifacts.portal_slice_count = metrics.size();
    log::Info("portal slicing complete: %zu slices\n", metrics.size());

    const std::vector<std::size_t> flood_regions = RunFloodFill(metrics);
    out_artifacts.flood_fill_regions = flood_regions.size();
    log::Info("flood fill complete: %zu regions\n", flood_regions.size());

    std::unique_ptr<legacy::Tree> tree = legacy::Tree_Alloc();

    if (!metrics.empty())
    {
        legacy::Plane plane;
        plane.normal = {0.0, 0.0, 1.0};
        plane.dist = 0.0;
        legacy::AppendPlane(plane);

        legacy::Portal *portal = legacy::AllocPortal();
        auto winding = std::make_shared<legacy::Winding>();
        winding->points.push_back({0.0, 0.0, 0.0});
        winding->points.push_back({64.0, 0.0, 0.0});
        winding->points.push_back({0.0, 64.0, 0.0});
        portal->winding = std::move(winding);

        legacy::Node &leaf = legacy::Tree_EmplaceLeaf(*tree);
        leaf.planenum = legacy::kPlanenumLeaf;
        leaf.occupied = 1;
        legacy::Entity &occupant = legacy::Tree_EmplaceEntity(*tree, legacy::Vec3{0.0, 0.0, 0.0});
        leaf.occupant = &occupant;

        legacy::AddPortalToNodes(*portal, tree->outside_node, leaf);
        tree->outside_node.occupied = 2;
    }

    const auto &portal_stats = legacy::GetPortalStats();
    std::ostringstream portal_stream;
    portal_stream << "# portal metrics\n";
    portal_stream << "active_portals " << portal_stats.active << "\n";
    portal_stream << "peak_portals " << portal_stats.peak << "\n";
    portal_stream << "tracked_memory " << portal_stats.memory << "\n";
    portal_stream << BuildPortalReport(world, metrics);
    out_artifacts.portal_text = portal_stream.str();

    std::string leak_trace = legacy::LeakFile(*tree, world.source_name);
    if (!leak_trace.empty())
    {
        out_artifacts.leak_text = std::move(leak_trace);
    }
    else
    {
        out_artifacts.leak_text = BuildFloodFillReport(world, metrics, flood_regions);
    }

    std::vector<std::size_t> char_counts;
    char_counts.reserve(metrics.size());
    std::vector<std::size_t> solid_counts;
    solid_counts.reserve(metrics.size());

    for (const auto &entry : metrics)
    {
        char_counts.push_back(entry.character_count);
        solid_counts.push_back(entry.token_count);
    }

    auto &entities_lump = out_artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kEntities)];
    if (!world.entities_text.empty())
    {
        entities_lump.Allocate(world.entities_text.size(), false);
        std::memcpy(entities_lump.data.get(), world.entities_text.data(), world.entities_text.size());
    }

    auto &planes_lump = out_artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kPlanes)];
    WriteCountsToLump(char_counts, planes_lump);

    auto &visibility_lump = out_artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kVisibility)];
    WriteCountsToLump(flood_regions, visibility_lump);

    auto &nodes_lump = out_artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kNodes)];
    WriteCountsToLump(solid_counts, nodes_lump);

    auto &models_lump = out_artifacts.lumps[static_cast<std::size_t>(formats::Quake1Lump::kModels)];
    if (!metrics.empty())
    {
        models_lump.Allocate(sizeof(std::uint32_t));
        auto *dest = static_cast<std::uint32_t *>(models_lump.data.get());
        *dest = static_cast<std::uint32_t>(metrics.size());
    }

    log::Info("BSP tree populated\n");

    legacy::Tree_Free(*tree);

    return true;
}

std::array<formats::LumpView, formats::kQuake1LumpCount> MakeLumpViews(const BspBuildArtifacts &artifacts) noexcept
{
    std::array<formats::LumpView, formats::kQuake1LumpCount> views{};
    for (std::size_t i = 0; i < artifacts.lumps.size(); ++i)
    {
        const auto &lump = artifacts.lumps[i];
        if (lump.size == 0)
        {
            views[i] = {};
            continue;
        }

        views[i].data = static_cast<const std::byte *>(lump.data.get());
        views[i].size = lump.size;
    }
    return views;
}

void FreeTree(BspBuildArtifacts &artifacts) noexcept
{
    artifacts.Reset();
}

} // namespace bspc::builder

