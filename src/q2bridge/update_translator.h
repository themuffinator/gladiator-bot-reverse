#ifndef Q2BRIDGE_UPDATE_TRANSLATOR_H
#define Q2BRIDGE_UPDATE_TRANSLATOR_H

#include "q2bridge/botlib.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Cache the latest BotUpdateClient payload for future AAS translation.
 *
 * Mirrors the validation performed by the legacy Gladiator DLL: refusing to
 * operate when the bot library has not been initialised and rejecting invalid
 * client numbers. On success the update is stored so the eventual C
 * reimplementation of TranslateClientUpdate can consume it.
 *
 * @return BLERR_NOERROR on success, or a botlib error code describing the
 *         failure reason (e.g. BLERR_LIBRARYNOTSETUP, BLERR_INVALIDCLIENTNUMBER,
 *         BLERR_AIUPDATEINACTIVECLIENT).
 */
int Bridge_UpdateClient(int client, const bot_updateclient_t *update);

/**
 * @brief Cache the latest BotUpdateEntity payload for future AAS translation.
 *
 * Performs the same guard checks as the original DLL and records the snapshot
 * so the forthcoming AAS glue can replay it without the Quake II side having
 * to resend the frame.
 *
 * @return BLERR_NOERROR on success, or a botlib error code such as
 *         BLERR_LIBRARYNOTSETUP or BLERR_INVALIDENTITYNUMBER when validation
 *         fails.
 */
int Bridge_UpdateEntity(int ent, const bot_updateentity_t *update);

/**
 * @brief Reset the cached bridge state.
 */
void Bridge_ResetCachedUpdates(void);

#ifdef __cplusplus
}
#endif

#endif // Q2BRIDGE_UPDATE_TRANSLATOR_H
