#ifndef BOTLIB_COMMON_L_UTILS_H
#define BOTLIB_COMMON_L_UTILS_H

#include <stdbool.h>
#include <stddef.h>

#include "shared/q_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#define BOTLIB_PATH_SEPARATOR_STR "\\"
#define BOTLIB_PATH_SEPARATOR_CHAR '\\'
#else
#define BOTLIB_PATH_SEPARATOR_STR "/"
#define BOTLIB_PATH_SEPARATOR_CHAR '/'
#endif

bool L_Utils_Init(void);
void L_Utils_Shutdown(void);
bool L_Utils_IsInitialised(void);

void Vector2Angles(const vec3_t value1, vec3_t angles);
void ConvertPath(char *path);
void AppendPathSeperator(char *path, size_t length);

float AngleNormalize360(float angle);
float AngleNormalize180(float angle);
float AngleMod(float angle);
float AngleDelta(float angle1, float angle2);

float VectorLengthSquared(const vec3_t v);
float DistanceSquared(const vec3_t v1, const vec3_t v2);

bool BotUtils_FileExists(const char *path);
void BotUtils_StripExtension(const char *path, char *out, size_t out_size);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // BOTLIB_COMMON_L_UTILS_H
