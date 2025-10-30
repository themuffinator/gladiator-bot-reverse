#ifndef BOTLIB_COMMON_L_MEMORY_H
#define BOTLIB_COMMON_L_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOT_MEMORY_DEFAULT_HEAP_SIZE (8u * 1024u * 1024u)

#define BOT_MEMORY_LOG_INFO 1
#define BOT_MEMORY_LOG_WARNING 2
#define BOT_MEMORY_LOG_ERROR 3

typedef void (*bot_memory_log_fn)(int level, const char *fmt, va_list args);

bool BotMemory_Init(size_t heap_size);
void BotMemory_SetLogCallback(bot_memory_log_fn callback);
void BotMemory_Shutdown(void);
void BotMemory_LogSummary(void);

void *GetMemory(size_t size);
void *GetClearedMemory(size_t size);
void FreeMemory(void *ptr);
size_t MemoryByteSize(const void *ptr);
int AvailableMemory(void);

size_t BotMemory_TotalAllocated(void);
size_t BotMemory_HeapCapacity(void);

#ifdef __cplusplus
}
#endif

#endif // BOTLIB_COMMON_L_MEMORY_H
