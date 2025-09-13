#pragma once

#include <Arduino.h>

#include <optional>
#include <vector>

namespace pcomm::packets {

// COBS max chunk size
constexpr size_t MAX_CHUNK_ESTIMATE_SIZE = 64;
constexpr size_t MAX_CHUNK_SIZE =
    38; // 64 - 5 (cobs) - 14 (header+crc) - 1 (delimiter) - 6 (additional)
constexpr size_t MAX_FRAME_SIZE = 2048;

constexpr uint32_t RX_FRAME_TIMEOUT_MS = 2000;

struct ChunkHeader {
  uint16_t type;
  uint32_t frame_id;
  uint16_t total_chunks;
  uint16_t chunk_index;
  uint16_t payload_size;
  uint16_t crc16;
};

struct Frame {
  uint16_t type;
  uint32_t frame_id;
  uint16_t total_chunks;
  uint16_t received_chunks = 0;
  uint32_t last_update_ms;
  std::vector<std::vector<uint8_t>> chunks;
  bool invalid = false;

  Frame(const uint16_t type, const uint32_t frame_id,
        const uint16_t total_chunks)
      : type(type), frame_id(frame_id), total_chunks(total_chunks),
        last_update_ms(millis()), chunks(total_chunks) {}
};

struct Packet {
  const uint16_t type;
  const std::vector<uint8_t> payload;

  Packet(const uint16_t type, std::vector<uint8_t> &&payload)
      : type(type), payload(std::move(payload)) {}
};

} // namespace packets
