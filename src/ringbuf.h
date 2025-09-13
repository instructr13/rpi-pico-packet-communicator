#pragma once

#include <array>
#include <optional>

// Fixed-size ringbuf
template <typename T, size_t N> class ringbuf {
  static_assert(N > 0 && (N & (N - 1)) == 0,
                "N must be a power of two and greater than 0");

public:
  [[nodiscard]] size_t size() const { return write_idx - read_idx; }

  [[nodiscard]] size_t capacity() const { return N; }

  [[nodiscard]] bool empty() const { return write_idx == read_idx; }

  [[nodiscard]] bool full() const { return write_idx - read_idx == N; }

  [[nodiscard]] std::optional<T> front() const {
    if (empty()) {
      return std::nullopt;
    }

    return buffer[read_idx & (N - 1)];
  }

  [[nodiscard]] std::optional<T> back() const {
    if (empty()) {
      return std::nullopt;
    }

    return buffer[(write_idx - 1) & (N - 1)];
  }

  bool push(T &&item) {
    if (write_idx - read_idx == N) {
      // Full
      return false;
    }

    buffer[write_idx & (N - 1)] = std::move(item);

    ++write_idx;

    return true;
  }

  bool push_front(T &&item) {
    if (write_idx - read_idx == N) {
      // Full
      return false;
    }

    --read_idx;

    buffer[read_idx & (N - 1)] = std::move(item);

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

  size_t find(const T &item) const {
    for (size_t i = read_idx; i < write_idx; ++i) {
      if (buffer[i & (N - 1)] == item) {
        return i - read_idx;
      }
    }

    return size();
  }

private:
  std::array<T, N> buffer{};
  size_t read_idx = 0;
  size_t write_idx = 0;
};
