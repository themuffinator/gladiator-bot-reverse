#ifndef GLADIATOR_BOTLIB_AAS_AAS_SOUND_H
#define GLADIATOR_BOTLIB_AAS_AAS_SOUND_H

#include <stdbool.h>
#include <stddef.h>

#include "../../shared/q_shared.h"
#include "../interface/botlib_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct aas_soundinfo_s
{
    char name[128];
    char normalized[128];
    char subtitle[128];
    float volume;
    float duration;
    int type;
    float recognition;
} aas_soundinfo_t;

enum
{
    AAS_SOUNDTYPE_IGNORE = 0,
    AAS_SOUNDTYPE_PLAYER = 1,
    AAS_SOUNDTYPE_PLAYERSTEPS = 2,
    AAS_SOUNDTYPE_PLAYERJUMP = 3,
    AAS_SOUNDTYPE_PLAYERWATERIN = 4,
    AAS_SOUNDTYPE_PLAYERWATEROUT = 5,
    AAS_SOUNDTYPE_PLAYERFALL = 6,
    AAS_SOUNDTYPE_FIRINGWEAPON = 7,
    AAS_SOUNDTYPE_USINGPOWERUP = 8,
    AAS_SOUNDTYPE_PICKUPWEAPON = 9,
    AAS_SOUNDTYPE_PICKUPAMMO = 10,
    AAS_SOUNDTYPE_PICKUPARMOR = 11,
    AAS_SOUNDTYPE_PICKUPARMORSHARD = 12,
    AAS_SOUNDTYPE_PICKUPPOWERUP = 13,
    AAS_SOUNDTYPE_PICKUPHEALTH_SMALL = 14,
    AAS_SOUNDTYPE_PICKUPHEALTH_NORMAL = 15,
    AAS_SOUNDTYPE_PICKUPHEALTH_LARGE = 16,
    AAS_SOUNDTYPE_PICKUPHEALTH_MEGA = 17,
    AAS_SOUNDTYPE_DOOR = 18,
    AAS_SOUNDTYPE_ELEVATOR = 19,
    AAS_SOUNDTYPE_TELEPORT = 20,
};

typedef struct aas_sound_event_s
{
    vec3_t origin;
    int ent;
    int channel;
    int soundindex;
    int info_index;
    float volume;
    float attenuation;
    float timeofs;
    float timestamp;
} aas_sound_event_t;

typedef struct aas_pointlight_event_s
{
    vec3_t origin;
    int ent;
    float radius;
    float color[3];
    float time;
    float decay;
} aas_pointlight_event_t;

typedef struct aas_sound_event_summary_s
{
    vec3_t origin;
    int ent;
    int soundindex;
    int type;
    float timestamp;
    float expiry;
    float volume;
} aas_sound_event_summary_t;

typedef struct aas_pointlight_event_summary_s
{
    vec3_t origin;
    int ent;
    float timestamp;
    float expiry;
    float radius;
} aas_pointlight_event_summary_t;

int AAS_SoundSubsystem_Init(const botlib_library_variables_t *vars);
void AAS_SoundSubsystem_Shutdown(void);

void AAS_SoundSubsystem_ClearMapAssets(void);
bool AAS_SoundSubsystem_RegisterMapAssets(int count, char *assets[]);

void AAS_SoundSubsystem_SetFrameTime(float time);
void AAS_SoundSubsystem_ResetFrameEvents(void);

bool AAS_SoundSubsystem_RecordSound(const vec3_t origin,
                                    int ent,
                                    int channel,
                                    int soundindex,
                                    float volume,
                                    float attenuation,
                                    float timeofs);

bool AAS_SoundSubsystem_RecordPointLight(const vec3_t origin,
                                         int ent,
                                         float radius,
                                         float r,
                                         float g,
                                         float b,
                                         float time,
                                         float decay);

size_t AAS_SoundSubsystem_SoundEventCount(void);
const aas_sound_event_t *AAS_SoundSubsystem_SoundEvent(size_t index);

size_t AAS_SoundSubsystem_PointLightCount(void);
const aas_pointlight_event_t *AAS_SoundSubsystem_PointLight(size_t index);

size_t AAS_SoundSubsystem_QuerySoundSummaries(float now,
                                              aas_sound_event_summary_t *summaries,
                                              size_t max_summaries);
size_t AAS_SoundSubsystem_QueryPointLightSummaries(
    float now,
    aas_pointlight_event_summary_t *summaries,
    size_t max_summaries);

size_t AAS_SoundSubsystem_InfoCount(void);
const aas_soundinfo_t *AAS_SoundSubsystem_Info(size_t index);
int AAS_SoundSubsystem_FindInfoIndex(const char *name);
const aas_soundinfo_t *AAS_SoundSubsystem_InfoForSoundIndex(int soundindex);
const char *AAS_SoundSubsystem_AssetName(int soundindex);
int AAS_SoundSubsystem_SoundTypeForIndex(int soundindex);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GLADIATOR_BOTLIB_AAS_AAS_SOUND_H */
