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
/*
 * Retail allocates each source-checksum record as a single 0x98-byte block
 * (sub_100376b0 at 0x100376ec): the 16-bit checksum sits at +0, the filename
 * copy starts at +2, and the list link occupies +0x94.  The filename field is
 * therefore 0x94 - 2 = 146 bytes wide.
 */
#define CRC_SOURCE_NAME_MAX 146u

#ifdef __cplusplus
extern "C" {
#endif

void CRC_Init(uint16_t *crcvalue);
void CRC_ProcessByte(uint16_t *crcvalue, uint8_t data);
uint16_t CRC_Value(uint16_t crcvalue);
uint16_t CRC_ProcessString(uint8_t *data, int length);
void CRC_ContinueProcessString(uint16_t *crc, char *data, int length);
void CRC_ResetSourceChecksums(void);
void CRC_RegisterSourceChecksum(const char *name, uint16_t checksum);
void CRC_RegisterSourceData(const char *name, const void *data, int length);
size_t CRC_SourceChecksumCount(void);
int CRC_SourceChecksumAt(size_t index,
	uint16_t *checksum,
	char *name,
	size_t name_size);
void CRC_DumpSourceChecksums(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // BOTLIB_COMMON_L_CRC_H
