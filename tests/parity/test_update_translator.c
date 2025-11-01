#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "q2bridge/aas_translation.h"
#include "q2bridge/bridge.h"
#include "q2bridge/update_translator.h"
#include "botlib/ai/move/mover_catalogue.h"
#include "botlib_contract_loader.h"

#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))

typedef struct captured_print_s
{
    int severity;
    char message[1024];
} captured_print_t;

typedef struct translator_test_context_s
{
    bot_import_t imports;
    captured_print_t prints[32];
    size_t print_count;
    botlib_contract_catalogue_t catalogue;
} translator_test_context_t;

static translator_test_context_t *g_active_context = NULL;

static void Mock_Print(int type, char *fmt, ...)
{
    if (g_active_context == NULL || fmt == NULL)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    va_end(args);

    size_t index = g_active_context->print_count;
    if (index >= ARRAY_LEN(g_active_context->prints))
    {
        index = ARRAY_LEN(g_active_context->prints) - 1U;
    }

    captured_print_t *slot = &g_active_context->prints[index];
    slot->severity = type;
    strncpy(slot->message, buffer, sizeof(slot->message) - 1U);
    slot->message[sizeof(slot->message) - 1U] = '\0';

    if (g_active_context->print_count < ARRAY_LEN(g_active_context->prints))
    {
        g_active_context->print_count += 1U;
    }
}

static int translator_setup(void **state)
{
    translator_test_context_t *context = (translator_test_context_t *)calloc(1, sizeof(*context));
    assert_non_null(context);

    context->imports.Print = Mock_Print;
    g_active_context = context;

    char contract_path[1024];
    int written = snprintf(contract_path,
                           sizeof(contract_path),
                           "%s/tests/reference/botlib_contract.json",
                           PROJECT_SOURCE_DIR);
    assert_true(written > 0);
    assert_true((size_t)written < sizeof(contract_path));

    int status = BotlibContract_Load(contract_path, &context->catalogue);
    assert_int_equal(status, 0);

    Q2Bridge_SetImportTable(&context->imports);
    Bridge_ResetCachedUpdates();
    TranslateEntity_SetWorldLoaded(qfalse);
    TranslateEntity_SetCurrentTime(0.0f);

    *state = context;
    return 0;
}

static int translator_teardown(void **state)
{
    translator_test_context_t *context = (translator_test_context_t *)(*state);
    if (context != NULL)
    {
        Q2Bridge_ClearImportTable();
        BotlibContract_Free(&context->catalogue);
        free(context);
    }

    g_active_context = NULL;
    Bridge_ResetCachedUpdates();
    TranslateEntity_SetWorldLoaded(qfalse);
    TranslateEntity_SetCurrentTime(0.0f);
    return 0;
}

static float quantize_component(float angle)
{
    const float to_short = 65536.0f / 360.0f;
    const float to_angle = 360.0f / 65536.0f;
    long quantised = lroundf(angle * to_short);
    quantised &= 0xFFFF;
    if (quantised & 0x8000)
    {
        quantised -= 0x10000;
    }
    return (float)quantised * to_angle;
}

static void assert_vec3_equal(const vec3_t actual, const vec3_t expected, float tolerance)
{
    assert_float_equal(actual[0], expected[0], tolerance);
    assert_float_equal(actual[1], expected[1], tolerance);
    assert_float_equal(actual[2], expected[2], tolerance);
}

static void test_translate_client_quantises_payload(void **state)
{
    (void)state;

    AASClientFrame frame;
    memset(&frame, 0, sizeof(frame));

    bot_updateclient_t update;
    memset(&update, 0, sizeof(update));

    update.pm_type = 2;
    update.origin[0] = 128.0f;
    update.origin[1] = -64.0f;
    update.origin[2] = 16.0f;
    update.velocity[0] = 10.0f;
    update.velocity[1] = -5.0f;
    update.velocity[2] = 2.0f;
    update.delta_angles[0] = 45.125f;
    update.delta_angles[1] = -90.5f;
    update.delta_angles[2] = 720.0f;
    update.pm_flags = 0x5a;
    update.pm_time = 13;
    update.gravity = 800.0f;
    update.viewangles[0] = 30.4f;
    update.viewangles[1] = -181.3f;
    update.viewangles[2] = 89.8f;
    update.viewoffset[0] = 1.0f;
    update.viewoffset[1] = 2.0f;
    update.viewoffset[2] = 3.0f;
    update.kick_angles[0] = 5.5f;
    update.kick_angles[1] = -1.25f;
    update.kick_angles[2] = 0.75f;
    update.gunangles[0] = 15.0f;
    update.gunangles[1] = 25.0f;
    update.gunangles[2] = 35.0f;
    update.gunoffset[0] = -2.0f;
    update.gunoffset[1] = -4.0f;
    update.gunoffset[2] = 6.0f;
    update.gunindex = 7;
    update.gunframe = 42;
    update.blend[0] = 0.1f;
    update.blend[1] = 0.2f;
    update.blend[2] = 0.3f;
    update.blend[3] = 0.4f;
    update.fov = 100.0f;
    update.rdflags = 3;
    for (int i = 0; i < MAX_STATS; ++i)
    {
        update.stats[i] = (short)(i * 3);
    }
    for (int i = 0; i < MAX_ITEMS; ++i)
    {
        update.inventory[i] = i;
    }

    int status = TranslateClientUpdate(1, &update, 1.25f, &frame);
    assert_int_equal(status, BLERR_NOERROR);

    assert_int_equal(frame.pm_type, update.pm_type);
    assert_vec3_equal(frame.origin, update.origin, 0.0001f);
    assert_vec3_equal(frame.velocity, update.velocity, 0.0001f);
    assert_int_equal(frame.pm_flags, update.pm_flags);
    assert_int_equal(frame.pm_time, update.pm_time);
    assert_float_equal(frame.gravity, update.gravity, 0.0001f);
    assert_vec3_equal(frame.viewoffset, update.viewoffset, 0.0001f);
    assert_vec3_equal(frame.gunoffset, update.gunoffset, 0.0001f);
    assert_int_equal(frame.gunindex, update.gunindex);
    assert_int_equal(frame.gunframe, update.gunframe);
    assert_memory_equal(frame.blend, update.blend, sizeof(frame.blend));
    assert_float_equal(frame.fov, update.fov, 0.0001f);
    assert_int_equal(frame.rdflags, update.rdflags);
    assert_memory_equal(frame.stats, update.stats, sizeof(frame.stats));
    assert_memory_equal(frame.inventory, update.inventory, sizeof(frame.inventory));

    assert_float_equal(frame.delta_angles[0], quantize_component(update.delta_angles[0]), 0.0001f);
    assert_float_equal(frame.delta_angles[1], quantize_component(update.delta_angles[1]), 0.0001f);
    assert_float_equal(frame.delta_angles[2], quantize_component(update.delta_angles[2]), 0.0001f);
    assert_float_equal(frame.viewangles[0], quantize_component(update.viewangles[0]), 0.0001f);
    assert_float_equal(frame.viewangles[1], quantize_component(update.viewangles[1]), 0.0001f);
    assert_float_equal(frame.viewangles[2], quantize_component(update.viewangles[2]), 0.0001f);
    assert_float_equal(frame.kick_angles[0], quantize_component(update.kick_angles[0]), 0.0001f);
    assert_float_equal(frame.kick_angles[1], quantize_component(update.kick_angles[1]), 0.0001f);
    assert_float_equal(frame.kick_angles[2], quantize_component(update.kick_angles[2]), 0.0001f);
    assert_float_equal(frame.gunangles[0], quantize_component(update.gunangles[0]), 0.0001f);
    assert_float_equal(frame.gunangles[1], quantize_component(update.gunangles[1]), 0.0001f);
    assert_float_equal(frame.gunangles[2], quantize_component(update.gunangles[2]), 0.0001f);

    assert_float_equal(frame.last_update_time, 1.25f, 0.0001f);
    assert_float_equal(frame.frame_delta, 0.0f, 0.0001f);

    update.delta_angles[0] = -12.75f;
    update.viewangles[1] = 179.9f;
    update.kick_angles[2] = -33.33f;
    update.gunangles[0] = 91.1f;

    status = TranslateClientUpdate(1, &update, 3.75f, &frame);
    assert_int_equal(status, BLERR_NOERROR);
    assert_float_equal(frame.frame_delta, 2.5f, 0.0001f);
    assert_float_equal(frame.delta_angles[0], quantize_component(update.delta_angles[0]), 0.0001f);
    assert_float_equal(frame.viewangles[1], quantize_component(update.viewangles[1]), 0.0001f);
    assert_float_equal(frame.kick_angles[2], quantize_component(update.kick_angles[2]), 0.0001f);
    assert_float_equal(frame.gunangles[0], quantize_component(update.gunangles[0]), 0.0001f);
}

static void test_bridge_update_client_inactive_logs_contract_message(void **state)
{
    translator_test_context_t *context = (translator_test_context_t *)(*state);
    context->print_count = 0U;

    bot_updateclient_t update;
    memset(&update, 0, sizeof(update));

    int status = Bridge_UpdateClient(0, &update);
    assert_int_equal(status, BLERR_AIUPDATEINACTIVECLIENT);
    assert_true(context->print_count > 0U);

    const botlib_contract_export_t *entry = BotlibContract_FindExport(&context->catalogue, "BridgeDiagnostics");
    assert_non_null(entry);
    const botlib_contract_scenario_t *scenario = BotlibContract_FindScenario(entry, "success");
    assert_non_null(scenario);
    const botlib_contract_message_t *expected = BotlibContract_FindMessageContaining(scenario, "inactive bot client");
    assert_non_null(expected);

    assert_string_equal(context->prints[0].message, expected->text);
    assert_int_equal(context->prints[0].severity, expected->severity);

    const botlib_contract_return_code_t *code = BotlibContract_FindReturnCode(scenario, BLERR_AIUPDATEINACTIVECLIENT);
    assert_non_null(code);
}

static void test_translate_entity_dirty_flags_and_relink_logging(void **state)
{
    translator_test_context_t *context = (translator_test_context_t *)(*state);
    context->print_count = 0U;

    TranslateEntity_SetWorldLoaded(qtrue);
    TranslateEntity_SetCurrentTime(1.0f);

    AASEntityFrame frame;
    memset(&frame, 0, sizeof(frame));

    bot_updateentity_t update;
    memset(&update, 0, sizeof(update));

    update.origin[0] = 64.0f;
    update.origin[1] = 32.0f;
    update.origin[2] = 16.0f;
    update.old_origin[0] = 60.0f;
    update.old_origin[1] = 30.0f;
    update.old_origin[2] = 14.0f;
    update.mins[0] = -16.0f;
    update.mins[1] = -8.0f;
    update.mins[2] = -4.0f;
    update.maxs[0] = 16.0f;
    update.maxs[1] = 8.0f;
    update.maxs[2] = 4.0f;
    update.angles[0] = 91.0f;
    update.angles[1] = -45.0f;
    update.angles[2] = 10.0f;
    update.solid = 3;
    update.modelindex = 2;
    update.modelindex2 = 3;
    update.modelindex3 = 4;
    update.modelindex4 = 5;
    update.frame = 6;
    update.skinnum = 7;
    update.effects = 8;
    update.renderfx = 9;
    update.sound = 10;
    update.event = 11;

    int status = TranslateEntityUpdate(5, &update, &frame);
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal(frame.number, 5);
    assert_true(frame.origin_dirty);
    assert_true(frame.bounds_dirty);
    assert_true(frame.angles_dirty);
    assert_false(frame.is_mover);
    assert_vec3_equal(frame.origin, update.origin, 0.0001f);
    assert_vec3_equal(frame.old_origin, update.old_origin, 0.0001f);
    vec3_t zero = {0.0f, 0.0f, 0.0f};
    assert_vec3_equal(frame.previous_origin, zero, 0.0001f);
    assert_vec3_equal(frame.mins, update.mins, 0.0001f);
    assert_vec3_equal(frame.maxs, update.maxs, 0.0001f);
    assert_float_equal(frame.angles[0], quantize_component(update.angles[0]), 0.0001f);
    assert_float_equal(frame.angles[1], quantize_component(update.angles[1]), 0.0001f);
    assert_float_equal(frame.angles[2], quantize_component(update.angles[2]), 0.0001f);
    assert_int_equal(frame.solid, update.solid);
    assert_int_equal(frame.modelindex, update.modelindex);
    assert_int_equal(frame.modelindex2, update.modelindex2);
    assert_int_equal(frame.modelindex3, update.modelindex3);
    assert_int_equal(frame.modelindex4, update.modelindex4);
    assert_int_equal(frame.frame, update.frame);
    assert_int_equal(frame.skinnum, update.skinnum);
    assert_int_equal(frame.effects, update.effects);
    assert_int_equal(frame.renderfx, update.renderfx);
    assert_int_equal(frame.sound, update.sound);
    assert_int_equal(frame.event_id, update.event);
    assert_float_equal(frame.last_update_time, 1.0f, 0.0001f);
    assert_float_equal(frame.frame_delta, 0.0f, 0.0001f);

    assert_true(context->print_count > 0U);
    const botlib_contract_export_t *entry = BotlibContract_FindExport(&context->catalogue, "BridgeDiagnostics");
    assert_non_null(entry);
    const botlib_contract_scenario_t *scenario = BotlibContract_FindScenario(entry, "success");
    assert_non_null(scenario);
    const botlib_contract_message_t *brush_message = BotlibContract_FindMessageContaining(scenario, "brush model");
    assert_non_null(brush_message);
    assert_string_equal(context->prints[context->print_count - 1U].message, brush_message->text);
    assert_int_equal(context->prints[context->print_count - 1U].severity, brush_message->severity);

    context->print_count = 0U;
    TranslateEntity_SetCurrentTime(2.5f);

    bot_updateentity_t follow_up = update;
    follow_up.origin[0] = 72.0f;
    follow_up.origin[1] = 40.0f;
    follow_up.origin[2] = 24.0f;
    follow_up.old_origin[0] = 68.0f;
    follow_up.old_origin[1] = 36.0f;
    follow_up.old_origin[2] = 20.0f;

    status = TranslateEntityUpdate(5, &follow_up, &frame);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(frame.origin_dirty);
    assert_false(frame.bounds_dirty);
    assert_false(frame.angles_dirty);
    assert_vec3_equal(frame.previous_origin, update.origin, 0.0001f);
    assert_float_equal(frame.frame_delta, 1.5f, 0.0001f);
    assert_int_equal(context->print_count, 0U);

    BotMove_MoverCatalogueReset();
    bot_mover_catalogue_entry_t mover_entry = {0};
    mover_entry.modelnum = update.modelindex;
    assert_true(BotMove_MoverCatalogueInsert(&mover_entry));
    char model_name[] = "*2";
    char *model_entries[] = {model_name};
    botinterface_asset_list_t assets = {
        .entries = model_entries,
        .count = ARRAY_LEN(model_entries),
    };
    assert_true(BotMove_MoverCatalogueFinalize(&assets));

    context->print_count = 0U;
    TranslateEntity_SetCurrentTime(4.0f);
    status = TranslateEntityUpdate(5, &follow_up, &frame);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(frame.is_mover);
    assert_int_equal(context->print_count, 1U);
    assert_string_equal(context->prints[0].message, brush_message->text);
    assert_int_equal(context->prints[0].severity, brush_message->severity);

    BotMove_MoverCatalogueReset();
}

static void test_translate_entity_aas_not_loaded_logs(void **state)
{
    translator_test_context_t *context = (translator_test_context_t *)(*state);
    context->print_count = 0U;

    TranslateEntity_SetWorldLoaded(qfalse);

    AASEntityFrame frame;
    memset(&frame, 0, sizeof(frame));

    bot_updateentity_t update;
    memset(&update, 0, sizeof(update));

    int status = TranslateEntityUpdate(3, &update, &frame);
    assert_int_equal(status, BLERR_NOAASFILE);
    assert_true(context->print_count > 0U);

    const botlib_contract_export_t *entry = BotlibContract_FindExport(&context->catalogue, "BridgeDiagnostics");
    assert_non_null(entry);
    const botlib_contract_scenario_t *scenario = BotlibContract_FindScenario(entry, "success");
    assert_non_null(scenario);
    const botlib_contract_message_t *expected = BotlibContract_FindMessageContaining(scenario, "AAS_UpdateEntity: not loaded");
    assert_non_null(expected);
    assert_string_equal(context->prints[0].message, expected->text);
    assert_int_equal(context->prints[0].severity, expected->severity);

    const botlib_contract_return_code_t *code = BotlibContract_FindReturnCode(scenario, BLERR_NOAASFILE);
    assert_non_null(code);
}

static void test_bot_update_entity_logging_stops_after_map_load(void **state)
{
    translator_test_context_t *context = (translator_test_context_t *)(*state);
    context->print_count = 0U;

    TranslateEntity_SetWorldLoaded(qfalse);

    bot_updateentity_t update;
    memset(&update, 0, sizeof(update));

    int status = Bridge_UpdateEntity(3, &update);
    assert_int_equal(status, BLERR_NOAASFILE);
    assert_true(context->print_count > 0U);

    TranslateEntity_SetCurrentTime(0.0f);
    TranslateEntity_SetWorldLoaded(qtrue);
    context->print_count = 0U;

    status = Bridge_UpdateEntity(3, &update);
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal(context->print_count, 0U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_translate_client_quantises_payload,
                                        translator_setup,
                                        translator_teardown),
        cmocka_unit_test_setup_teardown(test_bridge_update_client_inactive_logs_contract_message,
                                        translator_setup,
                                        translator_teardown),
        cmocka_unit_test_setup_teardown(test_translate_entity_dirty_flags_and_relink_logging,
                                        translator_setup,
                                        translator_teardown),
        cmocka_unit_test_setup_teardown(test_translate_entity_aas_not_loaded_logs,
                                        translator_setup,
                                        translator_teardown),
        cmocka_unit_test_setup_teardown(test_bot_update_entity_logging_stops_after_map_load,
                                        translator_setup,
                                        translator_teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}

