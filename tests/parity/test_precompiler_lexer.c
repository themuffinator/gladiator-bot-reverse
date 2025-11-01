#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "botlib/precomp/l_precomp.h"
#include "botlib/precomp/l_script.h"

#include "../reference/precomp_lexer_expectations.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

static void assert_token_matches(const pc_token_snapshot_t *expected, const pc_token_t *actual)
{
    assert_non_null(expected);
    assert_non_null(actual);

    assert_int_equal(expected->type, actual->type);
    assert_int_equal(expected->subtype, actual->subtype);
    assert_int_equal(expected->line, actual->line);
    assert_string_equal(expected->lexeme, actual->string);
}

static void assert_diagnostics_match(const pc_diagnostic_snapshot_t *expected,
                                     size_t expected_count,
                                     const pc_diagnostic_t *head)
{
    size_t index = 0;
    const pc_diagnostic_t *cursor = head;

    while (index < expected_count && cursor != NULL) {
        assert_int_equal(expected[index].level, cursor->level);
        assert_int_equal(expected[index].line, cursor->line);
        assert_int_equal(expected[index].column, cursor->column);
        assert_string_equal(expected[index].message, cursor->message);

        cursor = cursor->next;
        ++index;
    }

    if (cursor == NULL) {
        assert_int_equal(expected_count, index);
    } else {
        fail_msg("diagnostic chain longer than expectation (index %zu)", index);
    }
}

static void drain_remaining_tokens(pc_source_t *source)
{
    pc_token_t token;
    while (PC_ReadToken(source, &token) > 0) {
        // Nothing to do; this helper ensures subsequent tests observe EOF.
    }
}

static void test_pc_loads_fw_items_and_matches_hlil_tokens(void **state)
{
    (void)state;

    PC_InitLexer();

    pc_source_t *source = PC_LoadSourceFile(g_fw_items_source_path);
    assert_non_null(source);

    for (size_t i = 0; i < ARRAY_SIZE(g_fw_items_token_expectations); ++i) {
        pc_token_t token;
        const int status = PC_ReadToken(source, &token);
        assert_int_equal(1, status);
        assert_token_matches(&g_fw_items_token_expectations[i], &token);
    }

    drain_remaining_tokens(source);

    assert_diagnostics_match(g_fw_items_diagnostics,
                             g_fw_items_diagnostics_count,
                             PC_GetDiagnostics(source));

    PC_FreeSource(source);
    PC_ShutdownLexer();
}

static void test_pc_loads_synonyms_and_matches_hlil_tokens(void **state)
{
    (void)state;

    PC_InitLexer();

    pc_source_t *source = PC_LoadSourceFile(g_synonyms_source_path);
    assert_non_null(source);

    for (size_t i = 0; i < ARRAY_SIZE(g_synonyms_token_expectations); ++i) {
        pc_token_t token;
        const int status = PC_ReadToken(source, &token);
        assert_int_equal(1, status);
        assert_token_matches(&g_synonyms_token_expectations[i], &token);
    }

    drain_remaining_tokens(source);

    assert_diagnostics_match(g_synonyms_diagnostics,
                             g_synonyms_diagnostics_count,
                             PC_GetDiagnostics(source));

    PC_FreeSource(source);
    PC_ShutdownLexer();
}

static void test_pc_peek_and_unread_mirror_hlil_behaviour(void **state)
{
    (void)state;

    PC_InitLexer();

    pc_source_t *source = PC_LoadSourceFile(g_fw_items_source_path);
    assert_non_null(source);

    pc_token_t peeked;
    assert_int_equal(1, PC_PeekToken(source, &peeked));

    assert_token_matches(&g_fw_items_token_expectations[0], &peeked);

    pc_token_t token;
    assert_int_equal(1, PC_ReadToken(source, &token));
    assert_token_matches(&g_fw_items_token_expectations[0], &token);

    PC_UnreadToken(source);

    assert_int_equal(1, PC_ReadToken(source, &token));
    assert_token_matches(&g_fw_items_token_expectations[0], &token);

    PC_FreeSource(source);
    PC_ShutdownLexer();
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pc_loads_fw_items_and_matches_hlil_tokens),
        cmocka_unit_test(test_pc_loads_synonyms_and_matches_hlil_tokens),
        cmocka_unit_test(test_pc_peek_and_unread_mirror_hlil_behaviour),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
