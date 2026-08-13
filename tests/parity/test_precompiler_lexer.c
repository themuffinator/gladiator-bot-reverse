#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <cmocka.h>

#if defined(_WIN32)
#include <direct.h>
#define TEST_MKDIR(path) _mkdir(path)
#define TEST_RMDIR(path) _rmdir(path)
#else
#include <unistd.h>
#define TEST_MKDIR(path) mkdir((path), 0777)
#define TEST_RMDIR(path) rmdir(path)
#endif

#include "botlib/common/l_crc.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_memory.h"
#include "botlib/precomp/l_precomp.h"
#include "botlib/precomp/l_script.h"

#include "../reference/precomp_lexer_expectations.h"

#ifndef cmocka_skip
#define cmocka_skip(...) skip()
#endif

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

/*
=============
test_pc_rejects_numeric_token_pasting

Pins the retail token-pasting failure for two numeric tokens.
=============
*/
static void test_pc_rejects_numeric_token_pasting(void **state)
{
	(void)state;

	PC_InitLexer();

	const char script[] = "#define MERGE(a,b) a ## b\nMERGE(12, 34)\n";
	pc_source_t *source = PC_LoadSourceMemory("merge_numbers", script, strlen(script));
	assert_non_null(source);

	pc_token_t token;
	assert_int_equal(0, PC_ReadToken(source, &token));

	PC_FreeSource(source);
	PC_ShutdownLexer();
}

/*
=============
test_pc_stringizes_macro_tokens_without_whitespace

Pins the retail stringizer's direct token-text concatenation at 0x10039a70.
=============
*/
static void test_pc_stringizes_macro_tokens_without_whitespace(void **state)
{
	(void)state;

	PC_InitLexer();

	const char script[] = "#define AS_TEXT(value) # value\nAS_TEXT(foo + bar)\n";
	pc_source_t *source = PC_LoadSourceMemory("stringize_tokens", script,
		strlen(script));
	assert_non_null(source);

	pc_token_t token;
	assert_int_equal(1, PC_ReadToken(source, &token));
	assert_int_equal(TT_STRING, token.type);
	assert_string_equal("\"foo+bar\"", token.string);
	assert_null(token.whitespace_p);
	assert_null(token.endwhitespace_p);
	assert_int_equal(0, PC_ReadToken(source, &token));

	PC_FreeSource(source);
	PC_ShutdownLexer();
}

/*
=============
test_pc_builtin_macros_preserve_name_token_metadata

Pins retail expansion's text-only update for the built-in macro token copy.
=============
*/
static void test_pc_builtin_macros_preserve_name_token_metadata(void **state)
{
	(void)state;

	PC_InitLexer();

	const char script[] = "__LINE__\n__FILE__\n";
	pc_source_t *source = PC_LoadSourceMemory("builtin_metadata", script,
		strlen(script));
	assert_non_null(source);
	PC_AddBuiltinDefines(source);

	pc_token_t token;
	assert_int_equal(1, PC_ReadToken(source, &token));
	assert_int_equal(TT_NAME, token.type);
	assert_int_equal(8, token.subtype);
	assert_string_equal("1", token.string);

	assert_int_equal(1, PC_ReadToken(source, &token));
	assert_int_equal(TT_NAME, token.type);
	assert_int_equal(8, token.subtype);
	assert_string_equal("builtin_metadata", token.string);
	assert_int_equal(0, PC_ReadToken(source, &token));

	PC_FreeSource(source);
	PC_ShutdownLexer();
}

/*
=============
test_pc_keeps_macro_adjacent_quoted_strings_separate

Pins retail PC_ReadToken, which does not join strings across a macro expansion.
=============
*/
static void test_pc_keeps_macro_adjacent_quoted_strings_separate(void **state)
{
	(void)state;

	PC_InitLexer();

	const char script[] = "#define FIRST \"first\"\nFIRST \"second\"";
	pc_source_t *source = PC_LoadSourceMemory("macro_adjacent_strings", script,
		strlen(script));
	assert_non_null(source);

	pc_token_t token;
	assert_int_equal(1, PC_ReadToken(source, &token));
	assert_int_equal(TT_STRING, token.type);
	assert_string_equal("\"first\"", token.string);

	assert_int_equal(1, PC_ReadToken(source, &token));
	assert_int_equal(TT_STRING, token.type);
	assert_string_equal("\"second\"", token.string);
	assert_int_equal(0, PC_ReadToken(source, &token));

	PC_FreeSource(source);
	PC_ShutdownLexer();
}

/*
=============
test_pc_concatenates_physical_adjacent_quoted_strings

Pins retail PS_ReadString, which combines adjacent source string literals.
=============
*/
static void test_pc_concatenates_physical_adjacent_quoted_strings(void **state)
{
	(void)state;

	PC_InitLexer();

	const char script[] = "\"first\" \"second\"";
	pc_source_t *source = PC_LoadSourceMemory("physical_adjacent_strings", script,
		strlen(script));
	assert_non_null(source);

	pc_token_t token;
	assert_int_equal(1, PC_ReadToken(source, &token));
	assert_int_equal(TT_STRING, token.type);
	assert_string_equal("\"firstsecond\"", token.string);
	assert_int_equal(0, PC_ReadToken(source, &token));

	PC_FreeSource(source);
	PC_ShutdownLexer();
}

/*
=============
test_pc_expands_chat_named_define

Ensures chat-looking identifiers still follow the retail define expansion path.
=============
*/
static void test_pc_expands_chat_named_define(void **state)
{
	(void)state;

	PC_InitLexer();

	const char script[] = "#define VICTIM 7\nVICTIM\n";
	pc_source_t *source = PC_LoadSourceMemory("chat_named_define", script, strlen(script));
	assert_non_null(source);

	pc_token_t token;
	assert_int_equal(1, PC_ReadToken(source, &token));
	assert_int_equal(TT_NUMBER, token.type);
	assert_int_equal(TT_DECIMAL | TT_INTEGER, token.subtype);
	assert_string_equal("7", token.string);
	assert_int_equal(7, (int)token.intvalue);
	assert_float_equal(7.0f, token.floatvalue, 0.0f);
	assert_int_equal(0, PC_ReadToken(source, &token));

	PC_FreeSource(source);
	PC_ShutdownLexer();
}

/*
=============
test_pc_reports_full_quoted_string_subtype

Ensures quoted string subtypes include the leading and trailing quotes.
=============
*/
static void test_pc_reports_full_quoted_string_subtype(void **state)
{
	(void)state;

	PC_InitLexer();

	const char script[] = "\"abc\"";
	pc_source_t *source = PC_LoadSourceMemory("quoted_string_subtype", script, strlen(script));
	assert_non_null(source);

	pc_token_t token;
	assert_int_equal(1, PC_ReadToken(source, &token));
	assert_int_equal(TT_STRING, token.type);
	assert_string_equal("\"abc\"", token.string);
	assert_int_equal(5, token.subtype);
	assert_int_equal(0, PC_ReadToken(source, &token));

	PC_FreeSource(source);
	PC_ShutdownLexer();
}

/*
=============
test_pc_preserves_memory_block_comment_token_boundary

Ensures in-memory block comments remain lexical whitespace between names.
=============
*/
static void test_pc_preserves_memory_block_comment_token_boundary(void **state)
{
	(void)state;

	PC_InitLexer();

	const char script[] = "left/**/right";
	pc_source_t *source = PC_LoadSourceMemory("block_comment_separator", script, strlen(script));
	assert_non_null(source);

	pc_token_t token;
	assert_int_equal(1, PC_ReadToken(source, &token));
	assert_int_equal(TT_NAME, token.type);
	assert_int_equal(4, token.subtype);
	assert_string_equal("left", token.string);

	assert_int_equal(1, PC_ReadToken(source, &token));
	assert_int_equal(TT_NAME, token.type);
	assert_int_equal(5, token.subtype);
	assert_string_equal("right", token.string);

	assert_int_equal(0, PC_ReadToken(source, &token));

	PC_FreeSource(source);
	PC_ShutdownLexer();
}

/*
=============
test_pc_dollar_evaluators_preserve_retail_token_caches

Pins the raw $eval emitters: integer results retain their evaluated cache, and
negative float results are emitted as a separate minus token plus an
absolute-text number whose caches retain the negative value.
=============
*/
static void test_pc_dollar_evaluators_preserve_retail_token_caches(void **state)
{
	(void)state;

	PC_InitLexer();

	const char script[] = "$evalint(2 + 3)\n$evalfloat(-1.25)\n";
	pc_source_t *source = PC_LoadSourceMemory("dollar_eval_tokens", script,
		strlen(script));
	assert_non_null(source);

	pc_token_t token;
	assert_int_equal(1, PC_ReadToken(source, &token));
	assert_int_equal(TT_NUMBER, token.type);
	assert_int_equal(TT_INTEGER | TT_LONG | TT_DECIMAL, token.subtype);
	assert_string_equal("5", token.string);
	assert_int_equal(5, (int)token.intvalue);
	assert_float_equal(5.0f, token.floatvalue, 0.0f);

	assert_int_equal(1, PC_ReadToken(source, &token));
	assert_int_equal(TT_PUNCTUATION, token.type);
	assert_int_equal(P_SUB, token.subtype);
	assert_string_equal("-", token.string);

	assert_int_equal(1, PC_ReadToken(source, &token));
	assert_int_equal(TT_NUMBER, token.type);
	assert_int_equal(TT_FLOAT | TT_LONG | TT_DECIMAL, token.subtype);
	assert_string_equal("1.25", token.string);
	assert_int_equal(-1, (int)token.intvalue);
	assert_float_equal(-1.25f, token.floatvalue, 0.0f);
	assert_int_equal(0, PC_ReadToken(source, &token));

	PC_FreeSource(source);
	PC_ShutdownLexer();
}

/*
=============
count_source_diagnostics

Returns the number of diagnostics queued on a source.
=============
*/
static size_t count_source_diagnostics(const pc_source_t *source)
{
	size_t count = 0;

	for (const pc_diagnostic_t *cursor = PC_GetDiagnostics(source);
		cursor != NULL;
		cursor = cursor->next)
	{
		++count;
	}

	return count;
}

/*
=============
assert_single_diagnostic

Requires exactly one queued diagnostic with the given level and message.
=============
*/
static void assert_single_diagnostic(const pc_source_t *source,
	pc_error_level_t level,
	const char *message)
{
	const pc_diagnostic_t *head = PC_GetDiagnostics(source);

	assert_non_null(head);
	assert_int_equal((int)head->level, (int)level);
	assert_non_null(head->message);
	assert_string_equal(head->message, message);
	assert_int_equal((int)count_source_diagnostics(source), 1);
}

/*
=============
test_pc_define_keeps_self_referencing_body_tokens

Pins retail PC_Directive_define (0x1003b128), whose body loop copies every token
unconditionally.  The Q3 "recursive define (removed recursion)" guard does not
exist in the shipped DLL, so a self-naming body token is stored, not dropped.
=============
*/
static void test_pc_define_keeps_self_referencing_body_tokens(void **state)
{
	(void)state;

	PC_InitLexer();

	//a body of exactly the define's own name must be kept, and must not
	//produce a diagnostic
	const char plain[] = "#define SELF SELF\nAFTER\n";
	pc_source_t *source = PC_LoadSourceMemory("self_define_plain", plain,
		strlen(plain));
	assert_non_null(source);

	pc_token_t token;
	assert_int_equal(1, PC_ReadToken(source, &token));
	assert_int_equal(TT_NAME, token.type);
	assert_string_equal("AFTER", token.string);
	assert_int_equal(0, PC_ReadToken(source, &token));
	assert_int_equal((int)count_source_diagnostics(source), 0);
	PC_FreeSource(source);

	//the retained leading token is observable through the misplaced-##
	//check at 0x1003b177: with the body kept, SELF (not ##) leads the list,
	//so the define is accepted instead of being rejected
	const char merged[] = "#define SELF SELF ## TAIL\nAFTER\n";
	source = PC_LoadSourceMemory("self_define_merge", merged, strlen(merged));
	assert_non_null(source);

	assert_int_equal(1, PC_ReadToken(source, &token));
	assert_int_equal(TT_NAME, token.type);
	assert_string_equal("AFTER", token.string);
	assert_int_equal(0, PC_ReadToken(source, &token));
	assert_int_equal((int)count_source_diagnostics(source), 0);

	PC_FreeSource(source);
	PC_ShutdownLexer();
}

/*
=============
test_pc_if_rejects_increment_operator_as_invalid

Pins retail PC_EvaluateTokens: lookup_table_1003c318[0x0b]/[0x0c] route P_INC and
P_DEC to the shared "invalid operator" arm at 0x1003bdb4, which sets the error
flag and aborts the load instead of warning and carrying on.
=============
*/
static void test_pc_if_rejects_increment_operator_as_invalid(void **state)
{
	(void)state;

	PC_InitLexer();

	const char script[] = "#if 1 ++ 2\nINSIDE\n#endif\nAFTER\n";
	pc_source_t *source = PC_LoadSourceMemory("if_increment", script,
		strlen(script));
	assert_non_null(source);

	pc_token_t token;
	assert_int_equal(0, PC_ReadToken(source, &token));
	assert_single_diagnostic(source,
		PC_ERROR_LEVEL_ERROR,
		"invalid operator ++ in #if/#elif");

	PC_FreeSource(source);
	PC_ShutdownLexer();
}

/*
=============
test_ps_read_number_classifies_look_ahead_character

Pins retail PS_ReadNumber (0x1003ee37), which stores the character before
classifying the next one, so a leading '.' never sets TT_FLOAT.
=============
*/
static void test_ps_read_number_classifies_look_ahead_character(void **state)
{
	(void)state;

	PC_InitLexer();

	const char script[] = ".5 0.5\n";
	pc_source_t *source = PC_LoadSourceMemory("leading_dot_number", script,
		strlen(script));
	assert_non_null(source);

	//".5": the dot is consumed unclassified, so the token is a decimal
	//integer whose NumberValue accumulates ('.' - '0') == -2 first
	pc_token_t token;
	assert_int_equal(1, PC_ReadToken(source, &token));
	assert_int_equal(TT_NUMBER, token.type);
	assert_int_equal(TT_DECIMAL | TT_INTEGER, token.subtype);
	assert_string_equal(".5", token.string);
	assert_int_equal(0xFFFFFFF1u, (unsigned int)token.intvalue);

	//"0.5": the dot is seen as a look-ahead character, so TT_FLOAT is set
	//and the leading zero still marks the token octal
	assert_int_equal(1, PC_ReadToken(source, &token));
	assert_int_equal(TT_NUMBER, token.type);
	assert_int_equal(TT_OCTAL | TT_FLOAT, token.subtype);
	assert_string_equal("0.5", token.string);
	assert_float_equal(0.5f, (float)token.floatvalue, 0.0f);
	assert_int_equal(0, PC_ReadToken(source, &token));

	PC_FreeSource(source);
	PC_ShutdownLexer();
}

/*
=============
test_ps_read_number_consumes_unguarded_lowercase_suffixes

Pins retail PS_ReadNumber's suffix loop (0x1003eed7 / 0x1003eeeb), which
short-circuits on lowercase 'l' and 'u' without consulting the subtype.  The
later id "bk001204 - brackets" patch must not be applied here.
=============
*/
static void test_ps_read_number_consumes_unguarded_lowercase_suffixes(
	void **state)
{
	(void)state;

	PC_InitLexer();

	const char script[] = "1.0u 5ll 7uu\n";
	pc_source_t *source = PC_LoadSourceMemory("number_suffixes", script,
		strlen(script));
	assert_non_null(source);

	//lowercase 'u' is taken even though TT_FLOAT is already set
	pc_token_t token;
	assert_int_equal(1, PC_ReadToken(source, &token));
	assert_int_equal(TT_NUMBER, token.type);
	assert_int_equal(TT_DECIMAL | TT_FLOAT | TT_UNSIGNED, token.subtype);
	assert_string_equal("1.0", token.string);

	//both lowercase 'l' suffixes are taken even though TT_LONG is set
	assert_int_equal(1, PC_ReadToken(source, &token));
	assert_int_equal(TT_NUMBER, token.type);
	assert_int_equal(TT_DECIMAL | TT_INTEGER | TT_LONG, token.subtype);
	assert_string_equal("5", token.string);

	//and both lowercase 'u' suffixes likewise
	assert_int_equal(1, PC_ReadToken(source, &token));
	assert_int_equal(TT_NUMBER, token.type);
	assert_int_equal(TT_DECIMAL | TT_INTEGER | TT_UNSIGNED, token.subtype);
	assert_string_equal("7", token.string);
	assert_int_equal(0, PC_ReadToken(source, &token));

	PC_FreeSource(source);
	PC_ShutdownLexer();
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
		assert_token_matches(i, &g_fw_items_token_expectations[i], &token);
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
		assert_token_matches(i, &g_synonyms_token_expectations[i], &token);
	}

	for (;;)
	{
		pc_token_t token;
		if (PC_ReadToken(source, &token) <= 0)
		{
			break;
		}
	}

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

	PC_UnreadToken(source, &token);

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

typedef struct test_pak_entry_s
{
	const char *name;
	const char *contents;
	uint32_t offset;
	uint32_t length;
} test_pak_entry_t;

/*
=============
write_test_pak_u32

Writes one little-endian PAK integer without relying on host endianness.
=============
*/
static void write_test_pak_u32(FILE *file, uint32_t value)
{
	unsigned char bytes[4];

	bytes[0] = (unsigned char)(value & 0xffU);
	bytes[1] = (unsigned char)((value >> 8) & 0xffU);
	bytes[2] = (unsigned char)((value >> 16) & 0xffU);
	bytes[3] = (unsigned char)((value >> 24) & 0xffU);
	assert_int_equal(fwrite(bytes, 1U, sizeof(bytes), file), sizeof(bytes));
}

/*
=============
write_test_pak

Creates the minimal Quake PAK fixture used by the precompiler resolver test.
=============
*/
static void write_test_pak(const char *path,
	test_pak_entry_t *entries,
	size_t entry_count)
{
	uint32_t data_offset = 12U;
	uint32_t directory_offset;
	FILE *file;

	for (size_t i = 0; i < entry_count; ++i)
	{
		size_t contents_length = strlen(entries[i].contents);
		assert_true(contents_length <= UINT32_MAX - data_offset);
		entries[i].offset = data_offset;
		entries[i].length = (uint32_t)contents_length;
		data_offset += entries[i].length;
	}
	directory_offset = data_offset;
	assert_true(entry_count <= UINT32_MAX / 64U);

	file = fopen(path, "wb");
	assert_non_null(file);
	assert_int_equal(fwrite("PACK", 1U, 4U, file), 4U);
	write_test_pak_u32(file, directory_offset);
	write_test_pak_u32(file, (uint32_t)(entry_count * 64U));

	for (size_t i = 0; i < entry_count; ++i)
	{
		assert_int_equal(fwrite(entries[i].contents,
			1U,
			entries[i].length,
			file),
			entries[i].length);
	}

	for (size_t i = 0; i < entry_count; ++i)
	{
		unsigned char name[56] = { 0 };
		size_t name_length = strlen(entries[i].name);
		assert_true(name_length < sizeof(name));
		memcpy(name, entries[i].name, name_length);
		assert_int_equal(fwrite(name, 1U, sizeof(name), file), sizeof(name));
		write_test_pak_u32(file, entries[i].offset);
		write_test_pak_u32(file, entries[i].length);
	}

	assert_int_equal(fclose(file), 0);
}

/*
=============
remove_precompiler_pak_fixture

Removes only the known files and directories created by the runtime fixture.
=============
*/
static void remove_precompiler_pak_fixture(void)
{
	static const char *files[] = {
		"__gladiator_pak_precompiler_fixture/.pak_cache/pak0/scripts/main.c",
		"__gladiator_pak_precompiler_fixture/.pak_cache/pak0/scripts/local_defs.h",
		"__gladiator_pak_precompiler_fixture/.pak_cache/pak0/scripts/angle_defs.h",
		"__gladiator_pak_precompiler_fixture/.pak_cache/pak0/scripts/choice.c",
		"__gladiator_pak_precompiler_fixture/.pak_cache/pak0/root_defs.h",
		"__gladiator_pak_precompiler_fixture/.pak_cache/pak1/scripts/choice.c",
		"__gladiator_pak_precompiler_fixture/.pak_cache/pak1/scripts/case_only.c",
		"__gladiator_pak_precompiler_fixture/.pak_cache/PAK1/scripts/choice.c",
		"__gladiator_pak_precompiler_fixture/.pak_cache/PAK1/scripts/case_only.c",
		"__gladiator_pak_precompiler_fixture/.pak_cache/custom/scripts/choice.c",
		"__gladiator_pak_precompiler_fixture/.pak_cache/custom/scripts/custom_only.c",
		"__gladiator_pak_precompiler_fixture/.pak_cache/pak10/scripts/pak10_only.c",
		"__gladiator_pak_precompiler_fixture/pak0.pak",
		"__gladiator_pak_precompiler_fixture/PAK1.PAK",
		"__gladiator_pak_precompiler_fixture/custom.pak",
		"__gladiator_pak_precompiler_fixture/pak10.pak",
	};
	static const char *directories[] = {
		"__gladiator_pak_precompiler_fixture/.pak_cache/pak0/scripts",
		"__gladiator_pak_precompiler_fixture/.pak_cache/pak0",
		"__gladiator_pak_precompiler_fixture/.pak_cache/pak1/scripts",
		"__gladiator_pak_precompiler_fixture/.pak_cache/pak1",
		"__gladiator_pak_precompiler_fixture/.pak_cache/PAK1/scripts",
		"__gladiator_pak_precompiler_fixture/.pak_cache/PAK1",
		"__gladiator_pak_precompiler_fixture/.pak_cache/custom/scripts",
		"__gladiator_pak_precompiler_fixture/.pak_cache/custom",
		"__gladiator_pak_precompiler_fixture/.pak_cache/pak10/scripts",
		"__gladiator_pak_precompiler_fixture/.pak_cache/pak10",
		"__gladiator_pak_precompiler_fixture/.pak_cache",
		"__gladiator_pak_precompiler_fixture",
	};

	for (size_t i = 0; i < ARRAY_SIZE(files); ++i)
	{
		(void)remove(files[i]);
	}
	for (size_t i = 0; i < ARRAY_SIZE(directories); ++i)
	{
		(void)TEST_RMDIR(directories[i]);
	}
}

/*
=============
test_pc_loads_pak_source_and_includes_with_logical_checksum_names

Pins PAK-backed top-level, root, quoted-local, and angle-local resolution while
retaining the directive spellings used by the source checksum cache.
=============
*/
static void test_pc_loads_pak_source_and_includes_with_logical_checksum_names(
	void **state)
{
	(void)state;
	static const char fixture_root[] = "__gladiator_pak_precompiler_fixture";
	static const char pak_path[] = "__gladiator_pak_precompiler_fixture/pak0.pak";
	static const char *expected_tokens[] = { "101", "202", "303" };
	static const char *expected_checksum_names[] = {
		"root_defs.h",
		"scripts/angle_defs.h",
		"scripts/local_defs.h",
		"scripts/main.c",
	};
	test_pak_entry_t entries[] = {
		{
			"scripts/main.c",
			"#include \"root_defs.h\"\n"
			"#include \"local_defs.h\"\n"
			"#include <angle_defs.h>\n"
			"ROOT_VALUE LOCAL_VALUE ANGLE_VALUE\n",
			0U,
			0U,
		},
		{ "root_defs.h", "#define ROOT_VALUE 101\n", 0U, 0U },
		{ "scripts/local_defs.h", "#define LOCAL_VALUE 202\n", 0U, 0U },
		{ "scripts/angle_defs.h", "#define ANGLE_VALUE 303\n", 0U, 0U },
	};

	remove_precompiler_pak_fixture();
	assert_int_equal(TEST_MKDIR(fixture_root), 0);
	write_test_pak(pak_path, entries, ARRAY_SIZE(entries));

	LibVar_Init();
	LibVarSet("basedir", fixture_root);
	LibVarSet("gamedir", "");
	LibVarSet("cddir", "");
	LibVarSet("gladiator_asset_dir", fixture_root);
	CRC_ResetSourceChecksums();
	PC_InitLexer();

	pc_source_t *source = PC_LoadSourceFile("scripts/main.c");
	assert_non_null(source);
	for (size_t i = 0; i < ARRAY_SIZE(expected_tokens); ++i)
	{
		pc_token_t token;
		assert_int_equal(PC_ReadToken(source, &token), 1);
		assert_int_equal(token.type, TT_NUMBER);
		assert_string_equal(token.string, expected_tokens[i]);
	}
	pc_token_t token;
	assert_int_equal(PC_ReadToken(source, &token), 0);
	assert_int_equal(CRC_SourceChecksumCount(), ARRAY_SIZE(expected_checksum_names));

	for (size_t i = 0; i < ARRAY_SIZE(expected_checksum_names); ++i)
	{
		uint16_t checksum = 0;
		char name[CRC_SOURCE_NAME_MAX];
		assert_true(CRC_SourceChecksumAt(i, &checksum, name, sizeof(name)));
		assert_string_equal(name, expected_checksum_names[i]);
	}

	PC_FreeSource(source);
	PC_ShutdownLexer();
	CRC_ResetSourceChecksums();
	LibVar_Shutdown();
	remove_precompiler_pak_fixture();
}

/*
=============
assert_pak_number_source

Loads one PAK-backed source and verifies its single numeric token.
=============
*/
static void assert_pak_number_source(const char *logical_name,
	const char *expected_token)
{
	pc_source_t *source = PC_LoadSourceFile(logical_name);
	assert_non_null(source);

	pc_token_t token;
	assert_int_equal(PC_ReadToken(source, &token), 1);
	assert_int_equal(token.type, TT_NUMBER);
	assert_string_equal(token.string, expected_token);
	assert_int_equal(PC_ReadToken(source, &token), 0);

	PC_FreeSource(source);
}

/*
=============
test_pak_search_uses_retail_numbered_precedence

Pins pak0-before-pak1 lookup, Windows-style case-insensitive numbered names,
and exclusion of custom and two-digit package names.
=============
*/
static void test_pak_search_uses_retail_numbered_precedence(void **state)
{
	(void)state;
	static const char fixture_root[] = "__gladiator_pak_precompiler_fixture";
	test_pak_entry_t pak0_entries[] = {
		{ "scripts/choice.c", "100\n", 0U, 0U },
	};
	test_pak_entry_t pak1_entries[] = {
		{ "scripts/choice.c", "200\n", 0U, 0U },
		{ "scripts/case_only.c", "201\n", 0U, 0U },
	};
	test_pak_entry_t custom_entries[] = {
		{ "scripts/choice.c", "300\n", 0U, 0U },
		{ "scripts/custom_only.c", "301\n", 0U, 0U },
	};
	test_pak_entry_t pak10_entries[] = {
		{ "scripts/pak10_only.c", "1000\n", 0U, 0U },
	};

	remove_precompiler_pak_fixture();
	assert_int_equal(TEST_MKDIR(fixture_root), 0);
	write_test_pak("__gladiator_pak_precompiler_fixture/pak0.pak",
		pak0_entries,
		ARRAY_SIZE(pak0_entries));
	write_test_pak("__gladiator_pak_precompiler_fixture/PAK1.PAK",
		pak1_entries,
		ARRAY_SIZE(pak1_entries));
	write_test_pak("__gladiator_pak_precompiler_fixture/custom.pak",
		custom_entries,
		ARRAY_SIZE(custom_entries));
	write_test_pak("__gladiator_pak_precompiler_fixture/pak10.pak",
		pak10_entries,
		ARRAY_SIZE(pak10_entries));

	LibVar_Init();
	LibVarSet("basedir", fixture_root);
	LibVarSet("gamedir", "");
	LibVarSet("cddir", "");
	LibVarSet("gladiator_asset_dir", fixture_root);
	CRC_ResetSourceChecksums();
	PC_InitLexer();

	assert_pak_number_source("scripts/choice.c", "100");
	assert_pak_number_source("scripts/case_only.c", "201");
	assert_null(PC_LoadSourceFile("scripts/custom_only.c"));
	assert_null(PC_LoadSourceFile("scripts/pak10_only.c"));

	PC_ShutdownLexer();
	CRC_ResetSourceChecksums();
	LibVar_Shutdown();
	remove_precompiler_pak_fixture();
}

/*
=============
test_load_script_file_registers_uncompressed_source_checksum

Pins the raw-file checksum and the verbatim buffer retail's LoadScriptFile keeps
(0x100401a0): length and end_p both describe the unmodified file bytes.
=============
*/
static void test_load_script_file_registers_uncompressed_source_checksum(
	void **state)
{
	(void)state;
	static const char path[] = "__gladiator_raw_source_crc.c";
	static const char source[] =
		"// Preserve raw bytes before compression.\r\n"
		"first /* comment */ second\r\n";

	remove(path);
	CRC_ResetSourceChecksums();
	assert_true(BotMemory_Init(BOT_MEMORY_DEFAULT_HEAP_SIZE));
	PS_SetBaseFolder(NULL);

	FILE *file = fopen(path, "wb");
	assert_non_null(file);
	assert_int_equal(fwrite(source, 1U, sizeof(source) - 1U, file),
		sizeof(source) - 1U);
	assert_int_equal(fclose(file), 0);

	pc_script_t *script = LoadScriptFile(path);
	assert_non_null(script);
	assert_int_equal(script->length, (int)(sizeof(source) - 1U));
	assert_ptr_equal(script->end_p, script->buffer + (sizeof(source) - 1U));
	assert_memory_equal(script->buffer, source, sizeof(source) - 1U);
	assert_int_equal(CRC_SourceChecksumCount(), 1U);

	uint16_t checksum = 0;
	char name[64];
	assert_true(CRC_SourceChecksumAt(0U,
		&checksum,
		name,
		sizeof(name)));
	assert_int_equal(checksum, 0xA41C);
	assert_string_equal(name, path);

	FreeScript(script);
	CRC_ResetSourceChecksums();
	BotMemory_Shutdown();
	assert_int_equal(remove(path), 0);
}

/*
=============
test_pc_reports_missing_endif_for_file_backed_source

Pins retail EndOfScript (0x10040060) reaching true on a file-backed script, which
is what lets PC_ReadSourceToken (0x10039501) warn about and pop dangling indents.
A compression pass in LoadScriptFile would leave end_p past the shortened text
and silently swallow the warning.
=============
*/
static void test_pc_reports_missing_endif_for_file_backed_source(void **state)
{
	(void)state;
	static const char path[] = "__gladiator_missing_endif.c";
	static const char contents[] =
		"// leading comment forces a shorter compressed buffer\n"
		"FIRST\n"
		"#ifdef NOT_DEFINED\n"
		"SECOND\n";

	remove(path);
	FILE *file = fopen(path, "wb");
	assert_non_null(file);
	assert_int_equal(fwrite(contents, 1U, sizeof(contents) - 1U, file),
		sizeof(contents) - 1U);
	assert_int_equal(fclose(file), 0);

	PC_InitLexer();

	pc_source_t *source = PC_LoadSourceFile(path);
	assert_non_null(source);

	pc_token_t token;
	assert_int_equal(1, PC_ReadToken(source, &token));
	assert_int_equal(TT_NAME, token.type);
	assert_string_equal("FIRST", token.string);

	//SECOND is inside the unterminated #ifdef, so the stream ends there
	assert_int_equal(0, PC_ReadToken(source, &token));
	assert_single_diagnostic(source,
		PC_ERROR_LEVEL_WARNING,
		"missing #endif");

	PC_FreeSource(source);
	PC_ShutdownLexer();
	assert_int_equal(remove(path), 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pc_loads_fw_items_and_matches_hlil_tokens),
        cmocka_unit_test(test_pc_loads_synonyms_and_matches_hlil_tokens),
		cmocka_unit_test(test_pc_peek_and_unread_mirror_hlil_behaviour),
		cmocka_unit_test(test_pc_rejects_numeric_token_pasting),
		cmocka_unit_test(test_pc_stringizes_macro_tokens_without_whitespace),
		cmocka_unit_test(test_pc_builtin_macros_preserve_name_token_metadata),
		cmocka_unit_test(test_pc_keeps_macro_adjacent_quoted_strings_separate),
		cmocka_unit_test(test_pc_concatenates_physical_adjacent_quoted_strings),
		cmocka_unit_test(test_pc_expands_chat_named_define),
		cmocka_unit_test(test_pc_reports_full_quoted_string_subtype),
		cmocka_unit_test(test_pc_preserves_memory_block_comment_token_boundary),
		cmocka_unit_test(test_pc_dollar_evaluators_preserve_retail_token_caches),
		cmocka_unit_test(test_pc_loads_pak_source_and_includes_with_logical_checksum_names),
		cmocka_unit_test(test_pak_search_uses_retail_numbered_precedence),
		cmocka_unit_test(test_load_script_file_registers_uncompressed_source_checksum),
		cmocka_unit_test(test_pc_define_keeps_self_referencing_body_tokens),
		cmocka_unit_test(test_pc_if_rejects_increment_operator_as_invalid),
		cmocka_unit_test(test_ps_read_number_classifies_look_ahead_character),
		cmocka_unit_test(test_ps_read_number_consumes_unguarded_lowercase_suffixes),
		cmocka_unit_test(test_pc_reports_missing_endif_for_file_backed_source),
	};

    return cmocka_run_group_tests(tests, NULL, NULL);
}
