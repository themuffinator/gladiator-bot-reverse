#include <array>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "bsp_builder.hpp"
#include "bsp_formats.hpp"
#include "filesystem_helper.h"

#ifndef PROJECT_SOURCE_DIR
#error "PROJECT_SOURCE_DIR must be defined so regression tests can resolve asset paths."
#endif

namespace
{

std::filesystem::path AssetDir()
{
    return std::filesystem::path(PROJECT_SOURCE_DIR) / "tests/support/assets/bspc";
}

std::filesystem::path GoldenDir()
{
    return AssetDir() / "golden/bsp_builder";
}

std::string ReadTextFile(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        throw std::runtime_error("failed to open text file: " + path.generic_string());
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

std::vector<std::byte> DecodeBase64(std::string_view input)
{
    auto Decode = [](unsigned char c) -> int {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        if (c == '+')
            return 62;
        if (c == '/')
            return 63;
        if (c == '=')
            return -2;
        return -1;
    };

    std::vector<std::byte> output;
    int value[4] = {0, 0, 0, 0};
    int index = 0;

    for (unsigned char c : input)
    {
        if (std::isspace(c))
        {
            continue;
        }

        const int decoded = Decode(c);
        assert(decoded != -1);
        value[index++] = decoded;

        if (index == 4)
        {
            const int a = value[0];
            const int b = value[1];
            const int cval = value[2];
            const int d = value[3];

            output.push_back(static_cast<std::byte>((a << 2) | ((b & 0x30) >> 4)));
            if (cval != -2)
            {
                output.push_back(static_cast<std::byte>(((b & 0x0F) << 4) | ((cval & 0x3C) >> 2)));
                if (d != -2)
                {
                    output.push_back(static_cast<std::byte>(((cval & 0x03) << 6) | d));
                }
            }

            index = 0;
        }
    }

    assert(index == 0);
    return output;
}

std::vector<std::byte> ReadBspGolden(const std::filesystem::path &path)
{
    const std::string encoded = ReadTextFile(path);
    return DecodeBase64(encoded);
}

std::vector<std::byte> SerializeBsp(const bspc::builder::BspBuildArtifacts &artifacts)
{
    const auto views = bspc::builder::MakeLumpViews(artifacts);
    std::vector<std::byte> bytes;
    std::string error;
    const bool ok = bspc::formats::SerializeQuake1Bsp(views, bytes, error);
    if (!ok)
    {
        throw std::runtime_error("SerializeQuake1Bsp failed: " + error);
    }
    return bytes;
}

void ValidateMap(const std::string &name)
{
    const std::filesystem::path map_path = AssetDir() / (name + ".map");
    bspc::InputFile input;
    input.path = map_path;
    input.original = (std::filesystem::path("tests/support/assets/bspc") / (name + ".map")).generic_string();

    bspc::builder::ParsedWorld world;
    std::string error;
    const bool loaded = bspc::builder::LoadWorldState(input, world, error);
    if (!loaded)
    {
        throw std::runtime_error("LoadWorldState failed: " + error);
    }

    bspc::builder::BspBuildArtifacts artifacts;
    const bool built = bspc::builder::BuildBspTree(world, artifacts);
    if (!built)
    {
        throw std::runtime_error("BuildBspTree failed for " + name);
    }

    const std::vector<std::byte> bsp_data = SerializeBsp(artifacts);
    const std::vector<std::byte> bsp_golden = ReadBspGolden(GoldenDir() / (name + ".bsp.base64"));
    if (bsp_data != bsp_golden)
    {
        std::cerr << "BSP mismatch for " << name << std::endl;
        assert(false);
    }

    const std::string portal_golden = ReadTextFile(GoldenDir() / (name + ".prt"));
    if (portal_golden != artifacts.portal_text)
    {
        std::cerr << "Portal text mismatch for " << name << std::endl;
        assert(false);
    }

    const std::string leak_golden = ReadTextFile(GoldenDir() / (name + ".lin"));
    if (leak_golden != artifacts.leak_text)
    {
        std::cerr << "Leak text mismatch for " << name << std::endl;
        assert(false);
    }
}

} // namespace

int main()
{
    const std::array<std::string, 5> maps = {
        "brush_primitives",
        "csg_room",
        "duplicate_brushes",
        "liquid_room",
        "simple_room",
    };

    for (const auto &name : maps)
    {
        ValidateMap(name);
    }

    return 0;
}
