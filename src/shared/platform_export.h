#ifndef SHARED_PLATFORM_EXPORT_H
#define SHARED_PLATFORM_EXPORT_H

#if defined(_WIN32) || defined(__CYGWIN__)
#    define GLADIATOR_API __declspec(dllexport)
#else
#    if defined(__GNUC__) && __GNUC__ >= 4
#        define GLADIATOR_API __attribute__((visibility("default")))
#    else
#        define GLADIATOR_API
#    endif
#endif

#endif /* SHARED_PLATFORM_EXPORT_H */
