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

#ifndef BOT_MATCH_MAX_MESSAGE_SIZE
#define BOT_MATCH_MAX_MESSAGE_SIZE 256
#endif

#ifndef BOT_MATCH_MAX_VARIABLES
#define BOT_MATCH_MAX_VARIABLES 11
#endif

typedef struct bot_matchvariable_s {
	int offset;
	int length;
} bot_matchvariable_t;

typedef struct bot_match_s {
	char string[BOT_MATCH_MAX_MESSAGE_SIZE];
	int type;
	int subtype;
	bot_matchvariable_t variables[BOT_MATCH_MAX_VARIABLES];
} bot_match_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SHARED_BOT_TYPES_H */
