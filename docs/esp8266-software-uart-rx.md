# ESP8266 Software UART Receive

Why `components/uart_edge_rx` exists, how it fixes the ESP8266 software
UART, and the alternative that was considered.

## Problem

On the ESP8266, ESPHome uses a hardware UART only when RX is GPIO3 or GPIO13
(UART0) or GPIO8 (UART1); every other RX pin gets `ESP8266SoftwareSerial`
(ESPHome 2026.9.1, `uart/__init__.py` and `uart_component_esp8266.cpp`).
Its receive interrupt reads a whole byte inside the interrupt, busy-waiting
through every bit with interrupts masked: about 1 ms per byte at 9600 baud.

The smart electric meter's IR head is on D2 (GPIO4). Its meter pushes a
660-byte SML telegram every second, so interrupts were masked for most of
each second. The device reset by hardware watchdog every few to 25 minutes.
Reboot diagnostics (an NMI sampler of the interrupted program counter)
showed, for every reset:

- the CPU spinning in a dead loop after the Wi-Fi driver's assertion
  `dev 1163` in `wDev_ProcessFiq`, the Wi-Fi interrupt handler;
- the remaining samples in the software UART's busy-wait with interrupts
  masked.

Espressif's guidance, as quoted on the community forums, is that interrupts
stay masked no longer than about 50 µs. The link between the masked
interrupts and the Wi-Fi assertion is inferred, not proven; the fix was
chosen as the probable cause and judged by whether the resets stop.

## Fix: receive by edge timing

`EdgeUartDecoder` works from the time and level of each edge, a few
microseconds of interrupt time each, and turns each interval into bits by
rounding to whole bit times. It takes the frame format from the bus: 5 to
8 data bits, parity read and ignored as ESPHome's own receivers do, 1 or
2 stop bits. Rounding tolerates edges up to almost half a bit late, more
than the software UART's fixed sample point.

## Option 1, chosen: automatic bus fix (`uart_edge_rx`)

The device keeps a normal `uart:` bus. `uart_edge_rx` names that bus:

```yaml
uart_edge_rx:
  - uart_id: uart_bus
```

- Configuration checks that the bus is a `uart:` bus at 300 to 38400 baud
  and listed once.
- Code is generated only on the ESP8266, and only for a bus with an RX pin;
  otherwise it does nothing.
- At runtime it checks whether the bus got a software UART. If so, it
  attaches its own interrupt to the bus's RX pin, which replaces the
  software UART's interrupt (the core keeps one handler per pin), and puts
  each decoded byte into the software UART's receive buffer from the
  interrupt, as that interrupt did, waking the main loop where ESPHome
  does. Every device on the bus reads through the normal `read()`. If the
  bus got a hardware UART, it steps aside.
- Packages can therefore add it for their own bus unconditionally.

It is coupled to ESPHome internals, without modifying them: it reads the
protected members `UARTComponent::rx_pin_` and
`ESP8266UartComponent::sw_serial_`, and the software UART's buffer members.
A rename or type change of any of them fails the build. Behaviour that
would not fail the build and must be re-checked on ESPHome upgrades:

- the software UART must keep its receive buffer in those members, with
  one slot free and `in == out` meaning empty;
- it must not re-attach its interrupt after setup. Today only
  `load_settings()` does, when the bus is reconfigured at runtime, and that
  silently brings the interrupt-masking receiver back.

## Option 2, alternative: explicit edge bus

A separate bus component implementing ESPHome's public `UARTComponent`
interface, with the edge decoder as its receiver. A device that needs it
declares that bus instead of `uart:`, by hand or through a substitution
that selects the bus definition.

- Not coupled to ESPHome internals.
- Not automatic: every affected device has to choose it.
- Not implemented; the decoder would be reused as is.

## Rejected

- **Hardware UART pin.** Moving the head to GPIO13 and the logger to UART1
  removes the problem, but needs rewiring.
- **Edge receiver inside the meter reader.** Hidden, meter-only, and the
  same coupling to ESPHome internals as option 1.
- **Locking the reader to a pin.** Dropping `uart_id` would break every
  hardware UART on the ESP32 and ESP8266.

## Limits

- Receive only. ESPHome's ESP8266 software transmit masks interrupts for
  every byte it sends. GPIO2 transmits through the hardware UART1; other
  pins would need a timer-driven transmitter.
- Low baud rates only: interrupt latency and jitter must stay well under
  half a bit, 52 µs at 9600 baud.
- Two edges within one interrupt delay reach the decoder as one edge to
  the level the line already had. That frame is dropped as a framing
  error. As on any UART, the decoder then resynchronises on the next
  falling edge, so bytes sent back to back after any framing error can
  arrive misframed until the line idles. Parity is not checked, so
  nothing below the consumer catches such a byte.
- On GPIO3 or GPIO13 the bus can still fall back to the software UART at
  runtime, for example when the logger holds UART0; the runtime check
  covers that case.
