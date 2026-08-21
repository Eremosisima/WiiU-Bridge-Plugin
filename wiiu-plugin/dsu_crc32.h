#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * Calculate CRC32 for DSU protocol
 * Uses polynomial 0xEDB88320 (reversed 0x04C11DB7)
 * 
 * @param data Data to calculate CRC for
 * @param length Length of data in bytes
 * @return CRC32 checksum
 */
uint32_t dsu_crc32(const uint8_t *data, size_t length);

/**
 * Calculate CRC32 for DSU packet (sets CRC field to 0 before calculation)
 * 
 * @param packet Full DSU packet (header + payload)
 * @param packet_length Total packet length
 * @return CRC32 checksum
 */
uint32_t dsu_crc32_packet(const uint8_t *packet, size_t packet_length);

/**
 * Verify DSU packet CRC
 * 
 * @param packet Full DSU packet (header + payload)
 * @param packet_length Total packet length
 * @return 1 if CRC is valid, 0 otherwise
 */
int dsu_crc32_verify(const uint8_t *packet, size_t packet_length);
