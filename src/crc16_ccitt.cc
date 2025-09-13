#include "crc16_ccitt.h"

#include <array>

namespace {

std::array<std::array<uint16_t, 256>, 8> table;
bool table_initialized = false;

} // namespace

uint16_t crc16_ccitt::basic_crc16(uint16_t crc, const uint8_t *data,
                                  const size_t length) {
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;

    for (uint8_t j = 0; j < 8; ++j) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ POLYNOMIAL;
      } else {
        crc <<= 1;
      }
    }
  }

  return crc;
}

void crc16_ccitt::initialize_table() {
  for (size_t n = 0; n < 256; ++n) {
    const auto byte = static_cast<uint8_t>(n);

    table[0][n] = basic_crc16(0, &byte, 1);
  }

  for (size_t n = 0; n < 256; ++n) {
    auto crc = table[0][n];

    for (int k = 1; k < 8; ++k) {
      crc = table[0][(crc >> 8) & 0xFF] ^ (crc << 8);

      table[k][n] = crc;
    }
  }

  table_initialized = true;
}

uint16_t crc16_ccitt::compute(const uint8_t *data, size_t length,
                              const uint16_t initial_crc) {
  if (!table_initialized) {
    crc16_ccitt::initialize_table();
  }

  auto crc = initial_crc;

  // Process bytes until data is 8-byte aligned
  while (length && (reinterpret_cast<uintptr_t>(data) & 7) != 0) {
    crc = table[0][((crc >> 8) ^ *data++) & 0xff] ^ (crc << 8);

    length--;
  }

  // Process 8 bytes at a time
  while (length >= 8) {
    const uint64_t n = *reinterpret_cast<const uint64_t *>(data);

    // clang-format off

    crc = table[7][(n & 0xff) ^ ((crc >> 8) & 0xff)] ^
          table[6][((n >> 8) & 0xff) ^ (crc & 0xff)] ^
          table[5][(n >> 16) & 0xff] ^
          table[4][(n >> 24) & 0xff] ^
          table[3][(n >> 32) & 0xff] ^
          table[2][(n >> 40) & 0xff] ^
          table[1][(n >> 48) & 0xff] ^
          table[0][n >> 56];

    // clang-format on

    data += 8;
    length -= 8;
  }

  // Process remaining bytes
  while (length) {
    crc = table[0][((crc >> 8) ^ *data++) & 0xff] ^ (crc << 8);
    length--;
  }

  return crc;
}
