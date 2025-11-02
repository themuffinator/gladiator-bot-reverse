#include "filesystem_helper.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace
{

using bspc::FileSystemResolver;
using bspc::InputFile;
using bspc::NormalizeSeparators;
using bspc::ReadFile;

bool ReadRawFile(const std::filesystem::path &path, std::vector<std::byte> &out)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return false;
    }

    stream.seekg(0, std::ios::end);
    const std::streampos end = stream.tellg();
    if (end < 0)
    {
        return false;
    }

    const std::size_t size = static_cast<std::size_t>(end);
    out.resize(size);
    stream.seekg(0, std::ios::beg);
    if (!stream)
    {
        return false;
    }

    if (size > 0)
    {
        stream.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(size));
        if (stream.gcount() != static_cast<std::streamsize>(size))
        {
            return false;
        }
    }

    return true;
}

bool ReadArchiveSlice(const InputFile &input, std::vector<std::byte> &out)
{
    if (!input.from_archive)
    {
        return false;
    }

    std::ifstream stream(input.archive_path, std::ios::binary);
    if (!stream)
    {
        return false;
    }

    if (input.archive_length == 0)
    {
        out.clear();
        return true;
    }

    stream.seekg(static_cast<std::streamoff>(input.archive_offset), std::ios::beg);
    if (!stream)
    {
        return false;
    }

    out.resize(input.archive_length);
    stream.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(input.archive_length));
    if (stream.gcount() != static_cast<std::streamsize>(input.archive_length))
    {
        return false;
    }

    return true;
}

std::string BytesToString(const std::vector<std::byte> &bytes)
{
    std::string text(bytes.size(), '\0');
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        text[i] = static_cast<char>(std::to_integer<unsigned char>(bytes[i]));
    }
    return text;
}

bool TestReadFileFromDisk()
{
    const std::filesystem::path project_root = PROJECT_SOURCE_DIR;
    const std::filesystem::path source_path = project_root / "dev_tools/assets/syn.c";

    FileSystemResolver resolver;
    const std::string pattern = NormalizeSeparators(source_path.generic_string());
    auto resolved = resolver.ResolvePattern(pattern, "", false);
    if (resolved.empty())
    {
        std::cerr << "failed to resolve disk fixture: " << source_path << '\n';
        return false;
    }

    const InputFile &input = resolved.front();
    std::vector<std::byte> expected;
    if (!ReadRawFile(input.path, expected))
    {
        std::cerr << "failed to read expected contents from disk" << '\n';
        return false;
    }

    std::vector<std::byte> actual;
    if (!ReadFile(input, actual, false))
    {
        std::cerr << "ReadFile returned false for disk input" << '\n';
        return false;
    }

    if (actual != expected)
    {
        std::cerr << "disk input bytes differed from expected" << '\n';
        return false;
    }

    return true;
}

bool TestReadFileFromArchive()
{
    const std::filesystem::path project_root = PROJECT_SOURCE_DIR;
    const std::filesystem::path source_path = project_root / "dev_tools/assets/syn.c";
    const std::filesystem::path archive_path = project_root / "dev_tools/assets/pak7.pak";

    FileSystemResolver resolver;

    auto direct = resolver.ResolvePattern(NormalizeSeparators(source_path.generic_string()), "", false);
    if (direct.empty())
    {
        std::cerr << "failed to resolve direct syn.c fixture" << '\n';
        return false;
    }

    const InputFile &direct_file = direct.front();
    std::vector<std::byte> direct_bytes;
    if (!ReadFile(direct_file, direct_bytes, false))
    {
        std::cerr << "failed to read direct syn.c fixture" << '\n';
        return false;
    }

    std::string archive_pattern = NormalizeSeparators(archive_path.generic_string());
    archive_pattern.push_back('/');
    archive_pattern.append("syn.c");
    auto archived = resolver.ResolvePattern(archive_pattern, "", false);
    if (archived.empty())
    {
        std::cerr << "failed to resolve archive syn.c fixture" << '\n';
        return false;
    }

    const InputFile &archive_file = archived.front();
    if (!archive_file.from_archive)
    {
        std::cerr << "expected archive input to be marked as from_archive" << '\n';
        return false;
    }

    std::vector<std::byte> archive_bytes;
    if (!ReadFile(archive_file, archive_bytes, false))
    {
        std::cerr << "ReadFile returned false for archive input" << '\n';
        return false;
    }

    std::vector<std::byte> manual_archive;
    if (!ReadArchiveSlice(archive_file, manual_archive))
    {
        std::cerr << "failed to read archive contents manually" << '\n';
        return false;
    }

    if (archive_bytes != manual_archive)
    {
        std::cerr << "archive bytes did not match manual slice" << '\n';
        return false;
    }

    std::vector<std::byte> normalized_direct;
    if (!ReadFile(direct_file, normalized_direct, true))
    {
        std::cerr << "failed to read direct fixture with newline normalization" << '\n';
        return false;
    }

    std::vector<std::byte> normalized_archive;
    if (!ReadFile(archive_file, normalized_archive, true))
    {
        std::cerr << "failed to read archive fixture with newline normalization" << '\n';
        return false;
    }

    if (normalized_direct != normalized_archive)
    {
        std::cerr << "normalized archive bytes did not match normalized direct bytes" << '\n';
        return false;
    }

    return true;
}

bool TestNormalizeNewlines()
{
    const std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
    const std::filesystem::path temp_file = temp_dir / "bspc_fs_newlines.txt";

    {
        std::ofstream stream(temp_file, std::ios::binary);
        if (!stream)
        {
            std::cerr << "failed to create temporary newline fixture" << '\n';
            return false;
        }
        stream << "line1\r\nline2\rline3\n";
    }

    InputFile input;
    input.original = NormalizeSeparators(temp_file.generic_string());
    input.path = temp_file;

    std::vector<std::byte> bytes;
    if (!ReadFile(input, bytes, true))
    {
        std::cerr << "failed to read temporary newline fixture" << '\n';
        std::filesystem::remove(temp_file);
        return false;
    }

    const std::string text = BytesToString(bytes);
    if (text != "line1\nline2\nline3\n")
    {
        std::cerr << "newline normalization produced unexpected text: " << text << '\n';
        std::filesystem::remove(temp_file);
        return false;
    }

    std::filesystem::remove(temp_file);
    return true;
}

bool TestResolveCompanionsFromDisk()
{
    const std::filesystem::path project_root = PROJECT_SOURCE_DIR;
    const std::filesystem::path bsp_fixture = project_root / "dev_tools/assets/maps/2box4.bsp";

    const std::filesystem::path temp_root = std::filesystem::temp_directory_path();
    const std::filesystem::path work_dir = temp_root / "bspc_fs_tests";
    std::error_code ec;
    std::filesystem::remove_all(work_dir, ec);
    if (ec)
    {
        std::cerr << "failed to reset temporary work directory" << '\n';
        return false;
    }

    std::filesystem::create_directories(work_dir, ec);
    if (ec)
    {
        std::cerr << "failed to create temporary work directory" << '\n';
        return false;
    }

    const std::filesystem::path bsp_copy = work_dir / "test_map.bsp";
    std::filesystem::copy_file(bsp_fixture, bsp_copy, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
    {
        std::cerr << "failed to copy bsp fixture" << '\n';
        std::filesystem::remove_all(work_dir);
        return false;
    }

    const std::filesystem::path prt_path = work_dir / "test_map.prt";
    {
        std::ofstream prt_stream(prt_path, std::ios::binary);
        prt_stream << "prt contents";
    }

    const std::filesystem::path lin_path = work_dir / "test_map.lin";
    {
        std::ofstream lin_stream(lin_path, std::ios::binary);
        lin_stream << "lin contents";
    }

    FileSystemResolver resolver;
    const std::string pattern = NormalizeSeparators(bsp_copy.generic_string());
    auto resolved = resolver.ResolvePattern(pattern, "bsp", false);
    if (resolved.empty())
    {
        std::cerr << "failed to resolve copied bsp fixture" << '\n';
        std::filesystem::remove_all(work_dir);
        return false;
    }

    InputFile input = resolved.front();
    auto companions = resolver.ResolveCompanions(input);
    if (companions.size() != 2)
    {
        std::cerr << "expected two companions for bsp input" << '\n';
        std::filesystem::remove_all(work_dir);
        return false;
    }

    bool prt_ok = false;
    bool lin_ok = false;
    for (const auto &companion : companions)
    {
        std::vector<std::byte> content;
        if (!ReadFile(companion, content, false))
        {
            std::cerr << "failed to read companion file" << '\n';
            std::filesystem::remove_all(work_dir);
            return false;
        }

        const std::string text = BytesToString(content);
        if (companion.path.extension() == ".prt")
        {
            prt_ok = (text == "prt contents");
        }
        else if (companion.path.extension() == ".lin")
        {
            lin_ok = (text == "lin contents");
        }
    }

    std::filesystem::remove_all(work_dir);

    if (!prt_ok || !lin_ok)
    {
        std::cerr << "companion contents did not match expectations" << '\n';
        return false;
    }

    return true;
}

} // namespace

int main()
{
    bool success = true;
    success &= TestReadFileFromDisk();
    success &= TestReadFileFromArchive();
    success &= TestNormalizeNewlines();
    success &= TestResolveCompanionsFromDisk();

    return success ? 0 : 1;
}
