#include "aas_sound.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "aas_local.h"
#include "../common/l_assets.h"
#include "../common/l_log.h"
#include "../common/l_memory.h"

typedef struct aas_sound_state_s
{
    aas_soundinfo_t *infos;
    size_t info_count;
    size_t info_capacity;

    char **asset_names;
    char **asset_normalized;
    int *asset_info_index;
    size_t asset_count;

    aas_sound_event_t *sound_events;
    size_t sound_event_count;
    size_t sound_event_capacity;

    aas_pointlight_event_t *pointlight_events;
    size_t pointlight_event_count;
    size_t pointlight_event_capacity;

    float frame_time;
    bool initialised;
} aas_sound_state_t;

static aas_sound_state_t g_aas_sound_state;

#define AAS_SOUND_DEFAULT_CAPACITY 256
#define AAS_SOUNDINFO_MAX_LIMIT 65535
#define AAS_AASSOUNDS_MAX_LIMIT 65536
#define AAS_SOUND_DEFAULT_DURATION 0.5f
#define AAS_POINTLIGHT_DEFAULT_DECAY 0.5f

static bool AAS_Sound_PushInfo(const aas_soundinfo_t *info);

static int AAS_Sound_ClampSoundInfoLimit(int requested)
{
    if (requested < 0 || requested > AAS_SOUNDINFO_MAX_LIMIT)
    {
        BotLib_Print(PRT_ERROR, "max_soundinfo out of range [0, 65535]\n");
        return AAS_SOUND_DEFAULT_CAPACITY;
    }
    return requested;
}

static int AAS_Sound_ClampAASSoundLimit(int requested)
{
    if (requested < 0 || requested > AAS_AASSOUNDS_MAX_LIMIT)
    {
        BotLib_Print(PRT_ERROR, "max_aassounds out of range [0, 65536]\n");
        return AAS_SOUND_DEFAULT_CAPACITY;
    }
    return requested;
}

static void AAS_Sound_NormalizeName(const char *input, char *output, size_t size)
{
    if (output == NULL || size == 0)
    {
        return;
    }

    output[0] = '\0';
    if (input == NULL)
    {
        return;
    }

    char temp[128];
    size_t out_index = 0U;
    for (const char *cursor = input; *cursor != '\0' && out_index + 1U < sizeof(temp); ++cursor)
    {
        unsigned char c = (unsigned char)*cursor;
        if (c == '\\')
        {
            c = '/';
        }
        temp[out_index++] = (char)tolower(c);
    }
    temp[out_index] = '\0';

    const char *normalized = temp;
    if (strncmp(temp, "sound/", 6) == 0)
    {
        normalized = temp + 6;
    }

    strncpy(output, normalized, size - 1U);
    output[size - 1U] = '\0';
}

#define AAS_SOUND_MAX_MACROS 64

typedef struct aas_sound_macro_s
{
    char name[64];
    int value;
} aas_sound_macro_t;

static aas_sound_macro_t g_aas_sound_macros[AAS_SOUND_MAX_MACROS];
static size_t g_aas_sound_macro_count = 0U;

static void AAS_Sound_ClearMacros(void)
{
    g_aas_sound_macro_count = 0U;
}

static void AAS_Sound_RegisterMacro(const char *name, int value)
{
    if (name == NULL || *name == '\0')
    {
        return;
    }

    for (size_t i = 0; i < g_aas_sound_macro_count; ++i)
    {
        if (strcasecmp(g_aas_sound_macros[i].name, name) == 0)
        {
            g_aas_sound_macros[i].value = value;
            return;
        }
    }

    if (g_aas_sound_macro_count < AAS_SOUND_MAX_MACROS)
    {
        aas_sound_macro_t *slot = &g_aas_sound_macros[g_aas_sound_macro_count++];
        strncpy(slot->name, name, sizeof(slot->name) - 1U);
        slot->name[sizeof(slot->name) - 1U] = '\0';
        slot->value = value;
    }
}

static bool AAS_Sound_FindMacroValue(const char *name, int *out)
{
    if (name == NULL || out == NULL)
    {
        return false;
    }

    for (size_t i = 0; i < g_aas_sound_macro_count; ++i)
    {
        if (strcasecmp(g_aas_sound_macros[i].name, name) == 0)
        {
            *out = g_aas_sound_macros[i].value;
            return true;
        }
    }

    return false;
}

static void AAS_Sound_SkipWhitespace(char **cursor)
{
    if (cursor == NULL || *cursor == NULL)
    {
        return;
    }

    char *ptr = *cursor;
    while (*ptr != '\0' && isspace((unsigned char)*ptr))
    {
        ++ptr;
    }
    *cursor = ptr;
}

static char *AAS_Sound_Trim(char *text)
{
    if (text == NULL)
    {
        return NULL;
    }

    char *start = text;
    while (*start != '\0' && isspace((unsigned char)*start))
    {
        ++start;
    }

    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1]))
    {
        --end;
    }

    *end = '\0';
    return start;
}

static bool AAS_Sound_ParseBareToken(char **cursor, char *buffer, size_t size)
{
    if (cursor == NULL || buffer == NULL || size == 0U)
    {
        return false;
    }

    char *ptr = *cursor;
    AAS_Sound_SkipWhitespace(&ptr);
    if (*ptr == '\0')
    {
        *cursor = ptr;
        return false;
    }

    size_t index = 0U;
    while (*ptr != '\0' && !isspace((unsigned char)*ptr))
    {
        if (index + 1U < size)
        {
            buffer[index++] = *ptr;
        }
        ++ptr;
    }
    buffer[index] = '\0';
    *cursor = ptr;
    return index > 0U;
}

static bool AAS_Sound_ParseStringToken(char **cursor, char *buffer, size_t size)
{
    if (cursor == NULL || buffer == NULL || size == 0U)
    {
        return false;
    }

    char *ptr = *cursor;
    AAS_Sound_SkipWhitespace(&ptr);
    if (*ptr != '"')
    {
        return false;
    }

    ++ptr;
    size_t index = 0U;
    while (*ptr != '\0' && *ptr != '"')
    {
        if (index + 1U < size)
        {
            buffer[index++] = *ptr;
        }
        ++ptr;
    }
    buffer[index] = '\0';
    if (*ptr == '"')
    {
        ++ptr;
    }
    *cursor = ptr;
    return true;
}

static bool AAS_Sound_ParseDefineLine(char *line)
{
    if (line == NULL)
    {
        return false;
    }

    char *cursor = line + 7;
    AAS_Sound_SkipWhitespace(&cursor);

    char name[64];
    if (!AAS_Sound_ParseBareToken(&cursor, name, sizeof(name)))
    {
        return false;
    }

    AAS_Sound_SkipWhitespace(&cursor);
    char value_text[64];
    if (!AAS_Sound_ParseBareToken(&cursor, value_text, sizeof(value_text)))
    {
        return false;
    }

    char *endptr = NULL;
    long value = strtol(value_text, &endptr, 0);
    if (endptr == value_text)
    {
        return false;
    }

    AAS_Sound_RegisterMacro(name, (int)value);
    return true;
}

static bool AAS_Sound_ParseFloatFromString(const char *text, float *out)
{
    if (text == NULL || out == NULL)
    {
        return false;
    }

    char *endptr = NULL;
    double value = strtod(text, &endptr);
    if (endptr == text)
    {
        return false;
    }

    *out = (float)value;
    return true;
}

static bool AAS_Sound_ParseIntFromString(const char *text, int *out)
{
    if (text == NULL || out == NULL)
    {
        return false;
    }

    char *endptr = NULL;
    long value = strtol(text, &endptr, 0);
    if (endptr != text && *endptr == '\0')
    {
        *out = (int)value;
        return true;
    }

    int macro_value = 0;
    if (AAS_Sound_FindMacroValue(text, &macro_value))
    {
        *out = macro_value;
        return true;
    }

    return false;
}

static char *AAS_Sound_RemoveComments(const char *input, size_t length)
{
    if (input == NULL)
    {
        return NULL;
    }

    char *output = (char *)malloc(length + 1U);
    if (output == NULL)
    {
        return NULL;
    }

    bool in_block = false;
    bool in_line = false;
    size_t out_index = 0U;

    for (size_t index = 0; index < length; ++index)
    {
        char c = input[index];
        char next = (index + 1U < length) ? input[index + 1U] : '\0';

        if (in_block)
        {
            if (c == '*' && next == '/')
            {
                in_block = false;
                ++index;
            }
            else if (c == '\n')
            {
                output[out_index++] = c;
            }
            continue;
        }

        if (in_line)
        {
            if (c == '\n')
            {
                in_line = false;
                output[out_index++] = c;
            }
            continue;
        }

        if (c == '/' && next == '*')
        {
            in_block = true;
            ++index;
            continue;
        }

        if (c == '/' && next == '/')
        {
            in_line = true;
            ++index;
            continue;
        }

        output[out_index++] = c;
    }

    output[out_index] = '\0';
    return output;
}

static bool AAS_Sound_ParseConfigBuffer(char *buffer, const char *log_path)
{
    if (buffer == NULL)
    {
        return false;
    }

    AAS_Sound_ClearMacros();

    bool inside_block = false;
    bool awaiting_block = false;
    aas_soundinfo_t info;
    memset(&info, 0, sizeof(info));

    char *line = buffer;
    while (line != NULL)
    {
        char *next_line = strchr(line, '\n');
        if (next_line != NULL)
        {
            *next_line = '\0';
            ++next_line;
        }

        char *trimmed = AAS_Sound_Trim(line);
        if (trimmed == NULL || *trimmed == '\0')
        {
            line = next_line;
            continue;
        }

        if (strncmp(trimmed, "#define", 7) == 0)
        {
            AAS_Sound_ParseDefineLine(trimmed);
            line = next_line;
            continue;
        }

        if (!inside_block)
        {
            if (strncmp(trimmed, "soundinfo", 9) == 0)
            {
                awaiting_block = true;
                if (strchr(trimmed, '{') != NULL)
                {
                    inside_block = true;
                    awaiting_block = false;
                    memset(&info, 0, sizeof(info));
                }
            }
            else if (awaiting_block && strchr(trimmed, '{') != NULL)
            {
                inside_block = true;
                awaiting_block = false;
                memset(&info, 0, sizeof(info));
            }

            line = next_line;
            continue;
        }

        if (strchr(trimmed, '{') != NULL)
        {
            line = next_line;
            continue;
        }

        if (strchr(trimmed, '}') != NULL)
        {
            if (info.name[0] != '\0')
            {
                if (info.normalized[0] == '\0')
                {
                    AAS_Sound_NormalizeName(info.name, info.normalized, sizeof(info.normalized));
                }
                AAS_Sound_PushInfo(&info);
            }
            else
            {
                BotLib_Print(PRT_WARNING,
                             "AAS_Sound: soundinfo missing name in %s\n",
                             (log_path != NULL) ? log_path : "<memory>");
            }

            inside_block = false;
            line = next_line;
            continue;
        }

        char *cursor = trimmed;
        char key[64];
        if (!AAS_Sound_ParseBareToken(&cursor, key, sizeof(key)))
        {
            line = next_line;
            continue;
        }

        if (strcmp(key, "name") == 0)
        {
            char value[128];
            if (AAS_Sound_ParseStringToken(&cursor, value, sizeof(value)))
            {
                strncpy(info.name, value, sizeof(info.name) - 1U);
                info.name[sizeof(info.name) - 1U] = '\0';
                AAS_Sound_NormalizeName(info.name, info.normalized, sizeof(info.normalized));
            }
        }
        else if (strcmp(key, "string") == 0)
        {
            char value[128];
            if (AAS_Sound_ParseStringToken(&cursor, value, sizeof(value)))
            {
                strncpy(info.subtitle, value, sizeof(info.subtitle) - 1U);
                info.subtitle[sizeof(info.subtitle) - 1U] = '\0';
            }
        }
        else if (strcmp(key, "volume") == 0)
        {
            char value_text[64];
            if (AAS_Sound_ParseBareToken(&cursor, value_text, sizeof(value_text)))
            {
                float parsed = 0.0f;
                if (AAS_Sound_ParseFloatFromString(value_text, &parsed))
                {
                    info.volume = parsed;
                }
            }
        }
        else if (strcmp(key, "duration") == 0)
        {
            char value_text[64];
            if (AAS_Sound_ParseBareToken(&cursor, value_text, sizeof(value_text)))
            {
                float parsed = 0.0f;
                if (AAS_Sound_ParseFloatFromString(value_text, &parsed))
                {
                    info.duration = parsed;
                }
            }
        }
        else if (strcmp(key, "type") == 0)
        {
            char value_text[64];
            if (AAS_Sound_ParseBareToken(&cursor, value_text, sizeof(value_text)))
            {
                int parsed = 0;
                if (AAS_Sound_ParseIntFromString(value_text, &parsed))
                {
                    info.type = parsed;
                }
            }
        }
        else if (strcmp(key, "recognition") == 0)
        {
            char value_text[64];
            if (AAS_Sound_ParseBareToken(&cursor, value_text, sizeof(value_text)))
            {
                float parsed = 0.0f;
                if (AAS_Sound_ParseFloatFromString(value_text, &parsed))
                {
                    info.recognition = parsed;
                }
            }
        }

        line = next_line;
    }

    if (inside_block)
    {
        BotLib_Print(PRT_ERROR,
                     "AAS_Sound: soundinfo missing closing brace in %s\n",
                     (log_path != NULL) ? log_path : "<memory>");
        return false;
    }

    return true;
}

static bool AAS_Sound_EnsureInfoCapacity(void)
{
    if (g_aas_sound_state.info_capacity == 0U)
    {
        return false;
    }

    if (g_aas_sound_state.infos != NULL)
    {
        return true;
    }

    g_aas_sound_state.infos =
        (aas_soundinfo_t *)calloc(g_aas_sound_state.info_capacity, sizeof(aas_soundinfo_t));
    return (g_aas_sound_state.infos != NULL);
}

static bool AAS_Sound_PushInfo(const aas_soundinfo_t *info)
{
    if (info == NULL)
    {
        return false;
    }

    if (!AAS_Sound_EnsureInfoCapacity())
    {
        return false;
    }

    if (g_aas_sound_state.info_count >= g_aas_sound_state.info_capacity)
    {
        BotLib_Print(PRT_WARNING,
                     "AAS_Sound: discarding soundinfo '%s' (max %zu)\n",
                     info->name,
                     g_aas_sound_state.info_capacity);
        return false;
    }

    g_aas_sound_state.infos[g_aas_sound_state.info_count++] = *info;
    return true;
}


static void AAS_Sound_FreeInfos(void)
{
    free(g_aas_sound_state.infos);
    g_aas_sound_state.infos = NULL;
    g_aas_sound_state.info_count = 0U;
    g_aas_sound_state.info_capacity = 0U;
}

static void AAS_Sound_FreeAssets(void)
{
    if (g_aas_sound_state.asset_names != NULL)
    {
        for (size_t i = 0; i < g_aas_sound_state.asset_count; ++i)
        {
            free(g_aas_sound_state.asset_names[i]);
            free(g_aas_sound_state.asset_normalized[i]);
        }
    }

    free(g_aas_sound_state.asset_names);
    free(g_aas_sound_state.asset_normalized);
    free(g_aas_sound_state.asset_info_index);

    g_aas_sound_state.asset_names = NULL;
    g_aas_sound_state.asset_normalized = NULL;
    g_aas_sound_state.asset_info_index = NULL;
    g_aas_sound_state.asset_count = 0U;
}

static void AAS_Sound_FreeEvents(void)
{
    free(g_aas_sound_state.sound_events);
    free(g_aas_sound_state.pointlight_events);

    g_aas_sound_state.sound_events = NULL;
    g_aas_sound_state.pointlight_events = NULL;
    g_aas_sound_state.sound_event_count = 0U;
    g_aas_sound_state.pointlight_event_count = 0U;
    g_aas_sound_state.sound_event_capacity = 0U;
    g_aas_sound_state.pointlight_event_capacity = 0U;
}

int AAS_SoundSubsystem_Init(const botlib_library_variables_t *vars)
{
    if (vars == NULL)
    {
        return BLERR_INVALIDIMPORT;
    }

    AAS_SoundSubsystem_Shutdown();

    int max_soundinfo = AAS_Sound_ClampSoundInfoLimit(vars->max_soundinfo);
    int max_aassounds = AAS_Sound_ClampAASSoundLimit(vars->max_aassounds);

    if (max_soundinfo <= 0)
    {
        BotLib_Print(PRT_WARNING, "AAS_Sound: max_soundinfo disabled\n");
        return BLERR_NOERROR;
    }

    g_aas_sound_state.info_capacity = (size_t)max_soundinfo;
    g_aas_sound_state.sound_event_capacity =
        (max_aassounds > 0) ? (size_t)max_aassounds : (size_t)max_soundinfo;
    g_aas_sound_state.pointlight_event_capacity = g_aas_sound_state.sound_event_capacity;

    if (g_aas_sound_state.sound_event_capacity > 0U)
    {
        g_aas_sound_state.sound_events =
            (aas_sound_event_t *)calloc(g_aas_sound_state.sound_event_capacity,
                                        sizeof(aas_sound_event_t));
        g_aas_sound_state.pointlight_events =
            (aas_pointlight_event_t *)calloc(g_aas_sound_state.pointlight_event_capacity,
                                             sizeof(aas_pointlight_event_t));
        if (g_aas_sound_state.sound_events == NULL || g_aas_sound_state.pointlight_events == NULL)
        {
            AAS_SoundSubsystem_Shutdown();
            return BLERR_INVALIDIMPORT;
        }
    }

    char resolved_path[BOTLIB_ASSET_MAX_PATH];
    const char *requested = (vars->soundconfig[0] != '\0') ? vars->soundconfig : "sounds.c";
    if (!BotLib_ResolveAssetPath(requested, "soundconfig", resolved_path, sizeof(resolved_path)))
    {
        BotLib_Print(PRT_ERROR,
                     "AAS_Sound: failed to resolve sound config '%s'\n",
                     requested);
        AAS_SoundSubsystem_Shutdown();
        return BLERR_INVALIDIMPORT;
    }

    FILE *file = fopen(resolved_path, "rb");
    if (file == NULL)
    {
        BotLib_Print(PRT_ERROR,
                     "AAS_Sound: failed to open sound config %s (%s)\n",
                     resolved_path,
                     strerror(errno));
        AAS_SoundSubsystem_Shutdown();
        return BLERR_INVALIDIMPORT;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        BotLib_Print(PRT_ERROR,
                     "AAS_Sound: failed to seek sound config %s\n",
                     resolved_path);
        fclose(file);
        AAS_SoundSubsystem_Shutdown();
        return BLERR_INVALIDIMPORT;
    }

    long file_length = ftell(file);
    if (file_length < 0)
    {
        BotLib_Print(PRT_ERROR,
                     "AAS_Sound: failed to determine size of %s\n",
                     resolved_path);
        fclose(file);
        AAS_SoundSubsystem_Shutdown();
        return BLERR_INVALIDIMPORT;
    }

    if (fseek(file, 0, SEEK_SET) != 0)
    {
        BotLib_Print(PRT_ERROR,
                     "AAS_Sound: failed to rewind %s\n",
                     resolved_path);
        fclose(file);
        AAS_SoundSubsystem_Shutdown();
        return BLERR_INVALIDIMPORT;
    }

    char *raw_buffer = (char *)malloc((size_t)file_length + 1U);
    if (raw_buffer == NULL)
    {
        BotLib_Print(PRT_ERROR, "AAS_Sound: out of memory reading %s\n", resolved_path);
        fclose(file);
        AAS_SoundSubsystem_Shutdown();
        return BLERR_INVALIDIMPORT;
    }

    size_t read = fread(raw_buffer, 1U, (size_t)file_length, file);
    fclose(file);
    if (read != (size_t)file_length)
    {
        BotLib_Print(PRT_ERROR, "AAS_Sound: failed to read %s\n", resolved_path);
        free(raw_buffer);
        AAS_SoundSubsystem_Shutdown();
        return BLERR_INVALIDIMPORT;
    }
    raw_buffer[file_length] = '\0';

    char *sanitized = AAS_Sound_RemoveComments(raw_buffer, (size_t)file_length);
    free(raw_buffer);
    if (sanitized == NULL)
    {
        BotLib_Print(PRT_ERROR, "AAS_Sound: failed to process %s\n", resolved_path);
        AAS_SoundSubsystem_Shutdown();
        return BLERR_INVALIDIMPORT;
    }

    bool parsed = AAS_Sound_ParseConfigBuffer(sanitized, resolved_path);
    free(sanitized);

    if (!parsed)
    {
        AAS_SoundSubsystem_Shutdown();
        return BLERR_INVALIDIMPORT;
    }

    g_aas_sound_state.initialised = true;
    return BLERR_NOERROR;
}

void AAS_SoundSubsystem_Shutdown(void)
{
    AAS_Sound_FreeInfos();
    AAS_Sound_FreeAssets();
    AAS_Sound_FreeEvents();
    memset(&g_aas_sound_state, 0, sizeof(g_aas_sound_state));
}

void AAS_SoundSubsystem_ClearMapAssets(void)
{
    AAS_Sound_FreeAssets();
}

bool AAS_SoundSubsystem_RegisterMapAssets(int count, char *assets[])
{
    AAS_Sound_FreeAssets();

    if (count <= 0 || assets == NULL)
    {
        return true;
    }

    size_t desired = (size_t)count;
    g_aas_sound_state.asset_names =
        (char **)calloc(desired, sizeof(char *));
    g_aas_sound_state.asset_normalized =
        (char **)calloc(desired, sizeof(char *));
    g_aas_sound_state.asset_info_index =
        (int *)calloc(desired, sizeof(int));
    if (g_aas_sound_state.asset_names == NULL || g_aas_sound_state.asset_normalized == NULL
        || g_aas_sound_state.asset_info_index == NULL)
    {
        AAS_Sound_FreeAssets();
        return false;
    }

    for (size_t i = 0; i < desired; ++i)
    {
        const char *name = assets[i];
        if (name == NULL)
        {
            continue;
        }

        size_t length = strlen(name);
        g_aas_sound_state.asset_names[i] = (char *)malloc(length + 1U);
        g_aas_sound_state.asset_normalized[i] = (char *)malloc(length + 1U);
        if (g_aas_sound_state.asset_names[i] == NULL
            || g_aas_sound_state.asset_normalized[i] == NULL)
        {
            AAS_Sound_FreeAssets();
            return false;
        }

        memcpy(g_aas_sound_state.asset_names[i], name, length + 1U);
        AAS_Sound_NormalizeName(name,
                                g_aas_sound_state.asset_normalized[i],
                                length + 1U);

        int info_index = AAS_SoundSubsystem_FindInfoIndex(g_aas_sound_state.asset_normalized[i]);
        g_aas_sound_state.asset_info_index[i] = info_index;
    }

    g_aas_sound_state.asset_count = desired;
    return true;
}

void AAS_SoundSubsystem_SetFrameTime(float time)
{
    g_aas_sound_state.frame_time = time;
}

void AAS_SoundSubsystem_ResetFrameEvents(void)
{
    g_aas_sound_state.sound_event_count = 0U;
    g_aas_sound_state.pointlight_event_count = 0U;
}

static void AAS_Sound_SetEntitySound(int ent, int soundindex)
{
    if (!aasworld.loaded || aasworld.entities == NULL || ent < 0 || ent >= aasworld.maxEntities)
    {
        return;
    }

    aasworld.entities[ent].sound = soundindex;
    aasworld.entities[ent].lastUpdateTime = g_aas_sound_state.frame_time;
}

bool AAS_SoundSubsystem_RecordSound(const vec3_t origin,
                                    int ent,
                                    int channel,
                                    int soundindex,
                                    float volume,
                                    float attenuation,
                                    float timeofs)
{
    if (!g_aas_sound_state.initialised || g_aas_sound_state.sound_events == NULL
        || g_aas_sound_state.sound_event_capacity == 0U)
    {
        return false;
    }

    if (g_aas_sound_state.sound_event_count == g_aas_sound_state.sound_event_capacity)
    {
        memmove(&g_aas_sound_state.sound_events[0],
                &g_aas_sound_state.sound_events[1],
                (g_aas_sound_state.sound_event_capacity - 1U) * sizeof(aas_sound_event_t));
        g_aas_sound_state.sound_event_count -= 1U;
    }

    aas_sound_event_t *event =
        &g_aas_sound_state.sound_events[g_aas_sound_state.sound_event_count++];

    if (origin != NULL)
    {
        VectorCopy(origin, event->origin);
    }
    else
    {
        VectorClear(event->origin);
    }

    event->ent = ent;
    event->channel = channel;
    event->soundindex = soundindex;
    event->volume = volume;
    event->attenuation = attenuation;
    event->timeofs = timeofs;
    event->timestamp = g_aas_sound_state.frame_time + timeofs;
    event->info_index =
        (soundindex >= 0 && (size_t)soundindex < g_aas_sound_state.asset_count)
            ? g_aas_sound_state.asset_info_index[soundindex]
            : -1;

    if (soundindex >= 0)
    {
        AAS_Sound_SetEntitySound(ent, soundindex);
    }

    return true;
}

bool AAS_SoundSubsystem_RecordPointLight(const vec3_t origin,
                                         int ent,
                                         float radius,
                                         float r,
                                         float g,
                                         float b,
                                         float time,
                                         float decay)
{
    if (!g_aas_sound_state.initialised || g_aas_sound_state.pointlight_events == NULL
        || g_aas_sound_state.pointlight_event_capacity == 0U)
    {
        return false;
    }

    if (g_aas_sound_state.pointlight_event_count == g_aas_sound_state.pointlight_event_capacity)
    {
        memmove(&g_aas_sound_state.pointlight_events[0],
                &g_aas_sound_state.pointlight_events[1],
                (g_aas_sound_state.pointlight_event_capacity - 1U)
                    * sizeof(aas_pointlight_event_t));
        g_aas_sound_state.pointlight_event_count -= 1U;
    }

    aas_pointlight_event_t *event =
        &g_aas_sound_state.pointlight_events[g_aas_sound_state.pointlight_event_count++];

    if (origin != NULL)
    {
        VectorCopy(origin, event->origin);
    }
    else
    {
        VectorClear(event->origin);
    }

    event->ent = ent;
    event->radius = radius;
    event->color[0] = r;
    event->color[1] = g;
    event->color[2] = b;
    event->time = time;
    event->decay = decay;
    return true;
}

size_t AAS_SoundSubsystem_SoundEventCount(void)
{
    return g_aas_sound_state.sound_event_count;
}

const aas_sound_event_t *AAS_SoundSubsystem_SoundEvent(size_t index)
{
    if (index >= g_aas_sound_state.sound_event_count)
    {
        return NULL;
    }
    return &g_aas_sound_state.sound_events[index];
}

size_t AAS_SoundSubsystem_PointLightCount(void)
{
    return g_aas_sound_state.pointlight_event_count;
}

const aas_pointlight_event_t *AAS_SoundSubsystem_PointLight(size_t index)
{
    if (index >= g_aas_sound_state.pointlight_event_count)
    {
        return NULL;
    }
    return &g_aas_sound_state.pointlight_events[index];
}

static float AAS_Sound_EventDuration(const aas_sound_event_t *event)
{
    if (event == NULL)
    {
        return 0.0f;
    }

    float duration = AAS_SOUND_DEFAULT_DURATION;
    if (event->info_index >= 0 && (size_t)event->info_index < g_aas_sound_state.info_count)
    {
        const aas_soundinfo_t *info = &g_aas_sound_state.infos[event->info_index];
        if (info->duration > 0.0f)
        {
            duration = info->duration;
        }
    }
    return duration;
}

size_t AAS_SoundSubsystem_QuerySoundSummaries(float now,
                                              aas_sound_event_summary_t *summaries,
                                              size_t max_summaries)
{
    if (!g_aas_sound_state.initialised || summaries == NULL || max_summaries == 0U)
    {
        return 0U;
    }

    size_t produced = 0U;
    for (size_t index = g_aas_sound_state.sound_event_count; index > 0 && produced < max_summaries; --index)
    {
        const aas_sound_event_t *event = &g_aas_sound_state.sound_events[index - 1U];
        float timestamp = event->timestamp;
        float expiry = timestamp + AAS_Sound_EventDuration(event);
        if (timestamp > now || expiry < now)
        {
            continue;
        }

        aas_sound_event_summary_t *summary = &summaries[produced];
        VectorCopy(event->origin, summary->origin);
        summary->ent = event->ent;
        summary->soundindex = event->soundindex;
        summary->timestamp = timestamp;
        summary->expiry = expiry;
        summary->volume = event->volume;

        if (event->info_index >= 0 && (size_t)event->info_index < g_aas_sound_state.info_count)
        {
            summary->type = g_aas_sound_state.infos[event->info_index].type;
        }
        else
        {
            summary->type = AAS_SOUNDTYPE_IGNORE;
        }

        produced += 1U;
    }

    return produced;
}

size_t AAS_SoundSubsystem_QueryPointLightSummaries(
    float now,
    aas_pointlight_event_summary_t *summaries,
    size_t max_summaries)
{
    if (!g_aas_sound_state.initialised || summaries == NULL || max_summaries == 0U)
    {
        return 0U;
    }

    size_t produced = 0U;
    for (size_t index = g_aas_sound_state.pointlight_event_count; index > 0 && produced < max_summaries; --index)
    {
        const aas_pointlight_event_t *event = &g_aas_sound_state.pointlight_events[index - 1U];
        float timestamp = (event->time > 0.0f) ? event->time : g_aas_sound_state.frame_time;
        float decay = (event->decay > 0.0f) ? event->decay : AAS_POINTLIGHT_DEFAULT_DECAY;
        float expiry = timestamp + decay;
        if (timestamp > now || expiry < now)
        {
            continue;
        }

        aas_pointlight_event_summary_t *summary = &summaries[produced];
        VectorCopy(event->origin, summary->origin);
        summary->ent = event->ent;
        summary->radius = event->radius;
        summary->timestamp = timestamp;
        summary->expiry = expiry;

        produced += 1U;
    }

    return produced;
}

size_t AAS_SoundSubsystem_InfoCount(void)
{
    return g_aas_sound_state.info_count;
}

const aas_soundinfo_t *AAS_SoundSubsystem_Info(size_t index)
{
    if (index >= g_aas_sound_state.info_count)
    {
        return NULL;
    }
    return &g_aas_sound_state.infos[index];
}

int AAS_SoundSubsystem_FindInfoIndex(const char *name)
{
    if (name == NULL || name[0] == '\0' || g_aas_sound_state.infos == NULL)
    {
        return -1;
    }

    char normalized[128];
    AAS_Sound_NormalizeName(name, normalized, sizeof(normalized));

    for (size_t i = 0; i < g_aas_sound_state.info_count; ++i)
    {
        const aas_soundinfo_t *info = &g_aas_sound_state.infos[i];
        if (strcmp(info->normalized, normalized) == 0)
        {
            return (int)i;
        }
    }

    return -1;
}

const aas_soundinfo_t *AAS_SoundSubsystem_InfoForSoundIndex(int soundindex)
{
    if (soundindex < 0 || (size_t)soundindex >= g_aas_sound_state.asset_count
        || g_aas_sound_state.asset_info_index == NULL)
    {
        return NULL;
    }

    int info_index = g_aas_sound_state.asset_info_index[soundindex];
    if (info_index < 0 || (size_t)info_index >= g_aas_sound_state.info_count)
    {
        return NULL;
    }

    return &g_aas_sound_state.infos[info_index];
}

const char *AAS_SoundSubsystem_AssetName(int soundindex)
{
    if (soundindex < 0 || (size_t)soundindex >= g_aas_sound_state.asset_count
        || g_aas_sound_state.asset_names == NULL)
    {
        return NULL;
    }
    return g_aas_sound_state.asset_names[soundindex];
}

int AAS_SoundSubsystem_SoundTypeForIndex(int soundindex)
{
    const aas_soundinfo_t *info = AAS_SoundSubsystem_InfoForSoundIndex(soundindex);
    return (info != NULL) ? info->type : 0;
}
