#ifndef TESTS_REFERENCE_PRECOMP_LEXER_EXPECTATIONS_H
#define TESTS_REFERENCE_PRECOMP_LEXER_EXPECTATIONS_H

#include "botlib/precomp/l_precomp.h"
#include "botlib/precomp/l_script.h"

#ifdef __cplusplus
extern "C" {
#endif

// Token snapshot captured from the Gladiator HLIL traces.  The production lexer
// is expected to mirror the Quake III precompiler contract, emitting identical
// token types, subtypes, and line metadata when lexing the sample assets.
typedef struct pc_token_snapshot_s {
    const char *lexeme;
    pc_token_type_t type;
    int subtype;
    int line;
} pc_token_snapshot_t;

typedef struct pc_diagnostic_snapshot_s {
    pc_error_level_t level;
    int line;
    int column;
    const char *message;
} pc_diagnostic_snapshot_t;

// Punctuation IDs mirrored from the historical precompiler.  The HLIL export
// shows that single character braces and parentheses share the same identifiers
// as Quake III Arena's botlib implementation.
#ifndef PC_PUNCTUATION_IDS
#define PC_PUNCTUATION_IDS
enum {
    P_PARENTHESESOPEN = 44,
    P_PARENTHESESCLOSE = 45,
    P_BRACEOPEN = 46,
    P_BRACECLOSE = 47,
    P_COLON = 42,
    P_COMMA = 40,
    P_SQBRACKETOPEN = 48,
    P_SQBRACKETCLOSE = 49,
};
#endif // PC_PUNCTUATION_IDS

static const char g_fw_items_source_path[] = "dev_tools/assets/fw_items.c";
static const char g_synonyms_source_path[] = "dev_tools/assets/syn.c";

static const pc_token_snapshot_t g_fw_items_token_expectations[] = {
    {"weight", TT_NAME, 6, 27},
    {"\"weapon_shotgun\"", TT_STRING, 14, 27},
    {"{", TT_PUNCTUATION, P_BRACEOPEN, 28},
    {"switch", TT_NAME, 6, 29},
    {"(", TT_PUNCTUATION, P_PARENTHESESOPEN, 29},
    {"INVENTORY_SHOTGUN", TT_NAME, 17, 29},
    {")", TT_PUNCTUATION, P_PARENTHESESCLOSE, 29},
    {"{", TT_PUNCTUATION, P_BRACEOPEN, 30},
    {"case", TT_NAME, 4, 31},
    {"1", TT_NUMBER, TT_INTEGER | TT_DECIMAL, 31},
    {":", TT_PUNCTUATION, P_COLON, 31},
    {"{", TT_PUNCTUATION, P_BRACEOPEN, 32},
    {"switch", TT_NAME, 6, 33},
    {"(", TT_PUNCTUATION, P_PARENTHESESOPEN, 33},
    {"INVENTORY_SHELLS", TT_NAME, 16, 33},
    {")", TT_PUNCTUATION, P_PARENTHESESCLOSE, 33},
    {"{", TT_PUNCTUATION, P_BRACEOPEN, 34},
};

static const pc_diagnostic_snapshot_t g_fw_items_diagnostics[] = {
    {0, 0, 0, NULL},
};
static const size_t g_fw_items_diagnostics_count = 0;

static const pc_token_snapshot_t g_synonyms_token_expectations[] = {
    {"CONTEXT_NEARBYITEM", TT_NAME, 18, 15},
    {"{", TT_PUNCTUATION, P_BRACEOPEN, 16},
    {"[", TT_PUNCTUATION, P_SQBRACKETOPEN, 18},
    {"(", TT_PUNCTUATION, P_PARENTHESESOPEN, 18},
    {"\"Body Armor\"", TT_STRING, 10, 18},
    {",", TT_PUNCTUATION, P_COMMA, 18},
    {"1", TT_NUMBER, TT_INTEGER | TT_DECIMAL, 18},
    {")", TT_PUNCTUATION, P_PARENTHESESCLOSE, 18},
    {",", TT_PUNCTUATION, P_COMMA, 18},
    {"(", TT_PUNCTUATION, P_PARENTHESESOPEN, 18},
    {"\"red armor\"", TT_STRING, 9, 18},
};

static const pc_diagnostic_snapshot_t g_synonyms_diagnostics[] = {
    {0, 0, 0, NULL},
};
static const size_t g_synonyms_diagnostics_count = 0;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // TESTS_REFERENCE_PRECOMP_LEXER_EXPECTATIONS_H
