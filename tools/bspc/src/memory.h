#pragma once

#include <cstddef>

namespace bspc::memory
{
struct ArenaBuffer
{
    void *data = nullptr;
    std::size_t size_bytes = 0;
    std::size_t used_count = 0;
};

struct TmpAasArena
{
    ArenaBuffer faces;
    ArenaBuffer areas;
    ArenaBuffer area_settings;
    ArenaBuffer nodes;
};

// Formats a human readable memory size identical to the legacy allocator.
void PrintMemorySize(std::size_t size_bytes);

// Allocates memory and zeroes it. Terminates with the legacy out of memory
// message when allocation fails.
void *GetClearedMemory(std::size_t size_bytes);

// Allocates memory without zeroing. Terminates with the legacy out of memory
// message when allocation fails.
void *GetMemory(std::size_t size_bytes);

// Releases memory previously allocated with GetClearedMemory/GetMemory.
void FreeMemory(void *ptr) noexcept;

// Initializes the temporary AAS arena buffers and emits the original BSPC
// allocation banner. Safe to call multiple times; reinitialisation resets the
// arena to a clean state.
void InitTmpAasArena();

// Releases the temporary AAS arena buffers and emits the original BSPC free
// banner. Safe to call even when the arena has not been initialised.
void ShutdownTmpAasArena() noexcept;

// Provides mutable access to the underlying temporary AAS arena buffers so the
// geometry/AAS pipeline can carve the preallocated pools into typed views.
TmpAasArena &TmpAasArenaState() noexcept;

// Returns the total number of bytes reserved for the temporary AAS arena.
std::size_t TmpAasArenaFootprint() noexcept;
} // namespace bspc::memory
