#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "botlib/common/l_crc.h"
#include "botlib/common/l_struct.h"
#include "botlib/common/l_utils.h"
#include "botlib/precomp/l_precomp.h"

static void test_utils_initialisation_flags(void) {
    L_Utils_Shutdown();
    assert(!L_Utils_IsInitialised());
    assert(L_Utils_Init());
    assert(L_Utils_IsInitialised());
    L_Utils_Shutdown();
    assert(!L_Utils_IsInitialised());
}

static void test_struct_initialisation_flags(void) {
    L_Struct_Shutdown();
    assert(!L_Struct_IsInitialised());
    assert(L_Struct_Init());
    assert(L_Struct_IsInitialised());
    L_Struct_Shutdown();
    assert(!L_Struct_IsInitialised());
}

static void test_crc_matches_reference(void) {
    const char payload[] = "gladiator";
    const uint16_t expected_crc = 0x40FEu;
    const uint16_t crc = CRC_ProcessString((const uint8_t *)payload, strlen(payload));
    assert(crc == expected_crc);

    uint16_t running = CRC_INIT_VALUE;
    for (size_t i = 0; i < strlen(payload); ++i) {
        CRC_ProcessByte(&running, (uint8_t)payload[i]);
    }
    assert(CRC_Value(running) == expected_crc);
}

typedef struct test_sample_s {
    int integer;
    float real;
    char name[MAX_STRINGFIELD];
    unsigned char flags[3];
} test_sample_t;

static const fielddef_t g_test_sample_fields[] = {
    {"integer", (int)offsetof(test_sample_t, integer), FT_INT, 0, 0.0f, 0.0f, NULL},
    {"real", (int)offsetof(test_sample_t, real), FT_FLOAT | FT_BOUNDED, 0, -10.0f, 10.0f, NULL},
    {"name", (int)offsetof(test_sample_t, name), FT_STRING, 0, 0.0f, 0.0f, NULL},
    {"flags", (int)offsetof(test_sample_t, flags), FT_CHAR | FT_UNSIGNED | FT_ARRAY, 3, 0.0f, 0.0f, NULL},
    {NULL, 0, 0, 0, 0.0f, 0.0f, NULL},
};

static const structdef_t g_test_sample_struct = {
    (int)sizeof(test_sample_t),
    g_test_sample_fields,
};

static void test_read_structure_parses_basic_types(void) {
    const char script[] = "{ integer 1337 real 3.75 name \"Unit\" flags { 1, 2, 4 } }";

    PC_InitLexer();
    pc_source_t *source = PC_LoadSourceMemory("test_struct", script, strlen(script));
    assert(source != NULL);

    test_sample_t sample;
    memset(&sample, 0, sizeof(sample));

    assert(ReadStructure(source, &g_test_sample_struct, &sample));
    assert(sample.integer == 1337);
    assert(fabsf(sample.real - 3.75f) < 0.0001f);
    assert(strcmp(sample.name, "Unit") == 0);
    assert(sample.flags[0] == 1);
    assert(sample.flags[1] == 2);
    assert(sample.flags[2] == 4);

    PC_FreeSource(source);
    PC_ShutdownLexer();
}

static void test_vector2angles_and_angle_helpers(void) {
    vec3_t forward = {0.0f, 1.0f, 0.0f};
    vec3_t angles;
    Vector2Angles(forward, angles);
    assert(fabsf(angles[PITCH] - 0.0f) < 0.001f);
    assert(fabsf(angles[YAW] - 90.0f) < 0.001f);

    vec3_t up = {0.0f, 0.0f, 1.0f};
    Vector2Angles(up, angles);
    assert(fabsf(angles[PITCH] + 90.0f) < 0.001f);
    assert(fabsf(angles[YAW] - 0.0f) < 0.001f);

    assert(fabsf(AngleNormalize360(370.0f) - 10.0f) < 0.001f);
    assert(fabsf(AngleNormalize180(200.0f) + 160.0f) < 0.001f);
    assert(fabsf(AngleDelta(10.0f, 350.0f) - 20.0f) < 0.001f);
}

static void test_path_helpers(void) {
    char buffer[64];
    strcpy(buffer, "base");
    AppendPathSeperator(buffer, sizeof(buffer));
    size_t len = strlen(buffer);
    assert(len > 0);
    assert(buffer[len - 1] == BOTLIB_PATH_SEPARATOR_CHAR);

    strcpy(buffer, "base\\game");
    ConvertPath(buffer);
    for (size_t i = 0; i < strlen(buffer); ++i) {
        if (buffer[i] == BOTLIB_PATH_SEPARATOR_CHAR) {
            continue;
        }
        assert(buffer[i] != '/' && buffer[i] != '\\');
    }

    char stripped[64];
    BotUtils_StripExtension("scripts/test.bot", stripped, sizeof(stripped));
    assert(strcmp(stripped, "scripts/test") == 0);

    assert(BotUtils_FileExists(__FILE__));
}

int main(void) {
    test_utils_initialisation_flags();
    test_struct_initialisation_flags();
    test_crc_matches_reference();
    test_read_structure_parses_basic_types();
    test_vector2angles_and_angle_helpers();
    test_path_helpers();

    printf("bot_common_tests: all checks passed\n");
    return 0;
}
