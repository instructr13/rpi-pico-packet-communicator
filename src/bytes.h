#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace pcomm::bytes {

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
