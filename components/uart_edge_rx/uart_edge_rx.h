#pragma once

#include "esphome/core/defines.h"

#ifdef USE_ESP8266

#include "esphome/components/uart/uart.h"
#include "esphome/components/uart/uart_component_esp8266.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"

#include "edge_uart_decoder.h"

namespace esphome {
namespace uart_edge_rx {

// Receives for an ESP8266 software UART bus by decoding edge times
// (EdgeUartDecoder) instead of ESPHome's receive interrupt, which samples
// every bit of a byte with interrupts masked. Replaces that interrupt on
// the bus's RX pin and puts each decoded byte into the bus's own receive
// buffer from the interrupt, as ESPHome's interrupt did. On a bus with a
// hardware UART it does nothing.
class UartEdgeRx : public Component {
 public:
  void set_uart(uart::UARTComponent *bus) { this->bus_ = bus; }

  // After the UART bus, whose setup attaches the interrupt replaced here.
  float get_setup_priority() const override;
  void setup() override;
  void loop() override;
  void dump_config() override;

 protected:
  static void edge_isr_(UartEdgeRx *self);
  static void sink_(void *context, uint8_t byte);

  uart::UARTComponent *bus_{nullptr};
  // The bus's software serial; null when the bus has a hardware UART.
  uart::ESP8266SoftwareSerial *software_serial_{nullptr};
  // The bus's own RX pin.
  InternalGPIOPin *rx_pin_{nullptr};
  ISRInternalGPIOPin isr_rx_pin_;
  EdgeUartDecoder decoder_;
  // The software serial's receive ring, written from the interrupt.
  struct Ring {
    uint8_t *buffer{nullptr};
    size_t size{0};
    volatile size_t *in{nullptr};
    size_t *out{nullptr};
  } ring_;
  volatile uint32_t dropped_{0};
  uint32_t last_report_ms_{0};
  uint32_t reported_framing_errors_{0};
  uint32_t reported_dropped_{0};
};

}  // namespace uart_edge_rx
}  // namespace esphome

#endif  // USE_ESP8266
