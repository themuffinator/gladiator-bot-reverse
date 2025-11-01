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
