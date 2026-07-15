#include "l_log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "l_libvar.h"
#include "botlib/interface/botlib_interface.h"

#define BOTLIB_MAX_PRINT_LEN 2048
#define BOTLIB_MAX_LOGNAME 1024

typedef struct botlib_log_state_s
{
	char filename[BOTLIB_MAX_LOGNAME];
	FILE *file;
	int write_count;
} botlib_log_state_t;

static botlib_log_state_t g_botlib_log_state;
static float g_botlib_log_time;

/*
=============
BotLib_FormatMessage

Formats an engine-print message into the fixed bridge buffer.
=============
*/
static void BotLib_FormatMessage(char *buffer, size_t buffer_size,
	const char *fmt, va_list args)
{
	if (buffer_size == 0)
	{
		return;
	}

	int written = vsnprintf(buffer, buffer_size, fmt, args);
	if (written < 0)
	{
		buffer[0] = '\0';
		return;
	}

	if ((size_t)written >= buffer_size)
	{
		buffer[buffer_size - 1] = '\0';
	}
}

/*
=============
BotLib_OutputToImport

Forwards a formatted print to the copied retail engine callback.
=============
*/
static void BotLib_OutputToImport(int priority, const char *message)
{
	const botlib_import_table_t *imports = BotInterface_GetImportTable();
	if (imports == NULL || imports->Print == NULL)
	{
		return;
	}

	imports->Print(priority, "%s", message);
}

/*
=============
BotLib_OutputDebug

Forwards a formatted developer print when that extension is available.
=============
*/
static void BotLib_OutputDebug(const char *message)
{
	const botlib_import_table_t *imports = BotInterface_GetImportTable();
	if (imports == NULL || imports->DPrint == NULL)
	{
		return;
	}

	imports->DPrint("%s", message);
}

/*
=============
BotLib_LogSetTime

Updates the botlib-global frame time used by timestamped log records.
=============
*/
void BotLib_LogSetTime(float time)
{
	g_botlib_log_time = time;
}

/*
=============
BotLib_LogOpen

Opens the retail diagnostic log when the log libvar is enabled.
=============
*/
void BotLib_LogOpen(const char *filename)
{
	if (LibVarValue("log", "0") == 0.0f)
	{
		return;
	}

	if (filename == NULL || filename[0] == '\0')
	{
		BotLib_Print(PRT_MESSAGE, "openlog <filename>\n");
		return;
	}

	if (g_botlib_log_state.file != NULL)
	{
		BotLib_Print(PRT_ERROR, "log file %s is already opened\n",
			g_botlib_log_state.filename);
		return;
	}

	FILE *file = fopen(filename, "wb");
	if (file == NULL)
	{
		BotLib_Print(PRT_ERROR, "can't open the log file %s\n", filename);
		return;
	}

	g_botlib_log_state.file = file;
	strncpy(g_botlib_log_state.filename, filename,
		sizeof(g_botlib_log_state.filename));
	g_botlib_log_state.filename[sizeof(g_botlib_log_state.filename) - 1] = '\0';
	BotLib_Print(PRT_MESSAGE, "Opened log %s\n",
		g_botlib_log_state.filename);
}

/*
=============
BotLib_LogClose

Closes the active retail diagnostic log.
=============
*/
void BotLib_LogClose(void)
{
	if (g_botlib_log_state.file == NULL)
	{
		return;
	}

	if (fclose(g_botlib_log_state.file) != 0)
	{
		BotLib_Print(PRT_ERROR, "can't close log file %s\n",
			g_botlib_log_state.filename);
		return;
	}

	g_botlib_log_state.file = NULL;
	BotLib_Print(PRT_MESSAGE, "Closed log %s\n",
		g_botlib_log_state.filename);
}

/*
=============
BotLib_LogShutdown

Closes the log during library teardown when necessary.
=============
*/
void BotLib_LogShutdown(void)
{
	if (g_botlib_log_state.file != NULL)
	{
		BotLib_LogClose();
	}
}

/*
=============
BotLib_LogWrite

Writes the caller's formatted record, appends CRLF, and flushes the stream.
=============
*/
void BotLib_LogWrite(const char *fmt, ...)
{
	if (g_botlib_log_state.file == NULL || fmt == NULL)
	{
		return;
	}

	va_list args;
	va_start(args, fmt);
	vfprintf(g_botlib_log_state.file, fmt, args);
	va_end(args);
	fprintf(g_botlib_log_state.file, "\r\n");
	fflush(g_botlib_log_state.file);
}

/*
=============
BotLib_LogWriteTimeStamped

Prefixes a record with the retail counter and unmodded frame-time fields.
=============
*/
void BotLib_LogWriteTimeStamped(const char *fmt, ...)
{
	if (g_botlib_log_state.file == NULL || fmt == NULL)
	{
		return;
	}

	int seconds = (int)g_botlib_log_time;
	int centiseconds = (int)(g_botlib_log_time * 100.0f) - seconds * 100;
	fprintf(g_botlib_log_state.file, "%d   %02d:%02d:%02d:%02d   ",
		g_botlib_log_state.write_count,
		(int)(g_botlib_log_time / 60.0f / 60.0f),
		(int)(g_botlib_log_time / 60.0f),
		seconds,
		centiseconds);

	va_list args;
	va_start(args, fmt);
	vfprintf(g_botlib_log_state.file, fmt, args);
	va_end(args);
	fprintf(g_botlib_log_state.file, "\r\n");
	g_botlib_log_state.write_count += 1;
	fflush(g_botlib_log_state.file);
}

/*
=============
BotLib_LogFile

Returns the active retail log stream.
=============
*/
FILE *BotLib_LogFile(void)
{
	return g_botlib_log_state.file;
}

/*
=============
BotLib_LogFlush

Flushes the active retail log stream.
=============
*/
void BotLib_LogFlush(void)
{
	if (g_botlib_log_state.file != NULL)
	{
		fflush(g_botlib_log_state.file);
	}
}

/*
=============
BotLib_Print

Formats and forwards a retail severity print to the engine import.
=============
*/
void BotLib_Print(int priority, const char *fmt, ...)
{
	if (fmt == NULL)
	{
		return;
	}

	char buffer[BOTLIB_MAX_PRINT_LEN];
	va_list args;
	va_start(args, fmt);
	BotLib_FormatMessage(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	BotLib_OutputToImport(priority, buffer);
}

/*
=============
BotLib_DPrint

Formats and forwards a developer print to the optional engine extension.
=============
*/
void BotLib_DPrint(const char *fmt, ...)
{
	if (fmt == NULL)
	{
		return;
	}

	char buffer[BOTLIB_MAX_PRINT_LEN];
	va_list args;
	va_start(args, fmt);
	BotLib_FormatMessage(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	BotLib_OutputDebug(buffer);
}
