#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "botlib/common/l_libvar.h"
#include "botlib/common/l_memory.h"
#include "botlib/interface/botlib_interface.h"

#define TEST_CAPTURE_CAPACITY 32

typedef struct test_allocator_capture_s {
	int allocation_count;
	int free_count;
	int allocation_sizes[TEST_CAPTURE_CAPACITY];
	void *allocated_headers[TEST_CAPTURE_CAPACITY];
	void *freed_headers[TEST_CAPTURE_CAPACITY];
} test_allocator_capture_t;

static test_allocator_capture_t g_allocator;
static const botlib_import_table_t *g_imports = NULL;
static const char *g_import_name = NULL;
static const char *g_import_value = NULL;
static int g_import_get_count = 0;
static int g_import_set_count = 0;

/*
=============
test_allocate

Captures allocator request sizes and header pointers for layout assertions.
=============
*/
static void *test_allocate(int size)
{
	void *allocation = malloc((size_t)size);
	if (g_allocator.allocation_count < TEST_CAPTURE_CAPACITY)
	{
		int index = g_allocator.allocation_count;
		g_allocator.allocation_sizes[index] = size;
		g_allocator.allocated_headers[index] = allocation;
	}
	g_allocator.allocation_count += 1;
	return allocation;
}

/*
=============
test_free

Captures the engine-facing header pointer and then releases the allocation.
=============
*/
static void test_free(void *ptr)
{
	if (g_allocator.free_count < TEST_CAPTURE_CAPACITY)
	{
		g_allocator.freed_headers[g_allocator.free_count] = ptr;
	}
	g_allocator.free_count += 1;
	free(ptr);
}

/*
=============
test_import_get

Provides a controllable bridge bootstrap value to the compatibility path.
=============
*/
static int test_import_get(const char *name, char *value, size_t size)
{
	g_import_get_count += 1;
	if (name == NULL || value == NULL || size == 0 || g_import_name == NULL
		|| g_import_value == NULL || strcmp(name, g_import_name) != 0)
	{
		return BLERR_INVALIDIMPORT;
	}

	size_t length = strlen(g_import_value);
	if (length >= size)
	{
		length = size - 1;
	}
	memcpy(value, g_import_value, length);
	value[length] = '\0';
	return BLERR_NOERROR;
}

/*
=============
test_import_set

Captures calls made only through the bridge status-returning extension.
=============
*/
static int test_import_set(const char *name, const char *value)
{
	assert(name != NULL);
	assert(value != NULL);
	g_import_set_count += 1;
	return BLERR_NOERROR;
}

static const botlib_import_table_t g_bootstrap_imports = {
	.BotLibVarGet = test_import_get,
	.BotLibVarSet = test_import_set,
};

/*
=============
BotInterface_GetImportTable

Supplies the optional bridge table required by the standalone libvar build.
=============
*/
const botlib_import_table_t *BotInterface_GetImportTable(void)
{
	return g_imports;
}

/*
=============
test_reset

Starts each focused case with empty libvar and allocator state.
=============
*/
static void test_reset(void)
{
	LibVarDeAllocAll();
	BotMemory_Shutdown();
	BotMemory_SetAllocatorCallbacks(test_allocate, test_free);
	memset(&g_allocator, 0, sizeof(g_allocator));
	g_imports = NULL;
	g_import_name = NULL;
	g_import_value = NULL;
	g_import_get_count = 0;
	g_import_set_count = 0;
	assert(BotMemory_Init(0));
}

/*
=============
test_finish

Releases focused test state without leaving allocator callbacks installed.
=============
*/
static void test_finish(void)
{
	LibVarDeAllocAll();
	BotMemory_Shutdown();
	BotMemory_SetAllocatorCallbacks(NULL, NULL);
	g_imports = NULL;
}

/*
=============
test_retail_decimal_parser

Pins the unsigned decimal-only parser recovered at retail 0x10038750.
=============
*/
static void test_retail_decimal_parser(void)
{
	assert(LibVarStringValue("123.375") == 123.375f);
	assert(LibVarStringValue(".5") == 0.5f);
	assert(LibVarStringValue("0012") == 12.0f);
	assert(LibVarStringValue("") == 0.0f);
	assert(LibVarStringValue("-12") == 0.0f);
	assert(LibVarStringValue("+12") == 0.0f);
	assert(LibVarStringValue("1e2") == 0.0f);
	assert(LibVarStringValue("12x") == 0.0f);
	assert(LibVarStringValue("1.2.3") == 0.0f);
	assert(LibVarStringValue(" 12") == 0.0f);
	assert(LibVarStringValue(NULL) == 0.0f);
	assert(LibVarStringValue(".x") > 7.19f);
	assert(LibVarStringValue(".x") < 7.21f);
}

/*
=============
test_retail_struct_and_allocation_layout

Pins field order, the combined variable/name block, and separate value block.
=============
*/
static void test_retail_struct_and_allocation_layout(void)
{
	test_reset();

	assert(offsetof(libvar_t, name) == 0);
	assert(offsetof(libvar_t, string) == sizeof(void *));
	assert(offsetof(libvar_t, flags) == sizeof(void *) * 2);
	assert(offsetof(libvar_t, modified) == offsetof(libvar_t, flags) + sizeof(int));
	assert(offsetof(libvar_t, value) == offsetof(libvar_t, modified) + sizeof(int));

	const char *name = "ParityName";
	const char *text = "37.5";
	libvar_t *var = LibVar(name, text);
	assert(var != NULL);
	assert(g_allocator.allocation_count == 2);
	assert(var->name == (char *)(var + 1));
	assert(strcmp(var->name, name) == 0);
	assert(strcmp(var->string, text) == 0);
	assert(var->flags == 0);
	assert(var->modified == 1);
	assert(var->next == NULL);

	size_t variable_payload = sizeof(libvar_t) + strlen(name) + 1;
	size_t string_payload = strlen(text) + 1;
	assert(MemoryByteSize(var) - MemoryByteSize(var->string)
		== variable_payload - string_payload);
	assert(g_allocator.allocation_sizes[0] - g_allocator.allocation_sizes[1]
		== (int)(variable_payload - string_payload));

	void *variable_header = g_allocator.allocated_headers[0];
	void *string_header = g_allocator.allocated_headers[1];
	LibVarDeAllocAll();
	assert(g_allocator.free_count == 2);
	assert(g_allocator.freed_headers[0] == string_header);
	assert(g_allocator.freed_headers[1] == variable_header);
	assert(BotMemory_TotalAllocated() == 0);

	test_finish();
}

/*
=============
test_create_get_string_and_value_semantics

Pins ASCII case-insensitive lookup, default stickiness, and miss return values.
=============
*/
static void test_create_get_string_and_value_semantics(void)
{
	test_reset();

	assert(LibVarGet("missing") == NULL);
	assert(strcmp(LibVarGetString("missing"), "") == 0);
	assert(LibVarGetValue("missing") == 0.0f);
	assert(g_allocator.allocation_count == 0);

	libvar_t *created = LibVar("MiXeD", "12.25");
	assert(created != NULL);
	assert(LibVarGet("mixed") == created);
	assert(LibVar("MIXED", "99") == created);
	assert(strcmp(LibVarString("mixed", "77"), "12.25") == 0);
	assert(LibVarValue("mixed", "66") == 12.25f);
	assert(g_allocator.allocation_count == 2);

	const char high_byte_name_one[] = {'n', (char)0xc0, '\0'};
	const char high_byte_name_two[] = {'N', (char)0xe0, '\0'};
	assert(LibVar(high_byte_name_one, "1") != NULL);
	assert(LibVarGet(high_byte_name_two) == NULL);

	test_finish();
}

/*
=============
test_set_and_modified_semantics

Pins replacement on identical text, parsing, and modified-flag transitions.
=============
*/
static void test_set_and_modified_semantics(void)
{
	test_reset();

	libvar_t *var = LibVar("repeat", "7");
	assert(var != NULL);
	assert(LibVarChanged("REPEAT") == 1);
	LibVarSetNotModified("repeat");
	assert(var->modified == 0);
	assert(LibVarChanged("repeat") == 0);

	int allocations_before = g_allocator.allocation_count;
	int frees_before = g_allocator.free_count;
	LibVarSet("REPEAT", "7");
	assert(g_allocator.allocation_count == allocations_before + 1);
	assert(g_allocator.free_count == frees_before + 1);
	assert(var->modified == 1);
	assert(strcmp(var->string, "7") == 0);
	assert(var->value == 7.0f);

	LibVarSetNotModified("repeat");
	LibVarSet("repeat", "-4");
	assert(var->modified == 1);
	assert(strcmp(var->string, "-4") == 0);
	assert(var->value == 0.0f);
	assert(LibVarChanged("does_not_exist") == 0);
	LibVarSetNotModified("does_not_exist");

	test_finish();
}

/*
=============
test_import_bootstrap_does_not_refresh_existing_values

Pins the compatibility bootstrap while preserving retail cached-get behavior.
=============
*/
static void test_import_bootstrap_does_not_refresh_existing_values(void)
{
	test_reset();

	g_imports = &g_bootstrap_imports;
	g_import_name = "engine_value";
	g_import_value = "14";
	libvar_t *var = LibVarGet("engine_value");
	assert(var != NULL);
	assert(g_import_get_count == 1);
	assert(strcmp(var->string, "14") == 0);
	assert(var->value == 14.0f);
	assert(var->modified == 1);
	LibVarSet("local_only", "3");
	assert(g_import_set_count == 0);
	assert(LibVarSetStatus("exported", "4") == BLERR_NOERROR);
	assert(g_import_set_count == 1);

	g_import_value = "99";
	assert(LibVarGet("ENGINE_VALUE") == var);
	assert(g_import_get_count == 1);
	assert(strcmp(var->string, "14") == 0);
	assert(var->value == 14.0f);

	test_finish();
}

/*
=============
test_list_order_and_shutdown

Pins head insertion, string-before-node teardown, and idempotent shutdown.
=============
*/
static void test_list_order_and_shutdown(void)
{
	test_reset();

	libvar_t *first = LibVar("first", "1");
	libvar_t *second = LibVar("second", "2");
	assert(first != NULL);
	assert(second != NULL);
	assert(second->next == first);
	assert(first->next == NULL);
	assert(g_allocator.allocation_count == 4);

	void *first_variable_header = g_allocator.allocated_headers[0];
	void *first_string_header = g_allocator.allocated_headers[1];
	void *second_variable_header = g_allocator.allocated_headers[2];
	void *second_string_header = g_allocator.allocated_headers[3];
	LibVar_Shutdown();
	assert(g_allocator.free_count == 4);
	assert(g_allocator.freed_headers[0] == second_string_header);
	assert(g_allocator.freed_headers[1] == second_variable_header);
	assert(g_allocator.freed_headers[2] == first_string_header);
	assert(g_allocator.freed_headers[3] == first_variable_header);
	assert(LibVarGet("first") == NULL);
	assert(BotMemory_TotalAllocated() == 0);

	LibVar_Shutdown();
	assert(g_allocator.free_count == 4);
	test_finish();
}

/*
=============
main

Runs the isolated Gladiator libvar parity regression suite.
=============
*/
int main(void)
{
	test_retail_decimal_parser();
	test_retail_struct_and_allocation_layout();
	test_create_get_string_and_value_semantics();
	test_set_and_modified_semantics();
	test_import_bootstrap_does_not_refresh_existing_values();
	test_list_order_and_shutdown();

	printf("bot_common_libvar_tests: all checks passed\n");
	return 0;
}
