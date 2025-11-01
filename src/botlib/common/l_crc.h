#ifndef BOTLIB_COMMON_L_CRC_H
#define BOTLIB_COMMON_L_CRC_H

#include <stddef.h>
#include <stdint.h>

/*
 * Quake III used a 16-bit CCITT CRC (polynomial 0x1021) seeded with 0xffff and
 * without reflection.  Gladiator reuses the same routine to guard AAS route
 * cache blobs and bot asset packages.
 */

#define CRC_INIT_VALUE 0xffffu
#define CRC_XOR_VALUE  0x0000u

#ifdef __cplusplus
extern "C" {
#endif

void CRC_Init(uint16_t *crcvalue);
void CRC_ProcessByte(uint16_t *crcvalue, uint8_t data);
uint16_t CRC_Value(uint16_t crcvalue);
uint16_t CRC_ProcessString(const uint8_t *data, size_t length);
void CRC_ContinueProcessString(uint16_t *crc, const char *data, size_t length);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // BOTLIB_COMMON_L_CRC_H
