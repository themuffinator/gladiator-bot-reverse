#include "l_utils.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static bool g_l_utils_initialised = false;

/*
=============
L_Utils_Init

Marks the compatibility utility layer initialized.
=============
*/
bool L_Utils_Init(void)
{
	if (g_l_utils_initialised)
	{
		return true;
	}

	g_l_utils_initialised = true;
	return true;
}

/*
=============
L_Utils_Shutdown

Clears the compatibility utility initialization marker.
=============
*/
void L_Utils_Shutdown(void)
{
	g_l_utils_initialised = false;
}

/*
=============
L_Utils_IsInitialised

Reports the compatibility utility initialization marker.
=============
*/
bool L_Utils_IsInitialised(void)
{
	return g_l_utils_initialised;
}

/*
=============
Vector2Angles

Converts a direction to the whole-degree pitch and yaw used by retail botlib.
=============
*/
void Vector2Angles(const vec3_t value1, vec3_t angles)
{
	if (value1 == NULL || angles == NULL)
	{
		return;
	}

	float yaw;
	float pitch;

	if (value1[0] == 0.0f && value1[1] == 0.0f)
	{
		yaw = 0.0f;
		pitch = (value1[2] > 0.0f) ? 90.0f : 270.0f;
	}
	else
	{
		yaw = (float)(int)(atan2(value1[1], value1[0]) *
			(180.0 / M_PI));
		if (yaw < 0.0f)
		{
			yaw += 360.0f;
		}

		float forward = sqrtf(value1[0] * value1[0] +
			value1[1] * value1[1]);
		pitch = (float)(int)(atan2(value1[2], forward) *
			(180.0 / M_PI));
		if (pitch < 0.0f)
		{
			pitch += 360.0f;
		}
	}

	angles[PITCH] = -pitch;
	angles[YAW] = yaw;
	angles[ROLL] = 0.0f;
}

/*
=============
ConvertPath

Converts both slash forms to the retail platform path separator.
=============
*/
void ConvertPath(char *path)
{
	if (path == NULL)
	{
		return;
	}

	for (char *cursor = path; *cursor != '\0'; ++cursor)
	{
		if (*cursor == '/' || *cursor == '\\')
		{
			*cursor = BOTLIB_PATH_SEPARATOR_CHAR;
		}
	}
}

/*
=============
AppendPathSeperator

Appends the retail separator using the original signed buffer-length check.
=============
*/
void AppendPathSeperator(char *path, int length)
{
	if (path == NULL)
	{
		return;
	}

	int path_length = (int)strlen(path);
	if (path_length != 0 && length - path_length > 1 &&
		path[path_length - 1] != '/' && path[path_length - 1] != '\\')
	{
		path[path_length] = BOTLIB_PATH_SEPARATOR_CHAR;
		path[path_length + 1] = '\0';
	}
}

/*
=============
AngleNormalize360

Quantizes an angle through the unsigned 16-bit Quake angle representation.
=============
*/
float AngleNormalize360(float angle)
{
	return (360.0f / 65536.0f) *
		(float)(((int)(angle * (65536.0f / 360.0f))) & 65535);
}

/*
=============
AngleNormalize180

Maps a quantized retail angle to the signed half-circle range.
=============
*/
float AngleNormalize180(float angle)
{
	float result = AngleNormalize360(angle);
	if (result > 180.0f)
	{
		result -= 360.0f;
	}
	return result;
}

/*
=============
AngleMod

Returns the retail 16-bit-grid representation of an angle.
=============
*/
float AngleMod(float angle)
{
	return AngleNormalize360(angle);
}

/*
=============
AngleDelta

Returns the signed quantized difference between two angles.
=============
*/
float AngleDelta(float angle1, float angle2)
{
	return AngleNormalize180(angle1 - angle2);
}

/*
=============
VectorLengthSquared

Returns the squared magnitude of a vector.
=============
*/
float VectorLengthSquared(const vec3_t v)
{
	if (v == NULL)
	{
		return 0.0f;
	}

	return v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
}

/*
=============
DistanceSquared

Returns the squared distance between two vectors.
=============
*/
float DistanceSquared(const vec3_t v1, const vec3_t v2)
{
	if (v1 == NULL || v2 == NULL)
	{
		return 0.0f;
	}

	float dx = v1[0] - v2[0];
	float dy = v1[1] - v2[1];
	float dz = v1[2] - v2[2];
	return dx * dx + dy * dy + dz * dz;
}

/*
=============
BotUtils_FileExists

Reports whether a compatibility asset path exists.
=============
*/
bool BotUtils_FileExists(const char *path)
{
	if (path == NULL || *path == '\0')
	{
		return false;
	}

	struct stat info;
	return stat(path, &info) == 0;
}

/*
=============
BotUtils_StripExtension

Copies a compatibility asset path without its final filename extension.
=============
*/
void BotUtils_StripExtension(const char *path, char *out, size_t out_size)
{
	if (out == NULL || out_size == 0)
	{
		return;
	}

	if (path == NULL)
	{
		out[0] = '\0';
		return;
	}

	strncpy(out, path, out_size);
	out[out_size - 1] = '\0';

	char *last_separator = strrchr(out, '/');
	char *last_backslash = strrchr(out, '\\');
	if (last_backslash != NULL &&
		(last_separator == NULL || last_backslash > last_separator))
	{
		last_separator = last_backslash;
	}
	char *last_dot = strrchr(out, '.');

	if (last_dot != NULL &&
		(last_separator == NULL || last_dot > last_separator))
	{
		*last_dot = '\0';
	}
}
