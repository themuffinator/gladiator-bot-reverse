#include "l_script.h"

#include <stdlib.h>

pc_script_t *PS_CreateScriptFromSource(pc_source_t *source) {
    (void)source;
    // TODO: mirror the original PS_CreateScriptFromSource behaviour once
    // l_precomp.c exposes the full pc_source_t structure.
    return NULL;
}

void PS_FreeScript(pc_script_t *script) {
    (void)script;
    // TODO: release buffers owned by the script wrapper.
}

int PS_ReadToken(pc_script_t *script, pc_token_t *token) {
    (void)script;
    (void)token;
    // TODO: proxy to PC_ReadToken and preserve bookkeeping data.
    return 0;
}

int PS_ExpectTokenString(pc_script_t *script, const char *string) {
    (void)script;
    (void)string;
    // TODO: implement the expectation helper with detailed diagnostics.
    return 0;
}

int PS_ExpectTokenType(pc_script_t *script, int type, int subtype, pc_token_t *token) {
    (void)script;
    (void)type;
    (void)subtype;
    (void)token;
    // TODO: implement the expectation helper with detailed diagnostics.
    return 0;
}

int PS_SkipUntilString(pc_script_t *script, const char *string) {
    (void)script;
    (void)string;
    // TODO: consume tokens until the target string is found.
    return 0;
}
