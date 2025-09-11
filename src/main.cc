#include <Arduino.h>
#include <tusb.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <optional>
#include <string>

// Fixed-size ringbuf
template <typename T, size_t N> class ringbuf {
  static_assert(N > 0 && (N & (N - 1)) == 0,
                "N must be a power of two and greater than 0");

public:
  [[nodiscard]] size_t size() const { return write_idx - read_idx; }

  [[nodiscard]] size_t capacity() const { return N; }

  [[nodiscard]] bool empty() const { return write_idx == read_idx; }

  bool push(const T &item) {
    if (write_idx - read_idx == N) {
      // Full
      return false;
    }

    buffer[write_idx & (N - 1)] = item;

    ++write_idx;

    return true;
  }

  std::optional<T> pop() {
    if (write_idx == read_idx) {
      // Empty
      return std::nullopt;
    }

    const auto item = buffer[read_idx & (N - 1)];

    ++read_idx;

    return item;
  }

  // Indexing

  [[nodiscard]] std::optional<T> at(const size_t index) const {
    if (index >= size()) {
      return std::nullopt;
    }

    return buffer[(read_idx + index) & (N - 1)];
  }

  T &operator[](const size_t index) {
    assert(index < size());

    return buffer[(read_idx + index) & (N - 1)];
  }

  // Batch operations

  size_t push(const T *items, const size_t len) {
    const size_t space = N - size();

    const size_t to_write = std::min(len, space);

    if (to_write == 0) {
      return 0;
    }

    const size_t first_chunk = std::min(to_write, N - (write_idx & (N - 1)));

    std::memcpy(&buffer[write_idx & (N - 1)], items, first_chunk * sizeof(T));

    if (first_chunk < to_write) {
      // Wrap around
      std::memcpy(buffer.data(), items + first_chunk,
                  (to_write - first_chunk) * sizeof(T));
    }

    write_idx += to_write;

    return to_write;
  }

  size_t pop(T *items, const size_t len) {
    const size_t to_read = std::min(len, size());

    if (to_read == 0) {
      return 0;
    }

    const size_t first_chunk = std::min(to_read, N - (read_idx & (N - 1)));

    std::memcpy(items, &buffer[read_idx & (N - 1)], first_chunk * sizeof(T));

    if (first_chunk < to_read) {
      // Wrap around
      std::memcpy(items + first_chunk, buffer.data(),
                  (to_read - first_chunk) * sizeof(T));
    }

    read_idx += to_read;

    return to_read;
  }

  void clear() { read_idx = write_idx = 0; }

  void erase_front(const size_t len) {
    const size_t to_erase = std::min(len, size());

    read_idx += to_erase;
  }

  [[nodiscard]] size_t find(const T &item) const {
    for (size_t i = 0; i < size(); ++i) {
      if (buffer[(read_idx + i) & (N - 1)] == item) {
        return i;
      }
    }

    return size(); // Not found
  }

private:
  std::array<T, N> buffer{};
  size_t read_idx = 0;
  size_t write_idx = 0;
};

// CRC16-CCITT Implementation
namespace crc16_ccitt {

constexpr uint16_t POLYNOMIAL = 0x1021;

std::array<std::array<uint16_t, 256>, 8> table;
bool table_initialized = false;

uint16_t basic_crc16(uint16_t crc, const uint8_t *data, const size_t length) {
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

void initialize_table() {
  for (size_t i = 0; i < 256; ++i) {
    const auto byte = static_cast<uint8_t>(i);

    table[0][i] = basic_crc16(0, &byte, 1);
  }

  for (size_t t = 1; t < 8; ++t) {
    for (size_t i = 0; i < 256; ++i) {
      table[t][i] = (table[t - 1][i] >> 8) ^ table[0][table[t - 1][i] & 0xFF];
    }
  }

  table_initialized = true;
}

uint16_t compute(const uint8_t *data, size_t length,
                 const uint16_t initial_crc = 0) {
  if (!table_initialized) {
    initialize_table();
  }

  auto crc = initial_crc;

  while (length >= 8) {
    crc = table[7][(crc >> 8) ^ data[0]] ^ table[6][(crc & 0xFF) ^ data[1]] ^
          table[5][data[2]] ^ table[4][data[3]] ^ table[3][data[4]] ^
          table[2][data[5]] ^ table[1][data[6]] ^ table[0][data[7]];

    data += 8;
    length -= 8;
  }

  for (size_t i = 0; i < length; ++i) {
    crc = (crc << 8) ^ table[0][((crc >> 8) ^ data[i]) & 0xFF];
  }

  return crc;
}

bool verify(const uint8_t *data, const size_t length,
            const uint16_t expected_crc, const uint16_t initial_crc = 0xFFFF) {
  return compute(data, length, initial_crc) == expected_crc;
}

} // namespace crc16_ccitt

namespace cobs {

size_t encode(const uint8_t *input, const size_t length, uint8_t *output) {
  if (length == 0) {
    output[0] = 1;

    return 1;
  }

  const auto *end = input + length;
  auto dst = output;
  auto code_ptr = dst++;

  uint8_t code = 1;

  while (input < end) {
    if (*input == 0) {
      *code_ptr = code;
      code_ptr = dst++;

      code = 1;

      ++input;
    } else {
      *dst++ = *input++;
      ++code;

      if (code == 0xFF) {
        *code_ptr = code;
        code_ptr = dst++;

        code = 1;
      }
    }
  }

  *code_ptr = code;

  return dst - output;
}

bool decode(const uint8_t *input, const size_t length, uint8_t *output,
            size_t &output_length) {
  if (length == 0) {
    output_length = 0;

    return true;
  }

  auto ptr = input;
  const auto *end = input + length;
  auto dst = output;

  while (ptr < end) {
    uint8_t code = *ptr++;

    if (code == 0 || ptr + (code - 1) > end) {
      // Invalid COBS data
      return false;
    }

    for (uint8_t i = 1; i < code; ++i) {
      *dst++ = *ptr++;
    }

    if (code < 0xFF && ptr < end) {
      *dst++ = 0;
    }
  }

  output_length = dst - output;

  return true;
}

} // namespace cobs

namespace bytes {

// Little-endian encoder
class Encoder {
public:
  explicit Encoder(std::vector<uint8_t> &buffer) : buffer(buffer) {}

  void push_byte(const uint8_t byte) const { buffer.push_back(byte); }

  void push_bytes(const uint8_t *data, const size_t length) const {
    buffer.insert(buffer.end(), data, data + length);
  }

  template <typename T> void push_number(const T number) const {
    static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>);

    const auto ptr = reinterpret_cast<const uint8_t *>(&number);

    push_bytes(ptr, sizeof(T));
  }

  void push_bool(const bool value) const { push_byte(value ? 0xFF : 0); }

  void push_string(const std::string &str) const {
    push_bytes(reinterpret_cast<const uint8_t *>(str.data()), str.size());
  }

private:
  std::vector<uint8_t> &buffer;
};

// Consuming little-endian decoder
class Decoder {
public:
  explicit Decoder(const uint8_t *data, const size_t length)
      : data(data), length(length) {}

  [[nodiscard]] size_t remaining() const { return length - offset; }

  [[nodiscard]] uint8_t pop_byte() {
    if (length <= offset) {
      valid = false;

      return 0;
    }

    const auto byte = data[offset++];

    return byte;
  }

  void pop_bytes(uint8_t *dst, const size_t len) {
    if (remaining() < len) {
      valid = false;

      std::memset(dst, 0, len);

      return;
    }

    std::memcpy(dst, data + offset, len);

    offset += len;
  }

  template <typename T> [[nodiscard]] T pop_number() {
    static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>);

    if (remaining() < sizeof(T)) {
      valid = false;

      return T{};
    }

    T number;

    pop_bytes(reinterpret_cast<uint8_t *>(&number), sizeof(T));

    return number;
  }

  [[nodiscard]] bool pop_bool() { return pop_byte() != 0; }

  [[nodiscard]] std::string pop_string(const size_t len) {
    if (remaining() < len) {
      valid = false;

      return {};
    }

    std::string str(reinterpret_cast<const char *>(data + offset), len);

    offset += len;

    return str;
  }

  [[nodiscard]] bool good() const { return valid; }

private:
  bool valid = true;
  const uint8_t *data;
  size_t offset = 0;
  size_t length;
};

} // namespace bytes

namespace packets {
// COBS max chunk size
constexpr size_t MAX_CHUNK_ESTIMATE_SIZE = 64;
constexpr size_t MAX_CHUNK_SIZE =
    44; // 64 - 5 (cobs) - 14 (header+crc) - 1 (delimiter)
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

  using buf_type = ringbuf<uint8_t, 4096>;

  virtual ~Socket() = default;

  virtual bool is_available() = 0;

  void send(const Packet &packet) {
    send_frame(packet.type, next_frame_id++, packet.payload.data(),
               packet.payload.size());
  }

  void send_debug(const std::string &str) {
    std::vector<uint8_t> payload;

    const bytes::Encoder encoder{payload};

    encoder.push_string(str);

    send(Packet(TYPE_DEBUG_ECHO, std::move(payload)));
  }

  void update() {
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

    recv_raw_data(rx_buffer);

    if (!rx_buffer.empty()) {
      process_rx_buffer();
    }

    cleanup_stale_frames();
  }

protected:
  void send_chunk(const uint16_t type, const uint32_t frame_id,
                  const uint16_t total_chunks, const uint16_t chunk_index,
                  const uint8_t *data, const uint16_t len) {
    assert(len <= MAX_CHUNK_SIZE);

    std::vector<uint8_t> buffer;
    const bytes::Encoder encoder(buffer);

    encoder.push_number(type);
    encoder.push_number(frame_id);
    encoder.push_number(total_chunks);
    encoder.push_number(chunk_index);
    encoder.push_number(len);

    encoder.push_bytes(data, len);

    const auto crc = crc16_ccitt::compute(data, len);

    encoder.push_number(crc);

    const auto cobs_len =
        cobs::encode(buffer.data(), buffer.size(), cobs_buffer.data());

    cobs_buffer[cobs_len] = 0;

    // write encoded then 0x00 as delimiter
    queue_send_raw_data(cobs_buffer.data(), cobs_len + 1);
  }

  void send_frame(const uint16_t type, const uint32_t frame_id,
                  const uint8_t *payload, const size_t payload_len) {
    assert(payload_len <= MAX_FRAME_SIZE);

    uint16_t total_chunks = (payload_len + MAX_CHUNK_SIZE - 1) / MAX_CHUNK_SIZE;

    if (total_chunks == 0)
      total_chunks = 1;

    for (uint16_t i = 0; i < total_chunks; ++i) {
      const size_t offset = static_cast<size_t>(i) * MAX_CHUNK_SIZE;
      const uint16_t min = std::min(payload_len - offset, MAX_CHUNK_SIZE);

      send_chunk(type, frame_id, total_chunks, i, payload + offset, min);
    }
  }

private:
  std::vector<Frame> frame_table{};
  buf_type rx_buffer{};
  std::vector<uint8_t> contiguous_buffer{};
  buf_type tx_buffer{};

  std::array<uint8_t, MAX_CHUNK_ESTIMATE_SIZE> decoded_buffer{};
  std::array<uint8_t, MAX_CHUNK_ESTIMATE_SIZE> cobs_buffer{};

  bool prev_availability = false;
  size_t next_frame_id = 0;

  virtual void on_unavailable() {}

  virtual void on_available() {}

  virtual void recv_raw_data(buf_type &data) = 0;

  virtual size_t send_raw_data(const uint8_t *data, size_t len) = 0;

  virtual void on_recv(Packet packet) = 0;

  std::optional<Packet> recv(const uint8_t *buffer, const size_t length) {
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
      const auto frame = std::find_if(
          frame_table.begin(), frame_table.end(),
          [frame_id](const Frame &f) { return f.frame_id == frame_id; });

      if (frame != frame_table.end()) {
        frame->invalid = true;
      }

      send_debug("CRC mismatch");

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

      for (uint16_t i = 0; i < frame->total_chunks; ++i) {
        full_payload.insert(full_payload.end(), frame->chunks[i].begin(),
                            frame->chunks[i].end());

        if (full_payload.size() > MAX_FRAME_SIZE) {
          send_debug("Frame size overflow");

          // Overflow, mark frame as invalid
          frame->invalid = true;

          return std::nullopt;
        }
      }

      auto packet = Packet(frame->type, std::move(full_payload));

      // Remove frame from table
      frame_table.erase(std::remove_if(frame_table.begin(), frame_table.end(),
                                       [frame_id](const Frame &f) {
                                         return f.frame_id == frame_id;
                                       }),
                        frame_table.end());

      return packet;
    }

    // Not complete yet
    return std::nullopt;
  }

  void process_rx_buffer() {
    while (true) {
      const size_t idx = rx_buffer.find(0x00);

      if (idx == rx_buffer.size()) {
        // No complete chunk yet
        break;
      }

      if (idx > 0) {
        contiguous_buffer.resize(idx);

        const auto read = rx_buffer.pop(contiguous_buffer.data(), idx);

        if (read != idx) {
          // Should not happen
          send_debug("Ring buffer pop error");

          // Remove the problematic chunk
          rx_buffer.erase_front(idx + 1);

          continue;
        }

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
    }
  }

  void queue_send_raw_data(const uint8_t *data, const size_t len) {
    tx_buffer.push(data, len);

    flush_tx_buffer();
  }

  void flush_tx_buffer() {
    contiguous_buffer.resize(tx_buffer.size());

    const auto read = tx_buffer.pop(contiguous_buffer.data(), tx_buffer.size());
    const auto written = send_raw_data(contiguous_buffer.data(), read);

    if (written < read) {
      // Push back unwritten data
      tx_buffer.push(contiguous_buffer.data() + written, read - written);
    }
  }

  Frame *get_or_create_frame(uint16_t type, uint32_t frame_id,
                             uint16_t total_chunks) {
    for (auto &e : frame_table) {
      if (e.frame_id != frame_id)
        continue;

      if (e.type != type) {
        // Mark as invalid if type doesn't match
        e.invalid = true;

        return nullptr;
      }

      return &e;
    }

    frame_table.emplace_back(type, frame_id, total_chunks);

    return &frame_table.back();
  }

  void cleanup_stale_frames() {
    const auto now = millis();

    for (auto it = frame_table.begin(); it != frame_table.end();) {
      if (it->invalid || now - it->last_update_ms > RX_FRAME_TIMEOUT_MS) {
        send_debug("Cleaning up stale/invalid frame id: " +
                   std::to_string(it->frame_id));

        it = frame_table.erase(it);
      } else {
        ++it;
      }
    }
  }
};

class SerialUSBSocket : public Socket {
public:
  bool is_available() override {
    // DTR status indicates if the host is connected
    return tu_bit_test(tud_cdc_n_get_line_state(0), 0);
  }

private:
  void recv_raw_data(buf_type &data) override {
    static std::vector<uint8_t> tmp;
    tud_task();
    uint32_t avail;

    while ((avail = tud_cdc_available()) > 0) {
      tmp.resize(avail);

      const auto read = tud_cdc_read(tmp.data(), avail);
      tud_task();

      if (read > 0) {
        data.push(tmp.data(), read);
      } else {
        break;
      }
    }
  }

  size_t send_raw_data(const uint8_t *data, const size_t len) override {
    tud_task();
    size_t written = 0;

    while (written < len) {
      const auto write_ptr = data + written;
      const auto write_len = len - written;

      uint32_t avail = tud_cdc_write_available();

      if (avail == 0)
        break;

      const uint32_t to_write = std::min<uint32_t>(avail, write_len);
      const uint32_t current_written = tud_cdc_write(write_ptr, to_write);

      if (current_written == 0)
        break;

      tud_cdc_write_flush();

      written += current_written;
    }

    return written;
  }
};

} // namespace packets

void wait_for_serial();

class MySocket final : public packets::SerialUSBSocket {
  void on_unavailable() override { wait_for_serial(); }

  void on_recv(packets::Packet packet) override {
    // Echo back the received packet
    send(packet);
  }
} my_socket;

void wait_for_serial() {
  bool led_state = false;

  while (!Serial) {
    delay(150);

    led_state = !led_state;

    digitalWrite(LED_BUILTIN, led_state ? HIGH : LOW);
  }

  digitalWrite(LED_BUILTIN, LOW);
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin();

  wait_for_serial();
}

void loop() { my_socket.update(); }
