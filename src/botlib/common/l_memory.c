#include "l_memory.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOT_MEMORY_MAGIC 0x12345678u

/**
 * Native representation of the five-field block header observed in the retail
 * allocator. The retail x86 build occupies 0x14 bytes; native pointer width
 * determines the size of this reconstruction's host-side representation.
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
	size_t allocated_bytes;
	int block_count;
	bot_memory_block_t *head;
	bot_memory_log_fn log_callback;
	bot_memory_allocate_fn allocate_callback;
	bot_memory_free_fn free_callback;
} bot_memory_state_t;

static bot_memory_state_t g_memory_state = {
	.initialised = false,
	.allocated_bytes = 0,
	.block_count = 0,
	.head = NULL,
	.log_callback = NULL,
	.allocate_callback = NULL,
	.free_callback = NULL,
};

/*
=============
BotMemory_DefaultLog

Writes allocator diagnostics for standalone users without an engine printer.
=============
*/
static void BotMemory_DefaultLog(int level, const char *fmt, va_list args)
{
	(void)level;
	vfprintf(stderr, fmt, args);
}

/*
=============
BotMemory_Log

Routes a formatted allocator diagnostic to the configured printer.
=============
*/
static void BotMemory_Log(int level, const char *fmt, ...)
{
	bot_memory_log_fn callback = g_memory_state.log_callback;
	if (callback == NULL) {
		callback = BotMemory_DefaultLog;
	}

	va_list args;
	va_start(args, fmt);
	callback(level, fmt, args);
	va_end(args);
}

/*
=============
BotMemory_DefaultAllocate

Provides the standalone allocation fallback used before imports are bound.
=============
*/
static void *BotMemory_DefaultAllocate(int size)
{
	return malloc((size_t)size);
}

/*
=============
BotMemory_DefaultFree

Provides the matching standalone deallocation fallback.
=============
*/
static void BotMemory_DefaultFree(void *ptr)
{
	free(ptr);
}

/*
=============
BotMemory_LinkBlock

Adds a retail memory header to the tracked allocation list.
=============
*/
static void BotMemory_LinkBlock(bot_memory_block_t *block)
{
	block->prev = NULL;
	block->next = g_memory_state.head;
	if (g_memory_state.head != NULL) {
		g_memory_state.head->prev = block;
	}
	g_memory_state.head = block;
}

/*
=============
BotMemory_UnlinkBlock

Removes a retail memory header from the tracked allocation list.
=============
*/
static void BotMemory_UnlinkBlock(bot_memory_block_t *block)
{
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

/*
=============
BotMemory_BlockFromPointer

Recovers and validates a tracked block header from its payload pointer.
=============
*/
static bot_memory_block_t *BotMemory_BlockFromPointer(const void *ptr, const char *caller)
{
	if (ptr == NULL) {
		return NULL;
	}

	const uint8_t *payload = (const uint8_t *)ptr;
	const bot_memory_block_t *candidate = (const bot_memory_block_t *)(payload - sizeof(bot_memory_block_t));

	if (candidate->magic != BOT_MEMORY_MAGIC) {
		BotMemory_Log(BOT_MEMORY_LOG_FATAL, "%s: invalid memory block\n", caller);
		return NULL;
	}

	if (candidate->payload != payload) {
		BotMemory_Log(BOT_MEMORY_LOG_FATAL, "%s: memory block pointer invalid\n", caller);
		return NULL;
	}

	return (bot_memory_block_t *)candidate;
}

/*
=============
BotMemory_TrackAllocation

Updates retail counters and links a newly allocated block.
=============
*/
static void BotMemory_TrackAllocation(bot_memory_block_t *block)
{
	g_memory_state.allocated_bytes += block->total_size;
	g_memory_state.block_count += 1;
	BotMemory_LinkBlock(block);
}

/*
=============
BotMemory_TrackDeallocation

Updates retail counters and unlinks a released block.
=============
*/
static void BotMemory_TrackDeallocation(bot_memory_block_t *block)
{
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

/*
=============
BotMemory_Init

Initialises tracking state; Gladiator delegates capacity to the engine.
=============
*/
bool BotMemory_Init(size_t heap_size)
{
	(void)heap_size;

	if (g_memory_state.initialised) {
		return true;
	}

	g_memory_state.initialised = true;
	g_memory_state.allocated_bytes = 0;
	g_memory_state.block_count = 0;
	g_memory_state.head = NULL;

	return true;
}

/*
=============
BotMemory_SetLogCallback

Copies the diagnostic callback used by allocator validation and summaries.
=============
*/
void BotMemory_SetLogCallback(bot_memory_log_fn callback)
{
	g_memory_state.log_callback = callback;
}

/*
=============
BotMemory_SetAllocatorCallbacks

Copies the engine allocation callbacks supplied through the botlib import table.
=============
*/
void BotMemory_SetAllocatorCallbacks(bot_memory_allocate_fn allocate_callback,
	bot_memory_free_fn free_callback)
{
	g_memory_state.allocate_callback = allocate_callback;
	g_memory_state.free_callback = free_callback;
}

/*
=============
BotMemory_Shutdown

Releases every tracked allocation without emitting the optional usage summary.
=============
*/
void BotMemory_Shutdown(void)
{
	if (!g_memory_state.initialised) {
		return;
	}

	while (g_memory_state.head != NULL) {
		void *payload = g_memory_state.head->payload;
		FreeMemory(payload);
	}

	g_memory_state.head = NULL;
	g_memory_state.allocated_bytes = 0;
	g_memory_state.block_count = 0;
	g_memory_state.initialised = false;
}

/*
=============
BotMemory_LogSummary

Prints the two retail memory-manager usage counters.
=============
*/
void BotMemory_LogSummary(void)
{
	BotMemory_Log(BOT_MEMORY_LOG_INFO, "total botlib memory: %d KB\n",
		(int)(g_memory_state.allocated_bytes >> 10));
	BotMemory_Log(BOT_MEMORY_LOG_INFO, "total memory blocks: %d\n",
		g_memory_state.block_count);
}

/*
=============
GetMemory

Allocates a tracked block through the copied engine import callback.
=============
*/
void *GetMemory(size_t size)
{
	if (!g_memory_state.initialised) {
		if (!BotMemory_Init(0)) {
			return NULL;
		}
	}

	if (size > SIZE_MAX - sizeof(bot_memory_block_t)) {
		BotMemory_Log(BOT_MEMORY_LOG_ERROR, "GetMemory: size overflow (%zu bytes)\n", size);
		return NULL;
	}

	size_t total_size = sizeof(bot_memory_block_t) + size;
	if (total_size > (size_t)INT_MAX) {
		BotMemory_Log(BOT_MEMORY_LOG_ERROR, "GetMemory: size overflow (%zu bytes)\n", size);
		return NULL;
	}

	if (g_memory_state.allocated_bytes > SIZE_MAX - total_size) {
		BotMemory_Log(BOT_MEMORY_LOG_ERROR, "GetMemory: allocation counter overflow (%zu bytes)\n", size);
		return NULL;
	}

	bot_memory_allocate_fn allocate_callback = g_memory_state.allocate_callback;
	if (allocate_callback == NULL) {
		allocate_callback = BotMemory_DefaultAllocate;
	}

	bot_memory_block_t *block =
		(bot_memory_block_t *)allocate_callback((int)total_size);
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

/*
=============
GetClearedMemory

Allocates a tracked payload and clears the requested payload bytes.
=============
*/
void *GetClearedMemory(size_t size)
{
	void *ptr = GetMemory(size);
	if (ptr != NULL)
	{
		memset(ptr, 0, size);
	}
	return ptr;
}

/*
=============
FreeMemory

Validates and releases a tracked header through the copied engine callback.
=============
*/
void FreeMemory(void *ptr)
{
	bot_memory_block_t *block = BotMemory_BlockFromPointer(ptr, "FreeMemory");
	if (block == NULL) {
		return;
	}

	BotMemory_TrackDeallocation(block);
	bot_memory_free_fn free_callback = g_memory_state.free_callback;
	if (free_callback == NULL) {
		free_callback = BotMemory_DefaultFree;
	}
	free_callback(block);
}

/*
=============
MemoryByteSize

Returns the stored allocation size, including the allocator block header.
=============
*/
size_t MemoryByteSize(const void *ptr)
{
	bot_memory_block_t *block = BotMemory_BlockFromPointer(ptr, "MemoryByteSize");
	if (block == NULL) {
		return 0;
	}

	return block->total_size;
}

/*
=============
AvailableMemory

Retains the Quake III helper without imposing a Gladiator allocation budget.
=============
*/
int AvailableMemory(void)
{
	return INT_MAX;
}

/*
=============
BotMemory_TotalAllocated

Returns the current retail tracked-byte counter.
=============
*/
size_t BotMemory_TotalAllocated(void)
{
	return g_memory_state.allocated_bytes;
}

/*
=============
BotMemory_BlockCount

Returns the current retail tracked-block counter.
=============
*/
size_t BotMemory_BlockCount(void)
{
	return g_memory_state.block_count;
}

/*
=============
BotMemory_HeapCapacity

Reports that the compatibility layer has no synthetic aggregate heap limit.
=============
*/
size_t BotMemory_HeapCapacity(void)
{
	return SIZE_MAX;
}
