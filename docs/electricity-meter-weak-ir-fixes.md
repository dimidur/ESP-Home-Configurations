# Weak IR Signal: Fixes for Logarex LK13BE 803039

This document collects software and hardware fixes for the case where the
ESPHome smart-meter reader can no longer reliably decode the IR push-mode
output of a Logarex LK13BE 803039.

## Background

The Logarex LK13BE 803039 emits IEC-62056-21 ASCII OBIS in passive **push
mode** at **9600 baud, 7E1 (7 data bits, EVEN parity, 1 stop bit)**. These
serial parameters are correct and confirmed by multiple independent sources
(volkszaehler wiki, German PV/KNX community forums). They are not the source
of the problem and should be left as-is.

This particular meter model is **reported in the community** (notably
on the German simon42 forum, in a thread titled "Logarex LK13BE 803039
IR-Sendeschwach") to have a comparatively weak IR transmitter, with
several users describing decode quality near the lower limit of common
optical-head sensitivity. Reports of "worked for some time then degraded
to unusable" appear in that thread as well.

On this build, the same hardware ran reliably for years before going
intermittent over a short period. Whether that is meter-side LED aging,
head-side degradation, or accumulated optical contamination is not
established here.

The reader hardware in this project is a Wemos D1 Mini (ESP8266) with a
Stromleser serial IR head (micro-USB connector type) wired to
**D2 (GPIO4)** as RX. Hardware UART is intentionally not used in this
build.

## Quick triage: where on the chain is the problem?

Run through these in order before changing anything:

1. **Positioning mode on, watch the counters.**
   The package exposes two diagnostic sensors:
   - `UART Lines Seen`: lines the reader delivered plus the lines of
     dropped SML telegrams, counted before any filter while positioning
     mode is on. Zero means **no UART activity at all**.
   - `OBIS Valid Frame Rate`: the share of lines that passed validation.
     Lines the line filter rejects are not counted, so with garbled text
     it can stay high; the `meter_raw` log lines (INFO, positioning mode
     only) show what actually arrives.

   Interpretation:

   | UART Lines Seen | Diagnosis |
   | --- | --- |
   | stays 0 | No signal at all: wiring, pullup, or head alignment |
   | grows, `meter_raw` lines garbled | Signal present but corrupted: marginal optics or framing |
   | grows, `meter_raw` lines clean | Working normally |

2. **Phone-camera test (front camera).**
   Point the phone's selfie camera at the meter's IR window during a data
   burst. A *healthy* meter LED is clearly visible as a flashing
   white/purple dot, comparable in brightness to a TV remote LED.
   On the LK13BE 803039 the LED is documented to be so dim that it may be
   **invisible to a phone camera even when working**. So:
   - Visible flashes → LED is fine, problem is downstream.
   - Invisible → either LED is genuinely dim *or* simply consistent with
     this meter's known low output. Not conclusive on its own.

3. **Clean both optical surfaces** (meter IR window and head lens) with
   isopropanol and a lint-free cloth. Re-seat the head, check if the
   counters move.

## Software-side defensive settings

These are not fixes for the weak-signal problem on this build; that was
resolved by the hardware modification described further below. They are
general settings that keep the software UART path robust enough to make
the most of whatever signal the head delivers.

### Internal pullup on RX (alternative to the external 10 kΩ)

This build uses an external 10 kΩ pullup and leaves the ESP8266 internal
pullup off. If you want to avoid the external resistor, the internal
pullup can be enabled in the YAML instead. It has not been tested as a
direct substitute on this build, but it is the lowest-effort alternative.

In `packages/device-configs/electricity-meter.yaml`:

```yaml
uart:
  rx_pin:
    number: ${uart_rx_pin}
    mode:
      input: true
      pullup: true
  baud_rate: ${uart_baud_rate}
  id: uart_bus
  parity: ${uart_parity}
  data_bits: ${uart_data_bits}
  stop_bits: ${uart_stop_bits}
  rx_buffer_size: ${uart_rx_buffer_size}
```

The current build keeps this off because an external 10 kΩ pullup is
already fitted.

### UART RX buffer size

The device config sets the buffer size; the package has no default.
`smart-electric-meter.yaml` uses 1024 bytes, which holds a whole 660-byte
SML telegram from the LK13BE606739 and absorbs long bursts while main-loop
latency varies (Wi-Fi, MQTT, OTA):

```yaml
substitutions:
  uart_rx_buffer_size: "1024"
```

Compared with 256 bytes it costs 768 bytes of RAM, which the ESP8266 can
spare comfortably here.

### Keep the logger out of the UART path

The package logs verbosely in positioning mode. Outside of positioning,
keep `logger.level: INFO` (already the case). Avoid `DEBUG` or `VERBOSE`
during normal operation: long log bursts can starve the software-serial
ISR on the ESP8266 and corrupt frames at marginal signal levels.

## Hardware modifications on this build

Two specific hardware changes apply to this reader:

1. **External 10 kΩ pullup** between RX (D2 / GPIO4) and 3.3 V. This has
   been in place since the original build and was never changed.
2. **Photodiode load resistor on the IR head changed from the stock
   ~12 kΩ to 33 kΩ.** This is the recent fix that restored reliable
   decode. See the next section for how to do the swap.

The internal ESP8266 pullup on D2 is **off** (`mode: pullup: true` is not
set in the YAML).

The IR head is a Stromleser serial IR head with a micro-USB connector.

## RX-side pullup: what was tried, what worked

Only the head's load resistor changed in this debugging cycle. The ESP-side
10 kΩ external pullup was the same value throughout.

| Head load resistor | ESP-side RX pullup | Result |
| --- | --- | --- |
| stock (~12 kΩ) | 10 kΩ external | worked reliably for years, then degraded to weak / unreliable decode |
| stock (~12 kΩ) | 47 kΩ external | no signal at all |
| stock (~12 kΩ) | none (floating) | no signal at all; device may also fail to boot cleanly |
| **modified (33 kΩ)** | **10 kΩ external** | **clean decode, effectively zero garbage** |

### Boot reliability note

A pullup on RX is required. With no pullup at all (internal off and no
external), the line floats and the device was observed not to boot
cleanly.

### TX line

The UART config in this build does not assign a `tx_pin`, so the ESP
does not drive any pin for UART TX. No external TX pullup is needed or
fitted. This section is left as a placeholder in case a TX pin is ever
added (e.g. for IEC mode-C handshake meters that require sending `/?!`),
in which case a 10 kΩ pullup TX → 3.3 V is the conventional choice.

## Hardware fix: IR receiver resistor swap on the reading head

The community fix documented for this meter (simon42 thread cited above)
is to raise the photodiode load resistor on the reading head from the
stock value to 33 kΩ. This is the change that resolved the issue on
this build.

### The change

**Replace the photodiode load resistor on the head PCB.** On this build
the original part read ~13 kΩ in-circuit (marked `12C`) and was
replaced with **33 kΩ** (marked `333`).

### How to find the right resistor

Different reading heads use different amplifier topologies, so there is
no single component reference designator. Use these clues to identify the
correct part:

1. **Open the head**: usually two screws or a snap-fit shell. The PCB
   inside is small (often <2 cm²) and has only a handful of components.

2. **Locate the photodiode / phototransistor.** It is the clear or dark
   3 mm package facing the optical window, on the *receive* side of the
   PCB. Some heads have one optical component (phototransistor only),
   others have two (separate IR LED for TX and PIN photodiode for RX).

3. **Find the resistor directly connected to the photodiode's output
   pin** (usually the collector, on the side opposite the side that goes
   to GND). On the head used in this build it is the resistor going
   from the photodiode collector to VCC.

4. **Read the SMD code.** SMD resistors use one of two marking
   conventions and you must know which one you are looking at, because
   the same characters can mean very different values:

   **3-digit JEDEC** (digits only; first two = significant figures,
   third = power-of-ten multiplier):
   - `103` = 10 kΩ
   - `123` = 12 kΩ
   - `153` = 15 kΩ
   - `223` = 22 kΩ
   - `333` = 33 kΩ ← target value

   **EIA-96** (two digits + a letter; the digits look up a value from a
   table, the letter is the multiplier). Letters used as multipliers:
   `Z`=×0.001, `Y/R`=×0.01, `X/S`=×0.1, `A`=×1, `B/H`=×10, `C`=×100,
   `D`=×1000, `E`=×10 000, `F`=×100 000. Common photodiode-load codes:
   - `01C` = 100 × 100 = 10.0 kΩ
   - `12C` = 130 × 100 = **13.0 kΩ**
   - `19C` = 154 × 100 = 15.4 kΩ
   - `26C` = 182 × 100 = 18.2 kΩ
   - `33C` = 215 × 100 = 21.5 kΩ
   - For 33 kΩ replacement: 3-digit `333`, EIA-96 `51C` (332 × 100 =
     33.2 kΩ, the closest E96 value).

   **Important: `120` vs `12C`.** These look almost identical under
   poor lighting but mean wildly different things. `120` (3-digit
   JEDEC) = 12 × 10⁰ = **12 Ω**, which makes no sense as a photodiode
   load. `12C` (EIA-96) = **13 kΩ**, which is exactly the value you
   are looking for. If you see `12?` next to the photodiode and are
   not sure whether the third character is `0` or `C`, **it is almost
   certainly `C`**. Confirm with a magnifier (the `C` has a clear
   curve, the `0` is a closed oval) or by measuring (next step).

5. **Always confirm with a multimeter.** With the head unpowered and
   ideally desoldered on at least one pad, measure across the resistor
   on the 200 kΩ range. Anything in the **~10–22 kΩ** range sitting
   next to the photodiode is your target. In-circuit readings can be
   slightly lower than nominal due to parallel paths through the rest
   of the circuit, so 13 kΩ marked may read as ~10–12 kΩ in-circuit;
   that's fine.

### Doing the swap

- 0603 or 0805 SMD, hot-air or fine-tip iron with flux. A 33 kΩ 0603
  costs cents.
- After the swap, reassemble and re-test in positioning mode. The
  expected outcome is clean `meter_raw` lines and a steady
  `OBIS Valid Frame Rate`, reliable decode at 5–10 mm distance, and
  tolerance for small alignment errors.

## Recommended order of operations (for re-debugging in future)

1. Confirm the 10 kΩ external RX pullup is still in place and the device
   boots. Without any pullup, expect no signal and possible boot trouble.
2. Clean both optical surfaces (meter IR window and head lens) with
   isopropanol, re-seat head, observe `UART Lines Seen`,
   `OBIS Valid Frame Rate` and the `meter_raw` log lines in positioning
   mode.
3. If signal is weak or absent and the head is **stock** (~12 kΩ load),
   do the **12 kΩ → 33 kΩ swap** on the head's photodiode load resistor
   (see the resistor swap section above). This is the change that
   resolved the issue on this build.
4. As a last resort: replace the head, or escalate to the DSO to swap
   the meter for one with a stronger IR LED.

Steps 1 and 2 are reversible and safe. Step 3 (the resistor swap on
the head PCB) modifies the head and is the fix that worked on this
build. Step 4 (replacing the head or escalating to the DSO) is a last
resort.
