#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <cmocka.h>

#include "botlib_precomp/l_precomp.h"
#include "botlib_precomp/l_script.h"

#include "../reference/precomp_lexer_expectations.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef PROJECT_SOURCE_DIR
#error "PROJECT_SOURCE_DIR must be defined for regression tests."
#endif

static void resolve_asset_path_or_skip(const char *relative_path, char *out, size_t out_size)
{
    assert_non_null(relative_path);
    assert_non_null(out);

    int written = snprintf(out, out_size, "%s/%s", PROJECT_SOURCE_DIR, relative_path);
    if (written <= 0 || (size_t)written >= out_size) {
        fail_msg("asset path overflow for %s", relative_path);
    }

    struct stat info;
    if (stat(out, &info) != 0) {
        cmocka_skip("Gladiator asset missing: %s (%s)", out, strerror(errno));
    }
}

static pc_source_t *load_fixture_source(const char *relative_path)
{
    char absolute_path[PATH_MAX];
    resolve_asset_path_or_skip(relative_path, absolute_path, sizeof(absolute_path));

    pc_source_t *source = PC_LoadSourceFile(absolute_path);
    if (source == NULL) {
        fail_msg("PC_LoadSourceFile failed for %s", absolute_path);
    }
    assert_non_null(source);
    return source;
}

static pc_token_t *read_token_or_null(pc_source_t *source, pc_token_t *token, int *status_out)
{
    assert_non_null(token);

    const int status = PC_ReadToken(source, token);
    if (status_out != NULL) {
        *status_out = status;
    }

    if (status <= 0) {
        return NULL;
    }

    return token;
}

static void assert_token_matches(size_t index,
                                 const pc_token_snapshot_t *expected,
                                 const pc_token_t *actual)
{
    assert_non_null(expected);
    assert_non_null(actual);

    if (expected->type != actual->type) {
        fail_msg("token[%zu] type mismatch: expected %d (%s) but read %d (%s)",
                 index,
                 expected->type,
                 expected->lexeme,
                 actual->type,
                 actual->string);
    }

    if (expected->subtype != actual->subtype) {
        fail_msg("token[%zu] subtype mismatch for '%s': expected %d got %d",
                 index,
                 expected->lexeme,
                 expected->subtype,
                 actual->subtype);
    }

    if (expected->line != actual->line) {
        fail_msg("token[%zu] line mismatch for '%s': expected %d got %d",
                 index,
                 expected->lexeme,
                 expected->line,
                 actual->line);
    }

    if (strcmp(expected->lexeme, actual->string) != 0) {
        fail_msg("token[%zu] lexeme mismatch: expected '%s' got '%s'",
                 index,
                 expected->lexeme,
                 actual->string);
    }
}

static void assert_diagnostics_match(const pc_diagnostic_snapshot_t *expected,
                                     size_t expected_count,
                                     const pc_diagnostic_t *head)
{
    size_t index = 0;
    const pc_diagnostic_t *cursor = head;

    while (cursor != NULL && index < expected_count) {
        if (expected[index].level != cursor->level) {
            fail_msg("diagnostic[%zu] level mismatch: expected %d got %d",
                     index,
                     expected[index].level,
                     cursor->level);
        }

        if (expected[index].line != cursor->line) {
            fail_msg("diagnostic[%zu] line mismatch: expected %d got %d",
                     index,
                     expected[index].line,
                     cursor->line);
        }

        if (expected[index].column != cursor->column) {
            fail_msg("diagnostic[%zu] column mismatch: expected %d got %d",
                     index,
                     expected[index].column,
                     cursor->column);
        }

        const char *expected_message = expected[index].message != NULL ? expected[index].message : "";
        const char *actual_message = cursor->message != NULL ? cursor->message : "";
        if (strcmp(expected_message, actual_message) != 0) {
            fail_msg("diagnostic[%zu] message mismatch: expected '%s' got '%s'",
                     index,
                     expected_message,
                     actual_message);
        }

        cursor = cursor->next;
        ++index;
    }

    if (cursor != NULL) {
        fail_msg("diagnostic chain longer than expectation (first extra line %d)", cursor->line);
    }

    if (index != expected_count) {
        fail_msg("diagnostic chain shorter than expectation: expected %zu entries got %zu",
                 expected_count,
                 index);
    }
}

static void expect_tokens_match_fixture(pc_source_t *source,
                                        const pc_token_snapshot_t *expectations,
                                        size_t expectation_count)
{
    for (size_t i = 0; i < expectation_count; ++i) {
        pc_token_t token;
        int status = 0;
        pc_token_t *actual = read_token_or_null(source, &token, &status);
        if (actual == NULL) {
            const char *reason = (status == 0) ? "end of stream" : "lexer error";
            fail_msg("token[%zu] %s while expecting '%s' (status %d)",
                     i,
                     reason,
                     expectations[i].lexeme,
                     status);
        }
        assert_non_null(actual);
        assert_token_matches(i, &expectations[i], actual);
    }

    pc_token_t trailing;
    int trailing_status = 0;
    pc_token_t *extra = read_token_or_null(source, &trailing, &trailing_status);
    if (extra != NULL) {
        fail_msg("token stream longer than expectation: unexpected '%s' on line %d",
                 trailing.string,
                 trailing.line);
    }

    if (trailing_status < 0) {
        fail_msg("token stream terminated with lexer error after %zu tokens (status %d)",
                 expectation_count,
                 trailing_status);
    }
}

static void assert_fixture_diagnostics(pc_source_t *source,
                                       const pc_diagnostic_snapshot_t *expected,
                                       size_t expected_count)
{
    const pc_diagnostic_t *head = PC_GetDiagnostics(source);
    assert_diagnostics_match(expected, expected_count, head);
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

    assert_fixture_diagnostics(source,
                               g_fw_items_diagnostics,
                               g_fw_items_diagnostics_count);

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

    assert_fixture_diagnostics(source,
                               g_synonyms_diagnostics,
                               g_synonyms_diagnostics_count);

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

    assert_token_matches(0, &g_fw_items_token_expectations[0], &peeked);

    pc_token_t token;
    int status = 0;
    pc_token_t *first = read_token_or_null(source, &token, &status);
    if (first == NULL) {
        const char *reason = (status == 0) ? "end of stream" : "lexer error";
        fail_msg("PC_ReadToken failed after peek: %s (status %d)", reason, status);
    }
    assert_non_null(first);
    assert_token_matches(0, &g_fw_items_token_expectations[0], first);

    PC_UnreadToken(source);

    status = 0;
    pc_token_t *second = read_token_or_null(source, &token, &status);
    if (second == NULL) {
        const char *reason = (status == 0) ? "end of stream" : "lexer error";
        fail_msg("PC_ReadToken failed after unread: %s (status %d)", reason, status);
    }
    assert_non_null(second);
    assert_token_matches(0, &g_fw_items_token_expectations[0], second);

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
