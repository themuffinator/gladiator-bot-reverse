#include "l_utils.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static bool g_l_utils_initialised = false;

bool L_Utils_Init(void) {
    if (g_l_utils_initialised) {
        return true;
    }

    g_l_utils_initialised = true;
    return true;
}

void L_Utils_Shutdown(void) {
    g_l_utils_initialised = false;
}

bool L_Utils_IsInitialised(void) {
    return g_l_utils_initialised;
}

void Vector2Angles(const vec3_t value1, vec3_t angles) {
    if (value1 == NULL || angles == NULL) {
        return;
    }

    float yaw;
    float pitch;

    if (value1[0] == 0.0f && value1[1] == 0.0f) {
        yaw = 0.0f;
        pitch = (value1[2] > 0.0f) ? 90.0f : 270.0f;
    } else {
        yaw = (float)(atan2(value1[1], value1[0]) * (180.0 / M_PI));
        if (yaw < 0.0f) {
            yaw += 360.0f;
        }

        const float forward = sqrtf(value1[0] * value1[0] + value1[1] * value1[1]);
        pitch = (float)(atan2(value1[2], forward) * (180.0 / M_PI));
        if (pitch < 0.0f) {
            pitch += 360.0f;
        }
    }

    angles[PITCH] = -pitch;
    angles[YAW] = yaw;
    angles[ROLL] = 0.0f;
}

void ConvertPath(char *path) {
    if (path == NULL) {
        return;
    }

    for (char *cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '/' || *cursor == '\\') {
            *cursor = BOTLIB_PATH_SEPARATOR_CHAR;
        }
    }
}

void AppendPathSeperator(char *path, size_t length) {
    if (path == NULL || length == 0) {
        return;
    }

    size_t pathlen = strlen(path);
    if (pathlen == 0 || pathlen + 1 >= length) {
        return;
    }

    char last = path[pathlen - 1];
    if (last == '/' || last == '\\') {
        return;
    }

    path[pathlen++] = BOTLIB_PATH_SEPARATOR_CHAR;
    path[pathlen] = '\0';
}

float AngleNormalize360(float angle) {
    float result = fmodf(angle, 360.0f);
    if (result < 0.0f) {
        result += 360.0f;
    }
    return result;
}

float AngleNormalize180(float angle) {
    float result = AngleNormalize360(angle);
    if (result > 180.0f) {
        result -= 360.0f;
    }
    return result;
}

float AngleMod(float angle) {
    return AngleNormalize360(angle);
}

float AngleDelta(float angle1, float angle2) {
    return AngleNormalize180(angle1 - angle2);
}

float VectorLengthSquared(const vec3_t v) {
    if (v == NULL) {
        return 0.0f;
    }

    return v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
}

float DistanceSquared(const vec3_t v1, const vec3_t v2) {
    if (v1 == NULL || v2 == NULL) {
        return 0.0f;
    }

    const float dx = v1[0] - v2[0];
    const float dy = v1[1] - v2[1];
    const float dz = v1[2] - v2[2];
    return dx * dx + dy * dy + dz * dz;
}

bool BotUtils_FileExists(const char *path) {
    if (path == NULL || *path == '\0') {
        return false;
    }

    struct stat info;
    return stat(path, &info) == 0;
}

void BotUtils_StripExtension(const char *path, char *out, size_t out_size) {
    if (out == NULL || out_size == 0) {
        return;
    }

    if (path == NULL) {
        out[0] = '\0';
        return;
    }

    strncpy(out, path, out_size);
    out[out_size - 1] = '\0';

    char *last_sep = strrchr(out, '/');
    char *last_back = strrchr(out, '\\');
    if (last_back != NULL && (last_sep == NULL || last_back > last_sep)) {
        last_sep = last_back;
    }
    char *last_dot = strrchr(out, '.');

    if (last_dot != NULL && (last_sep == NULL || last_dot > last_sep)) {
        *last_dot = '\0';
    }
}
