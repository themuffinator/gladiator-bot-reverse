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

typedef enum pc_script_flags_e {
    SCFL_NOERRORS = 0x0001,
    SCFL_NOWARNINGS = 0x0002,
    SCFL_NOSTRINGWHITESPACES = 0x0004,
    SCFL_NOSTRINGESCAPECHARS = 0x0008,
    SCFL_PRIMITIVE = 0x0010,
    SCFL_NOBINARYNUMBERS = 0x0020,
    SCFL_NONUMBERVALUES = 0x0040,
} pc_script_flags_t;

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

typedef enum pc_punctuation_id_e {
    P_RSHIFT_ASSIGN = 1,
    P_LSHIFT_ASSIGN = 2,
    P_PARMS = 3,
    P_PRECOMPMERGE = 4,
    P_LOGIC_AND = 5,
    P_LOGIC_OR = 6,
    P_LOGIC_GEQ = 7,
    P_LOGIC_LEQ = 8,
    P_LOGIC_EQ = 9,
    P_LOGIC_UNEQ = 10,
    P_MUL_ASSIGN = 11,
    P_DIV_ASSIGN = 12,
    P_MOD_ASSIGN = 13,
    P_ADD_ASSIGN = 14,
    P_SUB_ASSIGN = 15,
    P_INC = 16,
    P_DEC = 17,
    P_BIN_AND_ASSIGN = 18,
    P_BIN_OR_ASSIGN = 19,
    P_BIN_XOR_ASSIGN = 20,
    P_RSHIFT = 21,
    P_LSHIFT = 22,
    P_POINTERREF = 23,
    P_CPP1 = 24,
    P_CPP2 = 25,
    P_MUL = 26,
    P_DIV = 27,
    P_MOD = 28,
    P_ADD = 29,
    P_SUB = 30,
    P_ASSIGN = 31,
    P_BIN_AND = 32,
    P_BIN_OR = 33,
    P_BIN_XOR = 34,
    P_BIN_NOT = 35,
    P_LOGIC_NOT = 36,
    P_LOGIC_GREATER = 37,
    P_LOGIC_LESS = 38,
    P_REF = 39,
    P_COMMA = 40,
    P_SEMICOLON = 41,
    P_COLON = 42,
    P_QUESTIONMARK = 43,
    P_PARENTHESESOPEN = 44,
    P_PARENTHESESCLOSE = 45,
    P_BRACEOPEN = 46,
    P_BRACECLOSE = 47,
    P_SQBRACKETOPEN = 48,
    P_SQBRACKETCLOSE = 49,
    P_BACKSLASH = 50,
    P_PRECOMP = 51,
    P_DOLLAR = 52,
} pc_punctuation_id_t;

typedef struct pc_punctuation_s {
    const char *p;
    int n;
    struct pc_punctuation_s *next;
} pc_punctuation_t;

typedef struct pc_token_s {
    char string[1024];
    pc_token_type_t type;
    int subtype;
    unsigned long int intvalue;
    long double floatvalue;
    const char *whitespace_p;
    const char *endwhitespace_p;
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
    char *whitespace_p;
    char *endwhitespace_p;
    int length;
    int line;
    int lastline;
    int tokenavailable;
    int flags;
    pc_punctuation_t *punctuations;
    pc_punctuation_t **punctuationtable;
    pc_diagnostic_t *diagnostics;
    pc_diagnostic_t *diagnostics_tail;
    const pc_diagnostic_t *last_source_diagnostic;
    pc_token_t token;
    pc_source_t *source;
    struct pc_script_s *next;
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
