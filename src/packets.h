#pragma once

#include <Arduino.h>

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "bytes.h"
#include "ringbuf.h"

namespace packets {

constexpr bool ENABLE_DEBUG_LOG = false;

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

class Socket {
public:
  static constexpr uint16_t TYPE_DEBUG_ECHO = 0xFFFF;
  static constexpr size_t RXTX_BUF_SIZE = 4096;

  using buf_type = ringbuf<uint8_t, RXTX_BUF_SIZE>;

  virtual ~Socket() = default;

  virtual bool is_available() = 0;

  void send(const Packet &packet) {
    send_frame(packet.type, next_frame_id++, packet.payload.data(),
               packet.payload.size());
  }

  void send_debug(const std::string &str) {
    if constexpr (!ENABLE_DEBUG_LOG)
      return;

    std::vector<uint8_t> payload;

    const bytes::Encoder encoder{payload};

    encoder.push_string(str);

    send(Packet(TYPE_DEBUG_ECHO, std::move(payload)));
  }

  void update();

protected:
  void send_chunk(uint16_t type, uint32_t frame_id, uint16_t total_chunks,
                  uint16_t chunk_index,
                  const uint8_t *data, uint16_t len);

  void send_frame(uint16_t type, uint32_t frame_id,
                  const uint8_t *payload,
                  size_t payload_len);

private:
  std::unordered_map<uint32_t, Frame> frame_table{};
  buf_type rx_buffer{};
  std::vector<uint8_t> contiguous_buffer;
  buf_type tx_buffer{};

  std::vector<uint8_t> chunk_temp_buffer;

  std::array<uint8_t, MAX_CHUNK_ESTIMATE_SIZE> decoded_buffer{};
  std::array<uint8_t, MAX_CHUNK_ESTIMATE_SIZE> cobs_buffer{};

  bool prev_availability = false;
  size_t next_frame_id = 0;

  virtual void on_unavailable() {}

  virtual void on_available() {}

  virtual void yield() {}

  virtual void recv_raw_data(buf_type &rx_buffer) = 0;

  virtual void send_raw_data(buf_type &tx_buffer) = 0;

  virtual void on_recv(Packet packet) = 0;

  std::optional<Packet> recv(const uint8_t *buffer, size_t length);

  void process_rx_buffer();

  Frame *get_or_create_frame(uint16_t type, uint32_t frame_id,
                             uint16_t total_chunks);

  void cleanup_stale_frames();
};

} // namespace packets
