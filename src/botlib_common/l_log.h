#ifndef BOTLIB_COMMON_L_LOG_H
#define BOTLIB_COMMON_L_LOG_H

#include <stdio.h>

#ifndef PRT_MESSAGE
#define PRT_MESSAGE 1
#define PRT_WARNING 2
#define PRT_ERROR   3
#define PRT_FATAL   4
#define PRT_EXIT    5
#define PRT_DEVELOPER 6
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opens a botlib diagnostic log file if the \"log\" libvar is enabled.
 */
void BotLib_LogOpen(const char *filename);

/**
 * Flushes and closes the active diagnostic log if one is open.
 */
void BotLib_LogClose(void);

/**
 * Ensures the logging subsystem releases any resources it owns. Safe to call
 * even if no log file was created.
 */
void BotLib_LogShutdown(void);

/**
 * Writes the formatted message to the active log without adding any timestamp
 * information. Messages are flushed immediately so external tools can tail the
 * file during long runs.
 */
void BotLib_LogWrite(const char *fmt, ...);

/**
 * Writes a message that is prefixed with a monotonically increasing counter and
 * a clock based timestamp to aid debugging of long running sessions.
 */
void BotLib_LogWriteTimeStamped(const char *fmt, ...);

/**
 * Returns the FILE pointer backing the active log. NULL is returned when
 * logging is disabled or no log file has been opened yet.
 */
FILE *BotLib_LogFile(void);

/**
 * Flushes the current log file to disk if logging is active.
 */
void BotLib_LogFlush(void);

/**
 * Sends a message to the engine provided printer with the supplied severity
 * level. The format string follows printf semantics.
 */
void BotLib_Print(int priority, const char *fmt, ...);

/**
 * Emits a debug print message that is only routed to the developer console in
 * debug builds of the engine.
 */
void BotLib_DPrint(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif // BOTLIB_COMMON_L_LOG_H
