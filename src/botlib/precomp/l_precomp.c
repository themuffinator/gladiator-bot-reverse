#include "l_precomp.h"
#include "l_script.h"

#include <stdlib.h>

struct pc_source_s {
    int placeholder; // TODO: restore the complete precompiler source state.
};

void PC_InitLexer(void) {
    // TODO: rebuild punctuation tables and register builtin defines.
}

void PC_ShutdownLexer(void) {
    // TODO: release cached sources, defines, and punctuation tables.
}

pc_source_t *PC_LoadSourceFile(const char *path) {
    (void)path;
    // TODO: load the file into memory and create a pc_source_t instance.
    return NULL;
}

pc_source_t *PC_LoadSourceMemory(const char *name, const char *buffer, size_t buffer_size) {
    (void)name;
    (void)buffer;
    (void)buffer_size;
    // TODO: wrap the supplied buffer with ownership semantics identical to the
    // original botlib implementation.
    return NULL;
}

void PC_FreeSource(pc_source_t *source) {
    (void)source;
    // TODO: tear down diagnostics and cached token state for the supplied source.
}

int PC_ReadToken(pc_source_t *source, pc_token_t *token) {
    (void)source;
    (void)token;
    // TODO: implement the lexer's token extraction logic.
    return 0;
}

int PC_PeekToken(pc_source_t *source, pc_token_t *token) {
    (void)source;
    (void)token;
    // TODO: implement a non-destructive token peek.
    return 0;
}

void PC_UnreadToken(pc_source_t *source) {
    (void)source;
    // TODO: push the previous token back onto the source stack.
}

const pc_diagnostic_t *PC_GetDiagnostics(const pc_source_t *source) {
    (void)source;
    // TODO: return the diagnostic chain once the structure is restored.
    return NULL;
}
