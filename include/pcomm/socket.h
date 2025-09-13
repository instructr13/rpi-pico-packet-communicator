#pragma once

#include <string>
#include <unordered_map>

#include "bytes.h"
#include "packets.h"
#include "ringbuf.h"

#ifndef PCOMM_ENABLE_DEBUG_LOG
#define PCOMM_ENABLE_DEBUG_LOG false
#endif

namespace pcomm::socket {

class Socket {
public:
  static constexpr uint16_t TYPE_DEBUG_ECHO = 0xFFFF;
  static constexpr size_t RXTX_BUF_SIZE = 4096;

  using buf_type = ringbuf<uint8_t, RXTX_BUF_SIZE>;

  virtual ~Socket() = default;

  virtual bool is_available() = 0;

  void send(const packets::Packet &packet) {
    send_frame(packet.type, next_frame_id++, packet.payload.data(),
               packet.payload.size());
  }

  void send_debug(const std::string &str) {
    if constexpr (!PCOMM_ENABLE_DEBUG_LOG)
      return;

    std::vector<uint8_t> payload;

    const bytes::Encoder encoder{payload};

    encoder.push_string(str);

    send(packets::Packet(TYPE_DEBUG_ECHO, std::move(payload)));
  }

  void update();

protected:
  void send_chunk(uint16_t type, uint32_t frame_id, uint16_t total_chunks,
                  uint16_t chunk_index, const uint8_t *data, uint16_t len);

  void send_frame(uint16_t type, uint32_t frame_id, const uint8_t *payload,
                  size_t payload_len);

private:
  std::unordered_map<uint32_t, packets::Frame> frame_table{};
  buf_type rx_buffer{};
  std::vector<uint8_t> contiguous_buffer;
  buf_type tx_buffer{};

  std::vector<uint8_t> chunk_temp_buffer;

  std::array<uint8_t, packets::MAX_CHUNK_ESTIMATE_SIZE> decoded_buffer{};
  std::array<uint8_t, packets::MAX_CHUNK_ESTIMATE_SIZE> cobs_buffer{};

  bool prev_availability = false;
  size_t next_frame_id = 0;

  virtual void on_unavailable() {}

  virtual void on_available() {}

  virtual void yield() {}

  virtual void recv_raw_data(buf_type &rx_buffer) = 0;

  virtual void send_raw_data(buf_type &tx_buffer) = 0;

  virtual void on_recv(packets::Packet packet) {}

  std::optional<packets::Packet> recv(const uint8_t *buffer, size_t length);

  void process_rx_buffer();

  packets::Frame *get_or_create_frame(uint16_t type, uint32_t frame_id,
                             uint16_t total_chunks);

  void cleanup_stale_frames();
};

} // namespace pcomm::socket

#include "socket/SerialUSBSocket.h"
