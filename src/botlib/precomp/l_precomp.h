#ifndef BOTLIB_PRECOMP_L_PRECOMP_H
#define BOTLIB_PRECOMP_L_PRECOMP_H

#include <stddef.h>

#ifndef MAX_TOKEN
#define MAX_TOKEN 1024
#endif

// -----------------------------------------------------------------------------
//  Precompiler Overview
// -----------------------------------------------------------------------------
// The Quake III bot library relies on a lightweight C-style precompiler to
// process the behavioural scripts that drive arena bots. HLIL decompilation of
// the original game and the id Software GPL drop both show that the module is
// responsible for:
//   * Tracking global "source files" that encapsulate a script's buffer,
//     filename metadata, and lexer flags before any AI-specific parsing occurs.
//   * Implementing a macro-style preprocessor that resolves #defines, evaluates
//     conditional compilation blocks, and merges include files before handing
//     the token stream to higher level parsers.
//   * Managing diagnostics: warnings and errors are queued with source line
//     numbers so the caller can decide whether to abort loading or continue
//     with defaulted behaviour.  The script flags mirror the historical
//     behaviour where callers can silence warnings (SCFL_NOWARNINGS) or bypass
//     strict error conditions (SCFL_NOERRORS).
//
// The token stream produced by the precompiler mirrors the layout documented in
// l_script.h; see that header for the type and subtype matrix.  The precompiler
// exposes convenience wrappers that allocate script handles, feed tokens to the
// parser, and log recoverable issues such as unterminated strings, recursive
// includes, or invalid numeric constants.
//
// File I/O mirrors the original code path uncovered via HLIL: scripts are read
// entirely into memory using the shared filesystem abstraction, UTF-8/ASCII
// decoded, and then wrapped in a pc_source_t structure.  Re-parsing the same
// file reuses the cached buffer when possible to reduce disk churn.  Loading
// from a caller supplied memory block is also supported, enabling the AI
// subsystem to feed dynamically generated preprocessing data during runtime.
// -----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations keep the interface dependency light.  The actual
// definitions live in the implementation once the lexer/parser is restored.
typedef struct pc_token_s pc_token_t;
typedef struct pc_source_s pc_source_t;

typedef enum pc_error_level_e {
    PC_ERROR_LEVEL_WARNING,
    PC_ERROR_LEVEL_ERROR,
    PC_ERROR_LEVEL_FATAL,
} pc_error_level_t;

// Records a diagnostic message emitted during preprocessing.  The historical
// implementation chained these records per-source and flushed them when the
// caller requested the next token.
typedef struct pc_diagnostic_s {
    pc_error_level_t level;
    int line;
    int column;
    const char *message; // TODO: switch to a dedicated small string buffer.
    struct pc_diagnostic_s *next;
} pc_diagnostic_t;

// Creates the global tables that map punctuation sequences to token subtypes
// and registers the built-in defines used by the Quake III AI scripts.
void PC_InitLexer(void);

// Releases all cached sources and diagnostic chains.
void PC_ShutdownLexer(void);

// Loads a script file into memory and returns a source handle that can be fed
// into the higher level parsers.  The returned pointer is owned by the caller
// and must be freed with PC_FreeSource.
pc_source_t *PC_LoadSourceFile(const char *path);

// Wraps an existing buffer in a source handle.  The caller retains ownership of
// the buffer and is responsible for ensuring its lifetime exceeds that of the
// source handle.
pc_source_t *PC_LoadSourceMemory(const char *name,
                                  const char *buffer,
                                  size_t buffer_size);

// Releases a source handle and any diagnostics queued on it.
void PC_FreeSource(pc_source_t *source);

// Reads the next token from the preprocessed stream.  Returns 1 when a token is
// available, 0 on end-of-file, or a negative value when a fatal diagnostic is
// generated.  Non-fatal warnings are enqueued on the source handle and do not
// change the return value.
int PC_ReadToken(pc_source_t *source, pc_token_t *token);

// Peeks the next token without consuming it.  Historically used by the bot
// configuration loader when probing for optional blocks.
int PC_PeekToken(pc_source_t *source, pc_token_t *token);

// Pushes the last token read back into the stream.
void PC_UnreadToken(pc_source_t *source, pc_token_t *token);

// Returns the head of the diagnostic chain built while lexing the supplied
// source.  Callers can iterate the list and display the messages using their
// own logging facilities.
const pc_diagnostic_t *PC_GetDiagnostics(const pc_source_t *source);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // BOTLIB_PRECOMP_L_PRECOMP_H
