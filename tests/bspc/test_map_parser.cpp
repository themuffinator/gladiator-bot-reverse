#include <cassert>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "map_parser.hpp"

#ifndef PROJECT_SOURCE_DIR
#error "PROJECT_SOURCE_DIR must be defined so regression tests can resolve asset paths."
#endif

namespace
{

std::filesystem::path AssetDir()
{
    return std::filesystem::path(PROJECT_SOURCE_DIR) / "tests/support/assets/bspc";
}

std::string ReadAsset(const std::filesystem::path &relative)
{
    std::ifstream stream(AssetDir() / relative, std::ios::binary);
    assert(stream);
    std::string contents;
    stream.seekg(0, std::ios::end);
    const std::streampos end = stream.tellg();
    assert(end >= 0);
    contents.resize(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!contents.empty())
    {
        stream.read(contents.data(), static_cast<std::streamsize>(contents.size()));
        assert(stream.gcount() == static_cast<std::streamsize>(contents.size()));
    }
    return contents;
}

void StoreU32LE(char *dest, std::uint32_t value)
{
    dest[0] = static_cast<char>(value & 0xffu);
    dest[1] = static_cast<char>((value >> 8) & 0xffu);
    dest[2] = static_cast<char>((value >> 16) & 0xffu);
    dest[3] = static_cast<char>((value >> 24) & 0xffu);
}

void WriteU32LE(std::ofstream &stream, std::uint32_t value)
{
    char buffer[4];
    StoreU32LE(buffer, value);
    stream.write(buffer, sizeof(buffer));
}

std::filesystem::path CreateTempDirectory()
{
    std::filesystem::path base = std::filesystem::temp_directory_path();
    std::filesystem::path dir = base / "bspc_archive_tests";
    std::error_code cleanup_ec;
    std::filesystem::remove_all(dir, cleanup_ec);
    std::filesystem::create_directories(dir);
    return dir;
}

std::filesystem::path CreateArchive(const std::filesystem::path &directory,
                                    std::string_view filename,
                                    std::string_view entry_name,
                                    std::string_view contents,
                                    bool heretic_sin_format)
{
    const std::filesystem::path archive_path = directory / filename;
    std::ofstream stream(archive_path, std::ios::binary);
    assert(stream);

    const bool sin_format = heretic_sin_format;
    const char *magic = sin_format ? "SPAK" : "PACK";
    const std::uint32_t entry_size = sin_format ? 128u : 64u;
    const std::uint32_t name_length = sin_format ? 120u : 56u;
    assert(entry_name.size() < name_length);

    const std::uint32_t data_offset = 12u;
    const std::uint32_t directory_offset = data_offset + static_cast<std::uint32_t>(contents.size());
    const std::uint32_t directory_length = entry_size;

    stream.write(magic, 4);
    WriteU32LE(stream, directory_offset);
    WriteU32LE(stream, directory_length);

    if (!contents.empty())
    {
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    std::vector<char> entry(entry_size, 0);
    std::copy(entry_name.begin(), entry_name.end(), entry.begin());
    StoreU32LE(entry.data() + name_length, data_offset);
    StoreU32LE(entry.data() + name_length + 4, static_cast<std::uint32_t>(contents.size()));
    stream.write(entry.data(), static_cast<std::streamsize>(entry.size()));

    return archive_path;
}

bspc::map::ParseResult ParseFromDisk(const std::filesystem::path &relative, const bspc::Options &options)
{
    bspc::InputFile input;
    input.path = AssetDir() / relative;
    input.original = input.path.generic_string();
    std::optional<bspc::map::ParseResult> parsed = bspc::map::ParseMapFromFile(input, options);
    assert(parsed.has_value());
    return *parsed;
}

bspc::map::ParseResult ParseFromInput(const bspc::InputFile &input, const bspc::Options &options)
{
    std::optional<bspc::map::ParseResult> parsed = bspc::map::ParseMapFromFile(input, options);
    assert(parsed.has_value());
    return *parsed;
}

void TestSimpleRoom()
{
    bspc::Options options;
    bspc::map::ParseResult parsed = ParseFromDisk("simple_room.map", options);

    assert(parsed.summary.entities == 1);
    assert(parsed.summary.brushes == 1);
    assert(parsed.summary.planes == 6);
    assert(parsed.summary.unique_materials == 1);

    assert(!parsed.preprocess.brush_merge_enabled);
    assert(!parsed.preprocess.csg_enabled);
    assert(!parsed.preprocess.liquids_filtered);
    assert(!parsed.preprocess.breath_first);

    const bspc::map::Entity &entity = parsed.entities.front();
    auto classname = entity.FindProperty("classname");
    assert(classname.has_value());
    assert(*classname == "worldspawn");
}

void TestBrushPrimitiveAndPatch()
{
    bspc::Options options;
    bspc::map::ParseResult parsed = ParseFromDisk("brush_primitives.map", options);

    assert(parsed.summary.entities == 1);
    assert(parsed.summary.brushes == 2);
    assert(parsed.summary.planes == 6);
    assert(parsed.summary.unique_materials == 1);

    const bspc::map::Entity &entity = parsed.entities.front();
    assert(entity.brushes.size() == 2);
    bool found_patch = false;
    bool found_brush = false;
    for (const auto &brush : entity.brushes)
    {
        if (brush.type == bspc::map::Brush::Type::kPatch)
        {
            assert(brush.patch.has_value());
            assert(brush.patch->width == 3);
            assert(brush.patch->height == 3);
            found_patch = true;
        }
        else
        {
            assert(brush.is_brush_primitive);
            assert(brush.planes.size() == 6);
            found_brush = true;
        }
    }
    assert(found_patch);
    assert(found_brush);
}

void TestBrushMerging()
{
    bspc::Options options;
    bspc::map::ParseResult merged = ParseFromDisk("duplicate_brushes.map", options);
    assert(merged.summary.brushes == 1);
    assert(merged.preprocess.brush_merge_enabled);

    options.nobrushmerge = true;
    bspc::map::ParseResult no_merge = ParseFromDisk("duplicate_brushes.map", options);
    assert(no_merge.summary.brushes == 2);
    assert(!no_merge.preprocess.brush_merge_enabled);
}

void TestCsgFiltering()
{
    bspc::Options options;
    bspc::map::ParseResult filtered = ParseFromDisk("csg_room.map", options);
    assert(filtered.summary.brushes == 1);
    assert(filtered.summary.planes == 6);
    assert(filtered.preprocess.csg_enabled);

    options.nocsg = true;
    bspc::map::ParseResult untouched = ParseFromDisk("csg_room.map", options);
    assert(untouched.summary.brushes == 2);
    assert(untouched.summary.planes == 12);
    assert(!untouched.preprocess.csg_enabled);
}

void TestLiquidFiltering()
{
    bspc::Options options;
    bspc::map::ParseResult keep = ParseFromDisk("liquid_room.map", options);
    assert(keep.summary.brushes == 1);
    assert(!keep.preprocess.liquids_filtered);

    options.noliquids = true;
    bspc::map::ParseResult filtered = ParseFromDisk("liquid_room.map", options);
    assert(filtered.summary.brushes == 0);
    assert(filtered.preprocess.liquids_filtered);
}

void TestArchiveLoading()
{
    const std::filesystem::path temp_dir = CreateTempDirectory();
    const std::string map_contents = ReadAsset("simple_room.map");
    const std::filesystem::path pak = CreateArchive(temp_dir, "test_maps.pak", "simple_room.map", map_contents, false);
    const std::filesystem::path sin = CreateArchive(temp_dir, "test_maps.sin", "simple_room.map", map_contents, true);

    bspc::FileSystemResolver resolver;
    const std::string pak_pattern = (pak.generic_string() + "/*.map");
    std::vector<bspc::InputFile> pak_inputs = resolver.ResolvePattern(pak_pattern, "map", false);
    assert(!pak_inputs.empty());
    for (const auto &input : pak_inputs)
    {
        assert(input.from_archive);
        bspc::Options options;
        bspc::map::ParseResult parsed = ParseFromInput(input, options);
        assert(parsed.summary.brushes == 1);
        assert(parsed.summary.entities == 1);
    }

    const std::string sin_pattern = (sin.generic_string() + "/*.map");
    std::vector<bspc::InputFile> sin_inputs = resolver.ResolvePattern(sin_pattern, "map", false);
    assert(!sin_inputs.empty());
    for (const auto &input : sin_inputs)
    {
        assert(input.from_archive);
        bspc::Options options;
        bspc::map::ParseResult parsed = ParseFromInput(input, options);
        assert(parsed.summary.brushes == 1);
        assert(parsed.summary.entities == 1);
    }

    std::error_code cleanup_ec;
    std::filesystem::remove_all(temp_dir, cleanup_ec);
}

void TestBreathFirstFlag()
{
    bspc::Options options;
    options.breath_first = true;
    bspc::map::ParseResult parsed = ParseFromDisk("simple_room.map", options);
    assert(parsed.preprocess.breath_first);
}

} // namespace

int main()
{
    TestSimpleRoom();
    TestBrushPrimitiveAndPatch();
    TestBrushMerging();
    TestCsgFiltering();
    TestLiquidFiltering();
    TestArchiveLoading();
    TestBreathFirstFlag();
    return 0;
}
