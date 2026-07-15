#ifndef BOTLIB_COMMON_L_LIBVAR_H
#define BOTLIB_COMMON_L_LIBVAR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct libvar_s {
	char *name;
	char *string;
	int flags;
	int modified;
	float value;
	struct libvar_s *next;
} libvar_t;

float LibVarStringValue(const char *string);
void LibVarDeAllocAll(void);

void LibVar_Init(void);
void LibVar_Shutdown(void);
void LibVar_ResetCache(void);

libvar_t *LibVarGet(const char *var_name);
const char *LibVarGetString(const char *var_name);
float LibVarGetValue(const char *var_name);

libvar_t *LibVar(const char *var_name, const char *value);
const char *LibVarString(const char *var_name, const char *value);
float LibVarValue(const char *var_name, const char *value);

void LibVarSet(const char *var_name, const char *value);
int LibVarSetStatus(const char *var_name, const char *value);
int LibVarChanged(const char *var_name);
void LibVarSetNotModified(const char *var_name);

#ifdef __cplusplus
}
#endif

#endif // BOTLIB_COMMON_L_LIBVAR_H
