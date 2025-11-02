#include "memory.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace bspc::memory
{
namespace
{
constexpr std::size_t kTmpFaceArenaSize = 0x9c4000;     // matches sub_4015e0
constexpr std::size_t kTmpAreaArenaSize = 0x1c0000;
constexpr std::size_t kTmpAreaSettingsSize = 0x140000;
constexpr std::size_t kTmpNodeArenaSize = 0x2ee000;

constexpr std::array<std::size_t, 4> kTmpArenaSizes = {
    kTmpFaceArenaSize,
    kTmpAreaArenaSize,
    kTmpAreaSettingsSize,
    kTmpNodeArenaSize,
};

struct TmpAasArenaBacking
{
    TmpAasArena arena;
    bool initialised = false;
    std::size_t footprint = 0;
};

TmpAasArenaBacking g_tmp_arena;

[[noreturn]] void FailOutOfMemory()
{
    std::fputs("out of memory\n", stderr);
    std::fflush(stderr);
    throw std::bad_alloc();
}

void ZeroArenaBuffers()
{
    if (!g_tmp_arena.initialised)
    {
        return;
    }

    if (g_tmp_arena.arena.faces.data != nullptr)
    {
        std::memset(g_tmp_arena.arena.faces.data, 0, g_tmp_arena.arena.faces.size_bytes);
    }
    if (g_tmp_arena.arena.areas.data != nullptr)
    {
        std::memset(g_tmp_arena.arena.areas.data, 0, g_tmp_arena.arena.areas.size_bytes);
    }
    if (g_tmp_arena.arena.area_settings.data != nullptr)
    {
        std::memset(g_tmp_arena.arena.area_settings.data, 0, g_tmp_arena.arena.area_settings.size_bytes);
    }
    if (g_tmp_arena.arena.nodes.data != nullptr)
    {
        std::memset(g_tmp_arena.arena.nodes.data, 0, g_tmp_arena.arena.nodes.size_bytes);
    }
}

void ResetUsageCounters()
{
    g_tmp_arena.arena.faces.used_count = 0;
    g_tmp_arena.arena.areas.used_count = 0;
    g_tmp_arena.arena.area_settings.used_count = 0;
    g_tmp_arena.arena.nodes.used_count = 0;
}
} // namespace

void PrintMemorySize(std::size_t size_bytes)
{
    std::size_t megabytes = size_bytes >> 20;
    std::size_t kilobytes = (size_bytes & 0xFFFFF) >> 10;
    std::size_t bytes = size_bytes & 0x3FF;

    bool printed_any = false;
    if (megabytes != 0)
    {
        std::printf("%zu MB", megabytes);
        printed_any = true;
    }
    if (kilobytes != 0)
    {
        if (printed_any)
        {
            std::printf(" and ");
        }
        std::printf("%zu KB", kilobytes);
        printed_any = true;
    }
    if (bytes != 0 || !printed_any)
    {
        if (printed_any)
        {
            std::printf(" and ");
        }
        std::printf("%zu bytes", bytes);
    }
}

void *GetClearedMemory(std::size_t size_bytes)
{
    void *ptr = std::malloc(size_bytes);
    if (ptr == nullptr)
    {
        FailOutOfMemory();
    }
    std::memset(ptr, 0, size_bytes);
    return ptr;
}

void *GetMemory(std::size_t size_bytes)
{
    void *ptr = std::malloc(size_bytes);
    if (ptr == nullptr)
    {
        FailOutOfMemory();
    }
    return ptr;
}

void FreeMemory(void *ptr) noexcept
{
    std::free(ptr);
}

void InitTmpAasArena()
{
    if (g_tmp_arena.initialised)
    {
        ZeroArenaBuffers();
        ResetUsageCounters();
        return;
    }

    g_tmp_arena.arena.faces.data = GetClearedMemory(kTmpFaceArenaSize);
    g_tmp_arena.arena.faces.size_bytes = kTmpFaceArenaSize;

    g_tmp_arena.arena.areas.data = GetClearedMemory(kTmpAreaArenaSize);
    g_tmp_arena.arena.areas.size_bytes = kTmpAreaArenaSize;

    g_tmp_arena.arena.area_settings.data = GetClearedMemory(kTmpAreaSettingsSize);
    g_tmp_arena.arena.area_settings.size_bytes = kTmpAreaSettingsSize;

    g_tmp_arena.arena.nodes.data = GetClearedMemory(kTmpNodeArenaSize);
    g_tmp_arena.arena.nodes.size_bytes = kTmpNodeArenaSize;

    ResetUsageCounters();

    g_tmp_arena.footprint = 0;
    for (std::size_t size : kTmpArenaSizes)
    {
        g_tmp_arena.footprint += size;
    }

    g_tmp_arena.initialised = true;

    std::printf("allocated ");
    PrintMemorySize(g_tmp_arena.footprint);
    std::printf(" of tmp AAS memory\n");
}

void ShutdownTmpAasArena() noexcept
{
    if (!g_tmp_arena.initialised)
    {
        return;
    }

    const std::size_t footprint = g_tmp_arena.footprint;

    FreeMemory(g_tmp_arena.arena.faces.data);
    g_tmp_arena.arena.faces = {};

    FreeMemory(g_tmp_arena.arena.areas.data);
    g_tmp_arena.arena.areas = {};

    FreeMemory(g_tmp_arena.arena.area_settings.data);
    g_tmp_arena.arena.area_settings = {};

    FreeMemory(g_tmp_arena.arena.nodes.data);
    g_tmp_arena.arena.nodes = {};

    g_tmp_arena.initialised = false;
    g_tmp_arena.footprint = 0;

    std::printf("freed ");
    PrintMemorySize(footprint);
    std::printf(" of tmp AAS memory\n");
}

TmpAasArena &TmpAasArenaState() noexcept
{
    return g_tmp_arena.arena;
}

std::size_t TmpAasArenaFootprint() noexcept
{
    return g_tmp_arena.footprint;
}
} // namespace bspc::memory
