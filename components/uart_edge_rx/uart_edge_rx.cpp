#include "uart_edge_rx.h"

#ifdef USE_ESP8266

#include <cinttypes>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#ifdef USE_UART_WAKE_LOOP_ON_RX
#include "esphome/core/wake.h"
#endif

#include "byte_ring.h"

namespace esphome {
namespace uart_edge_rx {

static const char *const TAG = "uart_edge_rx";

// Setup runs this far below the UART bus's priority, so after it.
static constexpr float SETUP_AFTER_BUS = 1.0f;
// Framing errors and dropped bytes are logged at most this often.
static constexpr uint32_t REPORT_INTERVAL_MS = 60000;

namespace {

// Access to protected members of ESPHome's ESP8266 UART classes (checked
// against ESPHome 2026.9.1). ESP8266UartComponent is final, so this uses
// the explicit-instantiation rule: access is not checked for the template
// arguments of an explicit instantiation. A renamed or retyped member
// fails the build here.
template<typename Tag, typename Tag::type MEMBER> struct MemberAccess {
  friend typename Tag::type member_of(Tag) { return MEMBER; }
};

struct RxPinOf {
  using type = InternalGPIOPin *uart::UARTComponent::*;
  friend type member_of(RxPinOf);
};
struct SoftwareSerialOf {
  using type = uart::ESP8266SoftwareSerial *uart::ESP8266UartComponent::*;
  friend type member_of(SoftwareSerialOf);
};
struct RxBuffer {
  using type = uint8_t *uart::ESP8266SoftwareSerial::*;
  friend type member_of(RxBuffer);
};
struct RxBufferSize {
  using type = size_t uart::ESP8266SoftwareSerial::*;
  friend type member_of(RxBufferSize);
};
struct RxInPos {
  using type = volatile size_t uart::ESP8266SoftwareSerial::*;
  friend type member_of(RxInPos);
};
struct RxOutPos {
  using type = size_t uart::ESP8266SoftwareSerial::*;
  friend type member_of(RxOutPos);
};

template struct MemberAccess<RxPinOf, &uart::UARTComponent::rx_pin_>;
template struct MemberAccess<SoftwareSerialOf, &uart::ESP8266UartComponent::sw_serial_>;
template struct MemberAccess<RxBuffer, &uart::ESP8266SoftwareSerial::rx_buffer_>;
template struct MemberAccess<RxBufferSize, &uart::ESP8266SoftwareSerial::rx_buffer_size_>;
template struct MemberAccess<RxInPos, &uart::ESP8266SoftwareSerial::rx_in_pos_>;
template struct MemberAccess<RxOutPos, &uart::ESP8266SoftwareSerial::rx_out_pos_>;

}  // namespace

float UartEdgeRx::get_setup_priority() const { return setup_priority::BUS - SETUP_AFTER_BUS; }

void IRAM_ATTR UartEdgeRx::edge_isr_(UartEdgeRx *self) {
  self->decoder_.edge(micros(), self->isr_rx_pin_.digital_read());
}

void IRAM_ATTR UartEdgeRx::sink_(void *context, uint8_t byte) {
  auto *self = static_cast<UartEdgeRx *>(context);
  if (!ring_push(self->ring_.buffer, self->ring_.size, *self->ring_.in, *self->ring_.out, byte)) {
    self->dropped_ = self->dropped_ + 1;
    return;
  }
#ifdef USE_UART_WAKE_LOOP_ON_RX
  // As ESPHome's own receive interrupt does: the consumer drains promptly.
  wake_loop_isrsafe();
#endif
}

void UartEdgeRx::setup() {
  // On the ESP8266 every UART bus is an ESP8266UartComponent.
  auto *bus = static_cast<uart::ESP8266UartComponent *>(this->bus_);
  this->software_serial_ = bus->*member_of(SoftwareSerialOf{});
  this->rx_pin_ = this->bus_->*member_of(RxPinOf{});
  if (this->software_serial_ == nullptr || this->rx_pin_ == nullptr) {
    this->software_serial_ = nullptr;
    this->disable_loop();
    return;
  }

  uart::ESP8266SoftwareSerial *serial = this->software_serial_;
  this->ring_.buffer = serial->*member_of(RxBuffer{});
  this->ring_.size = serial->*member_of(RxBufferSize{});
  this->ring_.in = &(serial->*member_of(RxInPos{}));
  this->ring_.out = &(serial->*member_of(RxOutPos{}));

  this->decoder_.configure(this->bus_->get_baud_rate(), this->bus_->get_data_bits(),
                           this->bus_->get_parity() != uart::UART_CONFIG_PARITY_NONE);
  this->decoder_.set_sink(UartEdgeRx::sink_, this);
  this->isr_rx_pin_ = this->rx_pin_->to_isr();
  // One handler per pin: this replaces the software serial's interrupt.
  this->rx_pin_->attach_interrupt(UartEdgeRx::edge_isr_, this, gpio::INTERRUPT_ANY_EDGE);
}

void UartEdgeRx::loop() {
  {
    // flush() shares the frame state with the edge interrupt.
    InterruptLock lock;
    this->decoder_.flush(micros());
  }

  const uint32_t now = millis();
  if (now - this->last_report_ms_ < REPORT_INTERVAL_MS) return;
  this->last_report_ms_ = now;
  const uint32_t framing_errors = this->decoder_.framing_errors();
  const uint32_t dropped = this->dropped_;
  if (framing_errors == this->reported_framing_errors_ && dropped == this->reported_dropped_) return;
  ESP_LOGW(TAG, "Framing errors: %" PRIu32 " (+%" PRIu32 "), bytes dropped on a full buffer: %" PRIu32 " (+%" PRIu32
           ")",
           framing_errors, framing_errors - this->reported_framing_errors_, dropped, dropped - this->reported_dropped_);
  this->reported_framing_errors_ = framing_errors;
  this->reported_dropped_ = dropped;
}

void UartEdgeRx::dump_config() {
  ESP_LOGCONFIG(TAG, "UART edge-timed receive:");
  if (this->software_serial_ == nullptr) {
    ESP_LOGCONFIG(TAG, "  Bus has a hardware UART; not active");
    return;
  }
  LOG_PIN("  RX Pin: ", this->rx_pin_);
}

}  // namespace uart_edge_rx
}  // namespace esphome

#endif  // USE_ESP8266
