#include "l_memory.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define BOT_MEMORY_MAGIC 0x12345678u

/**
 * Block header mirroring the layout observed in the disassembly. The pointer to
 * the usable payload is stored explicitly in order to keep the runtime checks
 * byte-for-byte compatible with the original Gladiator allocator.
 */
typedef struct bot_memory_block_s {
    uint32_t magic;
    uint8_t *payload;
    size_t total_size;
    struct bot_memory_block_s *prev;
    struct bot_memory_block_s *next;
} bot_memory_block_t;

typedef struct bot_memory_state_s {
    bool initialised;
    size_t heap_capacity;
    size_t allocated_bytes;
    int block_count;
    bot_memory_block_t *head;
    bot_memory_log_fn log_callback;
} bot_memory_state_t;

static bot_memory_state_t g_memory_state = {
    .initialised = false,
    .heap_capacity = BOT_MEMORY_DEFAULT_HEAP_SIZE,
    .allocated_bytes = 0,
    .block_count = 0,
    .head = NULL,
    .log_callback = NULL,
};

static void BotMemory_DefaultLog(int level, const char *fmt, va_list args) {
    (void)level;
    vfprintf(stderr, fmt, args);
}

static void BotMemory_Log(int level, const char *fmt, ...) {
    bot_memory_log_fn callback = g_memory_state.log_callback;
    if (callback == NULL) {
        callback = BotMemory_DefaultLog;
    }

    va_list args;
    va_start(args, fmt);
    callback(level, fmt, args);
    va_end(args);
}

static void BotMemory_LinkBlock(bot_memory_block_t *block) {
    block->prev = NULL;
    block->next = g_memory_state.head;
    if (g_memory_state.head != NULL) {
        g_memory_state.head->prev = block;
    }
    g_memory_state.head = block;
}

static void BotMemory_UnlinkBlock(bot_memory_block_t *block) {
    if (block->prev != NULL) {
        block->prev->next = block->next;
    } else {
        g_memory_state.head = block->next;
    }

    if (block->next != NULL) {
        block->next->prev = block->prev;
    }

    block->prev = NULL;
    block->next = NULL;
}

static bot_memory_block_t *BotMemory_BlockFromPointer(const void *ptr, const char *caller) {
    if (ptr == NULL) {
        BotMemory_Log(BOT_MEMORY_LOG_ERROR, "%s: invalid memory block\n", caller);
        return NULL;
    }

    const uint8_t *payload = (const uint8_t *)ptr;
    const bot_memory_block_t *candidate = (const bot_memory_block_t *)(payload - sizeof(bot_memory_block_t));

    if (candidate->magic != BOT_MEMORY_MAGIC) {
        BotMemory_Log(BOT_MEMORY_LOG_ERROR, "%s: invalid memory block\n", caller);
        return NULL;
    }

    if (candidate->payload != payload) {
        BotMemory_Log(BOT_MEMORY_LOG_ERROR, "%s: memory block pointer invalid\n", caller);
        return NULL;
    }

    return (bot_memory_block_t *)candidate;
}

static void BotMemory_TrackAllocation(bot_memory_block_t *block) {
    g_memory_state.allocated_bytes += block->total_size;
    g_memory_state.block_count += 1;
    BotMemory_LinkBlock(block);
}

static void BotMemory_TrackDeallocation(bot_memory_block_t *block) {
    if (g_memory_state.allocated_bytes >= block->total_size) {
        g_memory_state.allocated_bytes -= block->total_size;
    } else {
        g_memory_state.allocated_bytes = 0;
    }

    if (g_memory_state.block_count > 0) {
        g_memory_state.block_count -= 1;
    }

    BotMemory_UnlinkBlock(block);
}

bool BotMemory_Init(size_t heap_size) {
    if (g_memory_state.initialised) {
        return true;
    }

    if (heap_size == 0) {
        heap_size = BOT_MEMORY_DEFAULT_HEAP_SIZE;
    }

    g_memory_state.initialised = true;
    g_memory_state.heap_capacity = heap_size;
    g_memory_state.allocated_bytes = 0;
    g_memory_state.block_count = 0;
    g_memory_state.head = NULL;

    return true;
}

void BotMemory_SetLogCallback(bot_memory_log_fn callback) {
    g_memory_state.log_callback = callback;
}

void BotMemory_Shutdown(void) {
    if (!g_memory_state.initialised) {
        return;
    }

    BotMemory_LogSummary();

    while (g_memory_state.head != NULL) {
        void *payload = g_memory_state.head->payload;
        FreeMemory(payload);
    }

    g_memory_state.head = NULL;
    g_memory_state.allocated_bytes = 0;
    g_memory_state.block_count = 0;
    g_memory_state.initialised = false;
}

void BotMemory_LogSummary(void) {
    BotMemory_Log(BOT_MEMORY_LOG_INFO, "total botlib memory: %zu KB\n", g_memory_state.allocated_bytes >> 10);
    BotMemory_Log(BOT_MEMORY_LOG_INFO, "total memory blocks: %d\n", g_memory_state.block_count);
}

void *GetMemory(size_t size) {
    if (!g_memory_state.initialised) {
        if (!BotMemory_Init(g_memory_state.heap_capacity)) {
            return NULL;
        }
    }

    if (size == 0) {
        return NULL;
    }

    size_t total_size = sizeof(bot_memory_block_t) + size;
    if (g_memory_state.allocated_bytes + total_size > g_memory_state.heap_capacity) {
        BotMemory_Log(BOT_MEMORY_LOG_ERROR, "GetMemory: out of botlib memory (requested %zu bytes)\n", size);
        return NULL;
    }

    bot_memory_block_t *block = (bot_memory_block_t *)malloc(total_size);
    if (block == NULL) {
        BotMemory_Log(BOT_MEMORY_LOG_ERROR, "GetMemory: system allocation failed (%zu bytes)\n", size);
        return NULL;
    }

    block->magic = BOT_MEMORY_MAGIC;
    block->payload = (uint8_t *)(block + 1);
    block->total_size = total_size;
    block->prev = NULL;
    block->next = NULL;

    BotMemory_TrackAllocation(block);

    return block->payload;
}

void *GetClearedMemory(size_t size) {
    void *ptr = GetMemory(size);
    if (ptr != NULL) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void FreeMemory(void *ptr) {
    bot_memory_block_t *block = BotMemory_BlockFromPointer(ptr, "FreeMemory");
    if (block == NULL) {
        return;
    }

    BotMemory_TrackDeallocation(block);
    free(block);
}

size_t MemoryByteSize(const void *ptr) {
    bot_memory_block_t *block = BotMemory_BlockFromPointer(ptr, "MemoryByteSize");
    if (block == NULL) {
        return 0;
    }

    if (block->total_size < sizeof(bot_memory_block_t)) {
        return 0;
    }

    return block->total_size - sizeof(bot_memory_block_t);
}

int AvailableMemory(void) {
    if (!g_memory_state.initialised) {
        return (int)g_memory_state.heap_capacity;
    }

    size_t remaining = 0;
    if (g_memory_state.heap_capacity > g_memory_state.allocated_bytes) {
        remaining = g_memory_state.heap_capacity - g_memory_state.allocated_bytes;
    }

    if (remaining > (size_t)INT32_MAX) {
        remaining = (size_t)INT32_MAX;
    }

    return (int)remaining;
}

size_t BotMemory_TotalAllocated(void) {
    return g_memory_state.allocated_bytes;
}

size_t BotMemory_HeapCapacity(void) {
    return g_memory_state.heap_capacity;
}
