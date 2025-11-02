#pragma once

#include <string>

namespace bspc
{
struct Options;
struct InputFile;

namespace pipelines
{

void RunMapToBsp(const Options &options, const InputFile &input, const std::string &destination_path);
void RunBspToMap(const Options &options, const InputFile &input, const std::string &destination_path);
void RunBspToBsp(const Options &options, const InputFile &input, const std::string &destination_path);
void RunMapToAas(const Options &options, const InputFile &input, const std::string &destination_path);
void RunBspToAas(const Options &options, const InputFile &input, const std::string &destination_path);

} // namespace pipelines

} // namespace bspc

