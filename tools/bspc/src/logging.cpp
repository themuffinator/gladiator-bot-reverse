#include "logging.hpp"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <cerrno>

namespace bspc::log
{
namespace
{
std::FILE *g_log_file = nullptr;
bool g_open_attempted = false;
bool g_registered_atexit = false;
std::mutex g_mutex;

void AtExitShutdown() noexcept
{
    Shutdown();
}

std::string FormatString(const char *format, va_list args)
{
    if (format == nullptr)
    {
        return {};
    }

    std::array<char, 1024> buffer{};

    va_list copy;
    va_copy(copy, args);
    int required = std::vsnprintf(buffer.data(), buffer.size(), format, copy);
    va_end(copy);

    if (required < 0)
    {
        return {};
    }

    if (required < static_cast<int>(buffer.size()))
    {
        return std::string(buffer.data(), static_cast<size_t>(required));
    }

    std::vector<char> dynamic(static_cast<size_t>(required) + 1);
    va_copy(copy, args);
    std::vsnprintf(dynamic.data(), dynamic.size(), format, copy);
    va_end(copy);

    return std::string(dynamic.data(), static_cast<size_t>(required));
}

std::string ConvertForLog(std::string_view text)
{
    std::string converted;
    converted.reserve(text.size() + 16);
    for (size_t i = 0; i < text.size(); ++i)
    {
        char ch = text[i];
        if (ch == '\n')
        {
            if (i == 0 || text[i - 1] != '\r')
            {
                converted.push_back('\r');
            }
            converted.push_back('\n');
        }
        else
        {
            converted.push_back(ch);
        }
    }
    return converted;
}

bool EndsWith(std::string_view text, std::string_view suffix)
{
    if (suffix.size() > text.size())
    {
        return false;
    }
    return std::equal(suffix.rbegin(), suffix.rend(), text.rbegin());
}

void EnsureInitializedLocked()
{
    if (!g_open_attempted)
    {
        g_log_file = std::fopen("bspc.log", "wb");
        if (g_log_file == nullptr)
        {
            std::fprintf(stderr, "WARNING: could not open log file bspc.log: %s\n", std::strerror(errno));
        }
        g_open_attempted = true;
        if (!g_registered_atexit)
        {
            std::atexit(AtExitShutdown);
            g_registered_atexit = true;
        }
    }
}

void WriteInternal(std::string_view text, bool ensure_newline)
{
    std::lock_guard<std::mutex> guard(g_mutex);
    EnsureInitializedLocked();

    std::string console_text(text);
    std::string log_text = ConvertForLog(text);

    if (ensure_newline)
    {
        if (console_text.empty() || console_text.back() != '\n')
        {
            console_text.push_back('\n');
        }
        if (!EndsWith(log_text, "\r\n"))
        {
            log_text.append("\r\n");
        }
    }

    if (!console_text.empty())
    {
        std::fwrite(console_text.data(), 1, console_text.size(), stdout);
        std::fflush(stdout);
    }

    if (g_log_file != nullptr && !log_text.empty())
    {
        std::fwrite(log_text.data(), 1, log_text.size(), g_log_file);
        std::fflush(g_log_file);
    }
}

} // namespace

void Initialize()
{
    std::lock_guard<std::mutex> guard(g_mutex);
    EnsureInitializedLocked();
}

void Shutdown() noexcept
{
    std::lock_guard<std::mutex> guard(g_mutex);
    if (g_log_file != nullptr)
    {
        std::fflush(g_log_file);
        std::fclose(g_log_file);
        g_log_file = nullptr;
    }
}

void Write(std::string_view text)
{
    WriteInternal(text, false);
}

void Info(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    std::string message = FormatString(format, args);
    va_end(args);

    WriteInternal(message, false);
}

void Warning(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    std::string message = FormatString(format, args);
    va_end(args);

    std::string prefixed = "WARNING: ";
    prefixed.append(message);
    WriteInternal(prefixed, true);
}

[[noreturn]] void Fatal(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    std::string message = FormatString(format, args);
    va_end(args);

    std::string prefixed = "ERROR: ";
    prefixed.append(message);
    WriteInternal(prefixed, true);

    Shutdown();
    std::exit(EXIT_FAILURE);
}

} // namespace bspc::log

