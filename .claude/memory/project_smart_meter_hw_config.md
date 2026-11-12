---
name: project_smart_meter_hw_config
description: "Smart meter reader hardware facts - Logarex LK13BE606739 (SML 9600 8N1), sensitised IR head needs an optical filter, 10 kOhm external pullup"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-11T23:30:00.000Z
  originSessionId: 80e07557-6a3d-49ac-a47c-39b8647e81e4
---

The smart-electric-meter device (Wemos D1 Mini, ESPHome, package
`packages/device-configs/electricity-meter.yaml`) reads the meter through a
Stromleser serial IR head on D2 (GPIO4) with ESPHome's ESP8266 software
UART.

- **Meter: Logarex LK13BE606739.** It pushes binary SML at 9600 baud 8N1,
  about one telegram per second. Verified 2026-11-11 with throw-away
  probe firmware (8N1 decode with 0 framing errors, SML escape sequence
  found). The previous meter, an LK13BE 803039, sent OBIS text at 9600
  7E1.
- **IR head is sensitised:** its photodiode load resistor was raised from
  stock ~13 kOhm (`12C`) to 33 kOhm (`333`) for the old meter's weak LED.
  The new meter's LED overdrives it: rising edges arrived about 47 us late
  and the software UART, which samples about 31 us into each bit, misread
  every 1 bit following a 0 bit (`1b` read as `12`). Fix: an optical
  filter of about 10 layers of blue painter's tape between meter and head
  brought the lateness to 16-17 us and a 100% clean read (the user,
  2026-11-11; confirmed in the probe log the same day).
- **External 10 kOhm pullup** RX (D2) to 3.3 V; ESP8266 internal pullup
  stays OFF. A stuck-LOW RX line means the head or its wiring, not the
  firmware (seen 2026-11-11 while the head was being handled).

**Why:** the signal problems look like firmware faults but are optical;
both meters' failures were fixed at the head.

**How to apply:** if readings stop, check optics first: filter still in
place, head seated, line idles HIGH. Weak-signal procedure for the old
meter: `docs/electricity-meter-weak-ir-fixes.md`. Why the firmware decodes
SML itself: `docs/electricity-meter-sml-decoding.md`.
