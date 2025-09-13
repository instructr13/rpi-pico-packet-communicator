#pragma once

#include <Adafruit_TinyUSB.h>

#include "../packets.h"

namespace pcomm::socket {

class SerialUSBSocket : public Socket {
public:
  bool is_available() override {
    // DTR status indicates if the host is connected
    return tu_bit_test(tud_cdc_n_get_line_state(0), 0);
  }

private:
  std::array<uint8_t, CFG_TUD_CDC_RX_BUFSIZE> tmp_rx_buffer{};
  std::array<uint8_t, CFG_TUD_CDC_TX_BUFSIZE> tmp_tx_buffer{};

  void yield() override;

  void recv_raw_data(buf_type &rx_buffer) override;

  void send_raw_data(buf_type &tx_buffer) override;
};

} // namespace packets
