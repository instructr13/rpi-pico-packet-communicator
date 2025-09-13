#pragma once

#include <cstdint>
#include <cstring>

// CRC16-CCITT Implementation
namespace crc16_ccitt {

constexpr uint16_t POLYNOMIAL = 0x1021;

uint16_t basic_crc16(uint16_t crc, const uint8_t *data, size_t length);

void initialize_table();

uint16_t compute(const uint8_t *data, size_t length, uint16_t initial_crc = 0);

inline bool verify(const uint8_t *data, const size_t length,
                   const uint16_t expected_crc,
                   const uint16_t initial_crc = 0) {
  return compute(data, length, initial_crc) == expected_crc;
}

} // namespace crc16_ccitt
