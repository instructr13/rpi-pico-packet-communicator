#include "SerialUSBSocket.h"

using namespace packets;

void SerialUSBSocket::yield() { tud_task(); }

void SerialUSBSocket::recv_raw_data(buf_type &rx_buffer) {
  if (Serial.available() == 0) {
    return;
  }

  const auto read = Serial.read(tmp_rx_buffer.data(), tmp_rx_buffer.size());

  if (read > 0) {
    if (rx_buffer.full()) {
      // No space
      return;
    }

    rx_buffer.push(tmp_rx_buffer.data(), read);
  }
}

void SerialUSBSocket::send_raw_data(buf_type &tx_buffer) {
  const auto avail =
      std::min<size_t>(Serial.availableForWrite(), tx_buffer.size());

  if (avail == 0)
    return;

  const auto to_write = tx_buffer.pop(tmp_tx_buffer.data(),
                                      std::min(tmp_tx_buffer.size(), avail));

  tud_cdc_write(tmp_tx_buffer.data(), to_write);
  tud_cdc_write_flush();
}
