#ifndef Q_PLATFORM_H
#define Q_PLATFORM_H

#include <string.h>

#if defined(_WIN32)
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif
#else
#include <strings.h>
#endif

static inline int Q_stricmp(const char *lhs, const char *rhs)
{
#if defined(_WIN32)
    return _stricmp(lhs, rhs);
#else
    return strcasecmp(lhs, rhs);
#endif
}

static inline int Q_strnicmp(const char *lhs, const char *rhs, size_t count)
{
#if defined(_WIN32)
    return _strnicmp(lhs, rhs, count);
#else
    return strncasecmp(lhs, rhs, count);
#endif
}

#endif /* Q_PLATFORM_H */
