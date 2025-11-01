#include "l_struct.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define STRUCT_MAX(a, b) ((a) > (b) ? (a) : (b))
#define STRUCT_MIN(a, b) ((a) < (b) ? (a) : (b))

static bool g_l_struct_initialised = false;

/* Forward declarations for precompiler helpers provided by l_precomp.c. */
int PC_ExpectAnyToken(pc_source_t *source, pc_token_t *token);
int PC_ExpectTokenString(pc_source_t *source, char *string);
int PC_ExpectTokenType(pc_source_t *source, int type, int subtype, pc_token_t *token);
int PC_CheckTokenString(pc_source_t *source, char *string);
void PC_UnreadLastToken(pc_source_t *source);
void StripSingleQuotes(char *string);
void StripDoubleQuotes(char *string);
void SourceError(pc_source_t *source, char *str, ...);
void SourceWarning(pc_source_t *source, char *str, ...);

bool L_Struct_Init(void) {
    if (g_l_struct_initialised) {
        return true;
    }

    g_l_struct_initialised = true;
    return true;
}

void L_Struct_Shutdown(void) {
    g_l_struct_initialised = false;
}

bool L_Struct_IsInitialised(void) {
    return g_l_struct_initialised;
}

const fielddef_t *FindField(const fielddef_t *defs, const char *name) {
    if (defs == NULL || name == NULL) {
        return NULL;
    }

    for (int i = 0; defs[i].name != NULL; ++i) {
        if (strcmp(defs[i].name, name) == 0) {
            return &defs[i];
        }
    }

    return NULL;
}

bool ReadNumber(pc_source_t *source, const fielddef_t *fd, void *p) {
    if (source == NULL || fd == NULL || p == NULL) {
        return false;
    }

    pc_token_t token;
    bool negative = false;

    if (!PC_ExpectAnyToken(source, &token)) {
        return false;
    }

    if (token.type == TT_PUNCTUATION) {
        if ((fd->type & FT_UNSIGNED) != 0) {
            SourceError(source, "expected unsigned value, found %s", token.string);
            return false;
        }

        if (strcmp(token.string, "-") != 0) {
            SourceError(source, "unexpected punctuation %s", token.string);
            return false;
        }

        negative = true;

        if (!PC_ExpectAnyToken(source, &token)) {
            return false;
        }
    }

    if (token.type != TT_NUMBER) {
        SourceError(source, "expected number, found %s", token.string);
        return false;
    }

    if ((token.subtype & TT_FLOAT) != 0) {
        if ((fd->type & FT_TYPE) != FT_FLOAT) {
            SourceError(source, "unexpected float");
            return false;
        }

        double floatvalue = (double)token.floatvalue;
        if (negative) {
            floatvalue = -floatvalue;
        }

        if ((fd->type & FT_BOUNDED) != 0) {
            if (floatvalue < fd->floatmin || floatvalue > fd->floatmax) {
                SourceError(source,
                             "float out of range [%f, %f]",
                             fd->floatmin,
                             fd->floatmax);
                return false;
            }
        }

        *(float *)p = (float)floatvalue;
        return true;
    }

    long int intval = (long int)token.intvalue;
    if (negative) {
        intval = -intval;
    }

    int intmin = 0;
    int intmax = 0;

    if ((fd->type & FT_TYPE) == FT_CHAR) {
        if ((fd->type & FT_UNSIGNED) != 0) {
            intmin = 0;
            intmax = 255;
        } else {
            intmin = -128;
            intmax = 127;
        }
    } else if ((fd->type & FT_TYPE) == FT_INT) {
        if ((fd->type & FT_UNSIGNED) != 0) {
            intmin = 0;
            intmax = 65535;
        } else {
            intmin = -32768;
            intmax = 32767;
        }
    }

    if ((fd->type & FT_TYPE) == FT_CHAR || (fd->type & FT_TYPE) == FT_INT) {
        if ((fd->type & FT_BOUNDED) != 0) {
            intmin = (int)STRUCT_MAX((double)intmin, fd->floatmin);
            intmax = (int)STRUCT_MIN((double)intmax, fd->floatmax);
        }

        if (intval < intmin || intval > intmax) {
            SourceError(source,
                         "value %ld out of range [%d, %d]",
                         intval,
                         intmin,
                         intmax);
            return false;
        }
    } else if ((fd->type & FT_TYPE) == FT_FLOAT) {
        if ((fd->type & FT_BOUNDED) != 0) {
            if (intval < fd->floatmin || intval > fd->floatmax) {
                SourceError(source,
                             "value %ld out of range [%f, %f]",
                             intval,
                             fd->floatmin,
                             fd->floatmax);
                return false;
            }
        }
    }

    if ((fd->type & FT_TYPE) == FT_CHAR) {
        if ((fd->type & FT_UNSIGNED) != 0) {
            *(unsigned char *)p = (unsigned char)intval;
        } else {
            *(char *)p = (char)intval;
        }
    } else if ((fd->type & FT_TYPE) == FT_INT) {
        if ((fd->type & FT_UNSIGNED) != 0) {
            *(unsigned int *)p = (unsigned int)intval;
        } else {
            *(int *)p = (int)intval;
        }
    } else if ((fd->type & FT_TYPE) == FT_FLOAT) {
        *(float *)p = (float)intval;
    }

    return true;
}

bool ReadChar(pc_source_t *source, const fielddef_t *fd, void *p) {
    if (source == NULL || fd == NULL || p == NULL) {
        return false;
    }

    pc_token_t token;

    if (!PC_ExpectAnyToken(source, &token)) {
        return false;
    }

    if (token.type == TT_LITERAL) {
        StripSingleQuotes(token.string);
        *(char *)p = token.string[0];
        return true;
    }

    PC_UnreadLastToken(source);
    return ReadNumber(source, fd, p);
}

bool ReadString(pc_source_t *source, const fielddef_t *fd, void *p) {
    (void)fd;

    if (source == NULL || p == NULL) {
        return false;
    }

    pc_token_t token;
    if (!PC_ExpectTokenType(source, TT_STRING, 0, &token)) {
        return false;
    }

    StripDoubleQuotes(token.string);
    strncpy((char *)p, token.string, MAX_STRINGFIELD);
    ((char *)p)[MAX_STRINGFIELD - 1] = '\0';

    return true;
}

static bool ReadStructField(pc_source_t *source,
                            const fielddef_t *fd,
                            void *base,
                            int count) {
    unsigned char *cursor = (unsigned char *)base;
    bool closed = false;

    for (int index = 0; index < count; ++index) {
        if ((fd->type & FT_ARRAY) != 0) {
            if (PC_CheckTokenString(source, "}")) {
                closed = true;
                break;
            }
        }

        switch (fd->type & FT_TYPE) {
        case FT_CHAR:
            if (!ReadChar(source, fd, cursor)) {
                return false;
            }
            cursor += sizeof(char);
            break;
        case FT_INT:
            if (!ReadNumber(source, fd, cursor)) {
                return false;
            }
            cursor += sizeof(int);
            break;
        case FT_FLOAT:
            if (!ReadNumber(source, fd, cursor)) {
                return false;
            }
            cursor += sizeof(float);
            break;
        case FT_STRING:
            if (!ReadString(source, fd, cursor)) {
                return false;
            }
            cursor += MAX_STRINGFIELD;
            break;
        case FT_STRUCT:
            if (fd->substruct == NULL) {
                SourceError(source, "BUG: no sub structure defined");
                return false;
            }
            if (!ReadStructure(source, fd->substruct, cursor)) {
                return false;
            }
            cursor += fd->substruct->size;
            break;
        default:
            SourceError(source, "unsupported field type %d", fd->type & FT_TYPE);
            return false;
        }

        if ((fd->type & FT_ARRAY) != 0) {
            pc_token_t token;
            if (!PC_ExpectAnyToken(source, &token)) {
                return false;
            }

            if (strcmp(token.string, "}") == 0) {
                closed = true;
                break;
            }

            if (strcmp(token.string, ",") != 0) {
                SourceError(source, "expected a comma, found %s", token.string);
                return false;
            }
        }
    }

    if ((fd->type & FT_ARRAY) != 0 && !closed) {
        if (!PC_ExpectTokenString(source, "}")) {
            return false;
        }
    }

    return true;
}

bool ReadStructure(pc_source_t *source, const structdef_t *def, void *structure) {
    if (source == NULL || def == NULL || structure == NULL) {
        return false;
    }

    if (!PC_ExpectTokenString(source, "{")) {
        return false;
    }

    while (true) {
        pc_token_t token;
        if (!PC_ExpectAnyToken(source, &token)) {
            return false;
        }

        if (strcmp(token.string, "}") == 0) {
            break;
        }

        const fielddef_t *fd = FindField(def->fields, token.string);
        if (fd == NULL) {
            SourceError(source, "unknown structure field %s", token.string);
            return false;
        }

        int count = 1;
        if ((fd->type & FT_ARRAY) != 0) {
            count = fd->maxarray;
            if (!PC_ExpectTokenString(source, "{")) {
                return false;
            }
        }

        unsigned char *base = (unsigned char *)structure + fd->offset;
        if (!ReadStructField(source, fd, base, count)) {
            return false;
        }
    }

    return true;
}

int WriteIndent(FILE *fp, int indent) {
    if (fp == NULL) {
        return 0;
    }

    for (int i = 0; i < indent; ++i) {
        if (fprintf(fp, "\t") < 0) {
            return 0;
        }
    }

    return 1;
}

bool WriteFloat(FILE *fp, float value) {
    if (fp == NULL) {
        return false;
    }

    char buffer[128];
    if (snprintf(buffer, sizeof(buffer), "%f", value) < 0) {
        return false;
    }

    size_t length = strlen(buffer);
    while (length > 1) {
        size_t index = length - 1;
        if (buffer[index] == '0') {
            buffer[index] = '\0';
            --length;
            continue;
        }
        if (buffer[index] == '.') {
            buffer[index] = '\0';
        }
        break;
    }

    return fprintf(fp, "%s", buffer) >= 0;
}

static bool WriteStructWithIndent(FILE *fp,
                                  const structdef_t *def,
                                  const void *structure,
                                  int indent) {
    if (fp == NULL || def == NULL || structure == NULL) {
        return false;
    }

    if (!WriteIndent(fp, indent) || fprintf(fp, "{\r\n") < 0) {
        return false;
    }

    indent++;

    for (int i = 0; def->fields[i].name != NULL; ++i) {
        const fielddef_t *fd = &def->fields[i];
        if (!WriteIndent(fp, indent) || fprintf(fp, "%s\t", fd->name) < 0) {
            return false;
        }

        const unsigned char *cursor = (const unsigned char *)structure + fd->offset;
        int count = 1;
        if ((fd->type & FT_ARRAY) != 0) {
            count = fd->maxarray;
            if (fprintf(fp, "{") < 0) {
                return false;
            }
        }

        for (int index = 0; index < count; ++index) {
            switch (fd->type & FT_TYPE) {
            case FT_CHAR:
                if (fprintf(fp, "%d", *(const char *)cursor) < 0) {
                    return false;
                }
                cursor += sizeof(char);
                break;
            case FT_INT:
                if (fprintf(fp, "%d", *(const int *)cursor) < 0) {
                    return false;
                }
                cursor += sizeof(int);
                break;
            case FT_FLOAT:
                if (!WriteFloat(fp, *(const float *)cursor)) {
                    return false;
                }
                cursor += sizeof(float);
                break;
            case FT_STRING:
                if (fprintf(fp, "\"%s\"", (const char *)cursor) < 0) {
                    return false;
                }
                cursor += MAX_STRINGFIELD;
                break;
            case FT_STRUCT:
                if (!WriteStructWithIndent(fp,
                                            fd->substruct,
                                            cursor,
                                            indent)) {
                    return false;
                }
                cursor += fd->substruct->size;
                break;
            default:
                return false;
            }

            if ((fd->type & FT_ARRAY) != 0) {
                if (index + 1 < count) {
                    if (fprintf(fp, ",") < 0) {
                        return false;
                    }
                } else {
                    if (fprintf(fp, "}") < 0) {
                        return false;
                    }
                }
            }
        }

        if (fprintf(fp, "\r\n") < 0) {
            return false;
        }
    }

    indent--;

    return WriteIndent(fp, indent) && fprintf(fp, "}\r\n") >= 0;
}

bool WriteStructure(FILE *fp, const structdef_t *def, const void *structure) {
    return WriteStructWithIndent(fp, def, structure, 0);
}
