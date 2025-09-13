#include "packets/SerialUSBSocket.h"

#include <Adafruit_TinyUSB.h>
#include <Arduino.h>

void wait_for_serial();

class MySocket final : public packets::SerialUSBSocket {
  void on_unavailable() override { wait_for_serial(); }

  void on_recv(const packets::Packet packet) override {
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

  Serial.begin(115200);

  wait_for_serial();
}

void loop() { my_socket.update(); }
