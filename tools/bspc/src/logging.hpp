#pragma once

#include <string_view>

namespace bspc::log
{

void Initialize();

void Shutdown() noexcept;

void Write(std::string_view text);

void Info(const char *format, ...);

void Warning(const char *format, ...);

[[noreturn]] void Fatal(const char *format, ...);

} // namespace bspc::log

