#include "l_libvar.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "botlib/common/l_memory.h"
#include "botlib/interface/botlib_interface.h"

#define BOTLIB_MAX_LIBVAR_STRING 1024

static libvar_t *g_libvar_list = NULL;

/*
=============
LibVarStringValue

Converts the restricted unsigned decimal syntax accepted by retail botlib.
=============
*/
float LibVarStringValue(const char *string)
{
	int32_t divisor = 0;
	long double value = 0.0;

	if (string == NULL)
	{
		return 0.0f;
	}

	while (*string != '\0')
	{
		if (*string < '0' || *string > '9')
		{
			if (divisor != 0 || *string != '.')
			{
				return 0.0f;
			}

			divisor = 10;
			string++;
			if (*string == '\0')
			{
				/*
				 * 0x1003877c falls straight into 0x1003879a with no
				 * terminator check: retail adds (0 - '0') / divisor here and
				 * then keeps scanning past the NUL with dotfound set, so its
				 * result for a trailing '.' depends on whatever follows the
				 * GetMemory(strlen+1) block.  The reject at 0x10038798 fires
				 * on the first nonzero non-digit, which makes 0.0f the modal
				 * outcome; we stop in bounds and return that rather than
				 * reproducing the out-of-bounds read.  The accumulated value
				 * is the one result retail can never produce here, so it must
				 * not be returned.
				 */
				return 0.0f;
			}
		}

		if (divisor != 0)
		{
			value += (long double)(*string - '0') / (long double)divisor;
			divisor = (int32_t)((uint32_t)divisor * 10u);
		}
		else
		{
			value = value * 10.0 + (long double)(*string - '0');
		}

		string++;
	}

	return (float)value;
}

/*
=============
LibVar_ToLowerAscii

Folds only the ASCII upper-case range used by retail's comparison helper.
=============
*/
static unsigned char LibVar_ToLowerAscii(unsigned char character)
{
	if (character >= 'A' && character <= 'Z')
	{
		return (unsigned char)(character + ('a' - 'A'));
	}

	return character;
}

/*
=============
LibVar_NameEquals

Performs the ASCII case-insensitive name comparison used by the retail list scan.
=============
*/
static bool LibVar_NameEquals(const char *lhs, const char *rhs)
{
	if (lhs == NULL || rhs == NULL)
	{
		return false;
	}

	while (*lhs != '\0' && *rhs != '\0')
	{
		unsigned char left = (unsigned char)*lhs++;
		unsigned char right = (unsigned char)*rhs++;
		if (LibVar_ToLowerAscii(left) != LibVar_ToLowerAscii(right))
		{
			return false;
		}
	}

	return *lhs == '\0' && *rhs == '\0';
}

/*
=============
LibVar_Find

Returns an existing variable without creating or refreshing it.
=============
*/
static libvar_t *LibVar_Find(const char *name)
{
	for (libvar_t *var = g_libvar_list; var != NULL; var = var->next)
	{
		if (LibVar_NameEquals(var->name, name))
		{
			return var;
		}
	}

	return NULL;
}

/*
=============
LibVarAlloc

Allocates the variable and its trailing name in the single retail block.
=============
*/
libvar_t *LibVarAlloc(const char *name)
{
	if (name == NULL)
	{
		return NULL;
	}

	size_t name_length = strlen(name);
	if (name_length > SIZE_MAX - sizeof(libvar_t) - 1)
	{
		return NULL;
	}

	libvar_t *var = (libvar_t *)GetMemory(sizeof(libvar_t) + name_length + 1);
	if (var == NULL)
	{
		return NULL;
	}

	memset(var, 0, sizeof(*var));
	var->name = (char *)(var + 1);
	memcpy(var->name, name, name_length + 1);
	var->next = g_libvar_list;
	g_libvar_list = var;
	return var;
}

/*
=============
LibVar_CopyString

Allocates the separate value-string block used by retail libvars.
=============
*/
static char *LibVar_CopyString(const char *string)
{
	if (string == NULL)
	{
		return NULL;
	}

	size_t length = strlen(string);
	if (length == SIZE_MAX)
	{
		return NULL;
	}

	char *copy = (char *)GetMemory(length + 1);
	if (copy == NULL)
	{
		return NULL;
	}

	memcpy(copy, string, length + 1);
	return copy;
}

/*
=============
LibVarDeAlloc

Releases the separate value string before the combined variable/name block.
=============
*/
void LibVarDeAlloc(libvar_t *var)
{
	if (var == NULL)
	{
		return;
	}

	if (var->string != NULL)
	{
		FreeMemory(var->string);
	}
	FreeMemory(var);
}

/*
=============
LibVarDeAllocAll

Releases every variable in list order and clears the retail list head.
=============
*/
void LibVarDeAllocAll(void)
{
	while (g_libvar_list != NULL)
	{
		libvar_t *var = g_libvar_list;
		g_libvar_list = var->next;
		LibVarDeAlloc(var);
	}

	g_libvar_list = NULL;
}

/*
=============
LibVar_Create

Creates a linked variable, its value string, parsed value, and modified flag.
=============
*/
static libvar_t *LibVar_Create(const char *name, const char *string, int modified)
{
	if (name == NULL || string == NULL)
	{
		return NULL;
	}

	libvar_t *var = LibVarAlloc(name);
	if (var == NULL)
	{
		return NULL;
	}

	var->string = LibVar_CopyString(string);
	if (var->string == NULL)
	{
		g_libvar_list = var->next;
		LibVarDeAlloc(var);
		return NULL;
	}

	var->value = LibVarStringValue(var->string);
	var->modified = modified;
	return var;
}

/*
=============
LibVar_ReplaceString

Replaces a value string and marks the variable even when text is unchanged.
=============
*/
static bool LibVar_ReplaceString(libvar_t *var, const char *string)
{
	if (var == NULL || string == NULL)
	{
		return false;
	}

	if (var->string != NULL)
	{
		FreeMemory(var->string);
		var->string = NULL;
	}

	var->string = LibVar_CopyString(string);
	if (var->string == NULL)
	{
		var->value = 0.0f;
		var->modified = 1;
		return false;
	}

	var->value = LibVarStringValue(var->string);
	var->modified = 1;
	return true;
}

/*
=============
LibVar_SetLocal

Applies the retail set operation without invoking bridge import extensions.
=============
*/
static int LibVar_SetLocal(const char *var_name, const char *value)
{
	libvar_t *var = LibVar_Find(var_name);
	if (var == NULL)
	{
		return LibVar_Create(var_name, value, 1) != NULL ? BLERR_NOERROR : BLERR_INVALIDIMPORT;
	}

	return LibVar_ReplaceString(var, value) ? BLERR_NOERROR : BLERR_INVALIDIMPORT;
}

/*
=============
LibVar_Init

Initialises the compatibility wrapper with an empty retail list.
=============
*/
void LibVar_Init(void)
{
	LibVarDeAllocAll();
}

/*
=============
LibVar_Shutdown

Releases the list through the retail deallocation routine.
=============
*/
void LibVar_Shutdown(void)
{
	LibVarDeAllocAll();
}

/*
=============
LibVar_ResetCache

Clears bridge bootstrap state while preserving the compatibility API.
=============
*/
void LibVar_ResetCache(void)
{
	LibVarDeAllocAll();
}

/*
=============
LibVarGet

Finds a variable case-insensitively without refreshing an existing value.

Retail 0x10038910 is a side-effect-free walk of libvarlist that returns 0 when
the name is absent (0x1003893b).  Creating the record on a miss made every
getter allocate: with showmemoryusage or memorydump pushed in by the host, the
AAS frame diagnostic allocated two tracked blocks through this path immediately
before printing the very counters it reports.  Host-supplied names now reach the
list through the setup-time seed instead.
=============
*/
libvar_t *LibVarGet(const char *var_name)
{
	if (var_name == NULL)
	{
		return NULL;
	}

	return LibVar_Find(var_name);
}

/*
=============
LibVarGetString

Returns the variable text or the retail shared empty string for a miss.
=============
*/
const char *LibVarGetString(const char *var_name)
{
	libvar_t *var = LibVarGet(var_name);
	return var != NULL && var->string != NULL ? var->string : "";
}

/*
=============
LibVarGetValue

Returns the cached numeric value or zero for a missing variable.
=============
*/
float LibVarGetValue(const char *var_name)
{
	libvar_t *var = LibVarGet(var_name);
	return var != NULL ? var->value : 0.0f;
}

/*
=============
LibVar

Returns an existing variable or creates it from the supplied default.
=============
*/
libvar_t *LibVar(const char *var_name, const char *value)
{
	libvar_t *var = LibVarGet(var_name);
	if (var != NULL)
	{
		return var;
	}

	return LibVar_Create(var_name, value != NULL ? value : "", 1);
}

/*
=============
LibVarString

Returns the stored string from an existing or newly defaulted variable.
=============
*/
const char *LibVarString(const char *var_name, const char *value)
{
	libvar_t *var = LibVar(var_name, value);
	return var != NULL && var->string != NULL ? var->string : "";
}

/*
=============
LibVarValue

Returns the numeric value from an existing or newly defaulted variable.
=============
*/
float LibVarValue(const char *var_name, const char *value)
{
	libvar_t *var = LibVar(var_name, value);
	return var != NULL ? var->value : 0.0f;
}

/*
=============
LibVarSetStatus

Updates the bridge bootstrap store and applies retail local set semantics.
=============
*/
int LibVarSetStatus(const char *var_name, const char *value)
{
	if (var_name == NULL || value == NULL)
	{
		return BLERR_INVALIDIMPORT;
	}

	const botlib_import_table_t *imports = BotInterface_GetImportTable();
	if (imports != NULL && imports->BotLibVarSet != NULL)
	{
		int status = imports->BotLibVarSet(var_name, value);
		if (status != BLERR_NOERROR)
		{
			return status;
		}
	}

	return LibVar_SetLocal(var_name, value);
}

/*
=============
LibVarSet

Applies a value update while retaining the original void helper contract.
=============
*/
void LibVarSet(const char *var_name, const char *value)
{
	if (var_name == NULL || value == NULL)
	{
		return;
	}

	(void)LibVar_SetLocal(var_name, value);
}

/*
=============
LibVarChanged

Returns the stored modified flag without creating a missing variable.
=============
*/
int LibVarChanged(const char *var_name)
{
	libvar_t *var = LibVar_Find(var_name);
	return var != NULL ? var->modified : 0;
}

/*
=============
LibVarSetNotModified

Clears the modified flag when the named variable exists.
=============
*/
void LibVarSetNotModified(const char *var_name)
{
	libvar_t *var = LibVar_Find(var_name);
	if (var != NULL)
	{
		var->modified = 0;
	}
}
