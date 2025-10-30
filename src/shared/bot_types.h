#ifndef SHARED_BOT_TYPES_H
#define SHARED_BOT_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Status codes returned by botlib functions.
 */
typedef enum bot_status_e {
    BOT_STATUS_SUCCESS = 0,
    BOT_STATUS_ERROR = -1,
    BOT_STATUS_NOT_INITIALIZED = -2,
    BOT_STATUS_NOT_IMPLEMENTED = -3,
    BOT_STATUS_INVALID_ARGUMENT = -4
} bot_status_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SHARED_BOT_TYPES_H */
