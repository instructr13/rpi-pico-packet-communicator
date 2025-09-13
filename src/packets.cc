#include "packets.h"

#include <algorithm>

#include "cobs.h"
#include "crc16_ccitt.h"

using namespace packets;

void Socket::update() {
  if (!is_available()) {
    if (prev_availability) {
      rx_buffer.clear();
      tx_buffer.clear();

      prev_availability = false;

      on_unavailable();
    }

    return;
  }

  if (!prev_availability) {
    prev_availability = true;

    on_available();
  }

  yield();

  recv_raw_data(rx_buffer);

  if (!rx_buffer.empty()) {
    process_rx_buffer();
  }

  if (!tx_buffer.empty()) {
    send_raw_data(tx_buffer);
  }

  cleanup_stale_frames();
}

void Socket::send_chunk(const uint16_t type, const uint32_t frame_id,
                        const uint16_t total_chunks, const uint16_t chunk_index,
                        const uint8_t *data, const uint16_t len) {
  assert(len <= MAX_CHUNK_SIZE);

  chunk_temp_buffer.clear();
  chunk_temp_buffer.reserve(MAX_CHUNK_ESTIMATE_SIZE);

  const bytes::Encoder encoder(chunk_temp_buffer);

  encoder.push_number(type);
  encoder.push_number(frame_id);
  encoder.push_number(total_chunks);
  encoder.push_number(chunk_index);
  encoder.push_number(len);

  encoder.push_bytes(data, len);

  const auto crc = crc16_ccitt::compute(data, len);

  encoder.push_number(crc);

  const auto cobs_len = cobs::encode(
      chunk_temp_buffer.data(), chunk_temp_buffer.size(), cobs_buffer.data());

  tx_buffer.push(cobs_buffer.data(), cobs_len);
}

void Socket::send_frame(const uint16_t type, const uint32_t frame_id,
                        const uint8_t *payload, const size_t payload_len) {
  assert(payload_len <= MAX_FRAME_SIZE);

  if (tx_buffer.full()) {
    // No space
    return;
  }

  uint16_t total_chunks = (payload_len + MAX_CHUNK_SIZE - 1) / MAX_CHUNK_SIZE;

  if (total_chunks == 0)
    total_chunks = 1;

  for (uint16_t i = 0; i < total_chunks; ++i) {
    const size_t offset = static_cast<size_t>(i) * MAX_CHUNK_SIZE;
    const uint16_t min = std::min(payload_len - offset, MAX_CHUNK_SIZE);

    send_chunk(type, frame_id, total_chunks, i, payload + offset, min);
  }
}

std::optional<Packet> Socket::recv(const uint8_t *buffer, const size_t length) {
  if (length < 12) {
    send_debug("Packet too short");

    // Not enough data for even the smallest chunk
    // type(2) + frame_id(4) + total_chunks(2) + chunk_index(2) +
    // payload_size(2)
    return std::nullopt;
  }

  bytes::Decoder decoder(buffer, length);

  const auto type = decoder.pop_number<uint16_t>();
  const auto frame_id = decoder.pop_number<uint32_t>();
  const auto total_chunks = decoder.pop_number<uint16_t>();
  const auto chunk_index = decoder.pop_number<uint16_t>();
  const auto payload_size = decoder.pop_number<uint16_t>();

  if (!decoder.good()) {
    send_debug("Decoder underflow reading header");

    return std::nullopt;
  }

  if (payload_size != decoder.remaining() - 2) {
    send_debug("Invalid payload size");

    return std::nullopt;
  }

  if (payload_size > MAX_CHUNK_SIZE) {
    send_debug("Payload size too large");

    return std::nullopt;
  }

  auto data = std::vector<uint8_t>(payload_size);
  decoder.pop_bytes(data.data(), payload_size);

  if (!decoder.good()) {
    send_debug("Decoder underflow reading payload");

    return std::nullopt;
  }

  const auto crc16 = decoder.pop_number<uint16_t>();

  if (!decoder.good()) {
    send_debug("Decoder underflow reading CRC");

    return std::nullopt;
  }

  if (!crc16_ccitt::verify(data.data(), payload_size, crc16)) {
    // CRC mismatch
    // Find existing entry and mark as invalid
    const auto frame = frame_table.find(frame_id);

    if (frame != frame_table.end()) {
      frame->second.invalid = true;
    }

    return std::nullopt;
  }

  auto *frame = get_or_create_frame(type, frame_id, total_chunks);

  if (frame == nullptr || frame->invalid) {
    // Invalid frame
    return std::nullopt;
  }

  if (frame->chunks.size() != total_chunks) {
    // Adjust chunks
    frame->chunks.resize(total_chunks);
  }

  if (chunk_index >= total_chunks || !frame->chunks[chunk_index].empty()) {
    send_debug("Invalid chunk index or duplicate chunk");

    // Invalid chunk index or duplicate chunk
    return std::nullopt;
  }

  frame->chunks[chunk_index] = std::move(data);
  frame->received_chunks++;
  frame->last_update_ms = millis();

  // If all chunks received, reassemble
  if (frame->received_chunks == frame->total_chunks) {
    std::vector<uint8_t> full_payload;

    full_payload.reserve(frame->total_chunks * MAX_CHUNK_SIZE);

    for (auto &chunk : frame->chunks) {
      full_payload.insert(full_payload.end(),
                          std::make_move_iterator(chunk.begin()),
                          std::make_move_iterator(chunk.end()));

      chunk.clear();
    }

    if (full_payload.size() > MAX_FRAME_SIZE) {
      send_debug("Frame size overflow");

      // Overflow, mark frame as invalid
      frame->invalid = true;

      return std::nullopt;
    }

    auto packet = Packet(frame->type, std::move(full_payload));

    // Remove frame from table
    frame_table.erase(frame_id);

    return packet;
  }

  // Not complete yet
  return std::nullopt;
}

void Socket::process_rx_buffer() {
  while (true) {
    if (rx_buffer.empty()) {
      break;
    }

    const size_t idx = rx_buffer.find(0);

    if (idx == rx_buffer.size()) {
      // No complete chunk yet
      break;
    }

    if (idx > 0) {
      contiguous_buffer.resize(idx);

      rx_buffer.pop(contiguous_buffer.data(), idx);

      size_t decoded_length = 0;

      if (cobs::decode(contiguous_buffer.data(), idx, decoded_buffer.data(),
                       decoded_length)) {
        if (auto packet = recv(decoded_buffer.data(), decoded_length)) {
          on_recv(std::move(packet.value()));
        }
      } else {
        send_debug("COBS decode error");
      }
    }

    // Remove the 0x00 delimiter
    const auto front = rx_buffer.front();

    if (!rx_buffer.empty() && front && front.value() == 0) {
      rx_buffer.pop();
    }
  }
}

Frame *Socket::get_or_create_frame(const uint16_t type, uint32_t frame_id,
                                   const uint16_t total_chunks) {
  const auto frame = frame_table.find(frame_id);

  if (frame != frame_table.end()) {
    if (frame->second.type != type) {
      // Mark as invalid if type doesn't match
      frame->second.invalid = true;

      return nullptr;
    }

    return &frame->second;
  }

  frame_table.emplace(frame_id, Frame(type, frame_id, total_chunks));

  return &frame_table.at(frame_id);
}

void Socket::cleanup_stale_frames() {
  const auto now = millis();

  for (auto it = frame_table.begin(); it != frame_table.end();) {
    if (it->second.invalid ||
        now - it->second.last_update_ms > RX_FRAME_TIMEOUT_MS) {
      send_debug("Cleaning up stale/invalid frame id: " +
                 std::to_string(it->second.frame_id));

      it = frame_table.erase(it);
    } else {
      ++it;
    }
  }
}
