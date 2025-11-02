#include <cassert>
#include <filesystem>
#include <optional>
#include <string>

#include "map_parser.hpp"

#ifndef PROJECT_SOURCE_DIR
#error "PROJECT_SOURCE_DIR must be defined so regression tests can resolve asset paths."
#endif

namespace
{

std::filesystem::path AssetPath()
{
    return std::filesystem::path(PROJECT_SOURCE_DIR) / "tests/support/assets/bspc/simple_room.map";
}

} // namespace

int main()
{
    bspc::Options options;
    options.breath_first = true;

    bspc::InputFile input;
    input.path = AssetPath();
    input.original = input.path.generic_string();

    std::optional<bspc::map::ParseResult> parsed = bspc::map::ParseMapFromFile(input, options);
    assert(parsed.has_value());

    const bspc::map::Summary &summary = parsed->summary;
    assert(summary.entities == 1);
    assert(summary.brushes == 1);
    assert(summary.planes == 6);
    assert(summary.unique_materials == 1);

    assert(parsed->preprocess.breath_first);
    assert(parsed->preprocess.brush_merge_enabled);
    assert(parsed->preprocess.csg_enabled);
    assert(!parsed->preprocess.liquids_filtered);

    const bspc::map::Entity &entity = parsed->entities.front();
    assert(entity.properties.size() >= 1);
    auto classname = entity.FindProperty("classname");
    assert(classname.has_value());
    assert(*classname == "worldspawn");

    return 0;
}

