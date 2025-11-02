#pragma once

#include <string>
#include <vector>

#include "filesystem_helper.h"

namespace bspc
{

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

} // namespace bspc

