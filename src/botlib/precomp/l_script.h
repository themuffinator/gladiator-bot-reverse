#ifndef BOTLIB_PRECOMP_L_SCRIPT_H
#define BOTLIB_PRECOMP_L_SCRIPT_H

#include "l_precomp.h"

// -----------------------------------------------------------------------------
//  Lexer Token Model
// -----------------------------------------------------------------------------
// Historical Quake III sources show that the lexer emits five token categories:
//   TT_STRING       - A quoted string literal with escape processing controlled
//                     by SCFL_NOSTRINGESCAPECHARS.  The subtype stores the
//                     string length for fast buffer reservations.
//   TT_LITERAL      - Single character literals (',' ';' etc.) whose subtype is
//                     the ASCII value of the literal.  Useful when the parser
//                     must differentiate punctuation that is not part of the
//                     dedicated punctuation table.
//   TT_NUMBER       - Numeric constants.  Subtype flags encode format details
//                     (decimal, hex, octal, optional binary) and qualifiers such
//                     as TT_FLOAT, TT_INTEGER, TT_UNSIGNED or TT_LONG.
//   TT_NAME         - Identifiers used for macro names or symbolic keys inside
//                     the bot character/weapon files.  Subtype again captures
//                     the string length.
//   TT_PUNCTUATION  - Multi-character punctuation (&&, ||, >=, +=, ...).  The
//                     subtype is a stable integer ID that maps to the
//                     punctuation table defined when the lexer boots.
//
// Error handling flags align with the script_t::flags bits in the original
// code:
//   SCFL_NOERRORS              - Suppress hard errors and continue tokenising.
//   SCFL_NOWARNINGS            - Filter warnings; diagnostics are still queued
//                                but flagged as suppressed.
//   SCFL_NOSTRINGWHITESPACES   - Disable whitespace folding within strings.
//   SCFL_NOSTRINGESCAPECHARS   - Treat backslashes as literal characters.
//   SCFL_PRIMITIVE             - Skip preprocessor behaviour when loading the
//                                simplest data-driven files.
//   SCFL_NOBINARYNUMBERS       - Disallow 0b/0B prefixes when not needed.
//   SCFL_NONUMBERVALUES        - Skip populating the int/float value cache.
//
// File I/O mirrors the precompiler contract: PS_LoadScriptFile reads the entire
// script into memory before lexing, while PS_LoadScriptMemory wraps buffers
// provided by the caller.  The parser tracks current and previous positions so
// callers can surface accurate line/column numbers in error messages.
// -----------------------------------------------------------------------------

typedef enum pc_token_type_e {
    TT_STRING = 1,
    TT_LITERAL = 2,
    TT_NUMBER = 3,
    TT_NAME = 4,
    TT_PUNCTUATION = 5,
} pc_token_type_t;

typedef enum pc_number_subtype_e {
    TT_DECIMAL = 0x0008,
    TT_HEX = 0x0100,
    TT_OCTAL = 0x0200,
    TT_BINARY = 0x0400,
    TT_FLOAT = 0x0800,
    TT_INTEGER = 0x1000,
    TT_LONG = 0x2000,
    TT_UNSIGNED = 0x4000,
} pc_number_subtype_t;

typedef struct pc_token_s {
    char string[1024];
    pc_token_type_t type;
    int subtype;
    unsigned long int intvalue;
    long double floatvalue;
    int line;
    int linescrossed;
    struct pc_token_s *next;
} pc_token_t;

typedef struct pc_script_s {
    char filename[1024];
    char *buffer;
    char *script_p;
    char *end_p;
    char *lastscript_p;
    int length;
    int line;
    int lastline;
    int tokenavailable;
    int flags;
    pc_diagnostic_t *diagnostics;
    pc_token_t token;
} pc_script_t;

// Creates a new script wrapper around an already preprocessed source.  The
// implementation will duplicate metadata and snapshot the token stream so the
// higher level botlib modules can iterate without touching the precompiler's
// global queues.
pc_script_t *PS_CreateScriptFromSource(pc_source_t *source);

// Destroys a script wrapper created by PS_CreateScriptFromSource.
void PS_FreeScript(pc_script_t *script);

// Reads the next token from the script wrapper.  Internally this proxies to
// PC_ReadToken and copies the result into script->token for later access.
int PS_ReadToken(pc_script_t *script, pc_token_t *token);

// Convenience helpers used by the original loaders to enforce schema-specific
// expectations.  They will be filled in when the parser logic is implemented.
int PS_ExpectTokenString(pc_script_t *script, const char *string);
int PS_ExpectTokenType(pc_script_t *script, int type, int subtype, pc_token_t *token);
int PS_SkipUntilString(pc_script_t *script, const char *string);

#endif // BOTLIB_PRECOMP_L_SCRIPT_H
