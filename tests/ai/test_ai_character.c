#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <math.h>

#include "character/bot_character.h"
#include "chars.h"

static void test_babe_character_profile(void **state)
{
    (void)state;

    ai_character_profile_t *profile = AI_LoadCharacter("bots/babe_c.c", 1.0f);
    assert_non_null(profile);

    const char *chat_file = AI_CharacteristicAsString(profile, CHARACTERISTIC_CHAT_FILE);
    assert_non_null(chat_file);
    assert_string_equal(chat_file, "bots/babe_t.c");

    float aggression = AI_CharacteristicAsFloat(profile, CHARACTERISTIC_AGGRESSION);
    assert_true(fabsf(aggression - 0.7f) < 0.0001f);

    float grapple_user = AI_CharacteristicAsFloat(profile, CHARACTERISTIC_GRAPPLE_USER);
    assert_true(fabsf(grapple_user - 1.0f) < 0.0001f);

    int chat_cpm = AI_CharacteristicAsInteger(profile, CHARACTERISTIC_CHAT_CPM);
    assert_int_equal(chat_cpm, 400);

    AI_FreeCharacter(profile);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_babe_character_profile),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
