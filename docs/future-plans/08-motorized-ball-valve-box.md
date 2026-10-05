# Plan 08: Main water inlet valve box (CR02, ESP32 Relay X2)

**Status:** planning, 2026-10-04. Firmware package drafted and compiles; no
hardware yet.
**Scope:** `packages/device-configs/motorized-ball-valve-cr02.yaml`, plus a
new device config `main-water-valve.yaml` once the box exists
**Owner:** Dimitri

## TL;DR

Automated shutoff for the house's main water inlet. An ESPHome `valve` entity
for a 3-wire 2-control motorized ball valve (CR02 / CR301, 9 to 24 V AC/DC),
switched by an ESP32 Relay X2 board with two relays wired in series. One 12 V
DC supply feeds both the board and the valve. A manual shutoff stays in place;
this box adds the automatic one.

## Decisions

### Two relays in series, not one relay per wire

Relay 1 switches power, relay 2 is a changeover that picks the OPEN (NO) or
CLOSE (NC) wire.

| | One relay per wire | Series |
| --- | --- | --- |
| OPEN and CLOSE fed together | Prevented only by software (`interlock:`) | Impossible: a changeover contact touches NO or NC, never both |
| One welded contact | Can combine with the other relay into both wires fed | Power welded: direction still steers. Direction welded: power still cuts |
| Contact wear | Both relays break motor current | Direction relay only switches with power off |
| ESP reboot or crash | Valve holds | Valve holds |

The cost is a short off, set direction, settle, on sequence in firmware.

### CLOSE on the direction relay's NC

If the power relay welds on, or the direction relay loses its coil drive, the
valve ends up closed rather than open. The box exists for automated shutdown,
so a fault that leaves the valve open silently disables it; a fault that
closes it is a visible nuisance, fixed by hand.

Ordinary resets do not trigger this: on a reboot, crash or power cut both
relays are off, nothing is fed, and the valve holds its position.

### Rejected drivers

- **L293D motor shield.** Works: two half-bridges of one channel drive OPEN
  and CLOSE high, COMMON goes to GND, and one bridge cannot drive both
  outputs high at once. Rejected for DC-only operation, about 1.4 V drop,
  and the extra ESP32 board, buck converter and wiring it needs. The
  Arduino-shield variant also routes direction through a 74HC595.
- **ESP32 quad-MOS boards.** Most switch the low side, but CR02 needs the
  switched wire to carry `+` while COMMON stays at `-`. DC only as well.
- **Shelly Plus 2PM.** Shutter mode interlocks natively, but its power metering
  is built for AC and reads DC poorly, and 24 V DC support varies by
  revision.

### Stainless steel body, not brass

The valve sits after the water meter in a new composite (PE-X/Al) section.
Upstream of the meter, and in places near the taps, the pipe stays
galvanized steel.

- No-name brass valves are often high-lead CW617N and rarely
  dezincification-resistant. Stainless releases neither lead nor copper.
- DIN 1988-200 forbids copper upstream of galvanized steel: dissolved copper
  plates onto the zinc and pits it. Stainless releases no copper, so it is
  safe upstream of the remaining steel runs; brass releases a little.
- A stainless ball resists limescale and wear in a valve that sits open for
  months; a chrome-plated brass ball can pit and stick.
- Stainless against galvanized steel corrodes the steel, but here the valve
  never touches steel: composite pipe sits on both sides.

## Hardware

| Item | Choice | Notes |
| --- | --- | --- |
| Valve | 1" (DN25) CR02, 9 to 24 V AC/DC, stainless steel (304 or 316) body | Rated stroke 6 to 8 s, max 5 W |
| Controller | [ESP32 Relay X2](https://devices.esphome.io/devices/esp32-relay-x2/) | ESP32-WROOM-32E, 2 x 10 A relays with COM/NO/NC, 7 to 30 V DC input. Relays on GPIO16/17, button GPIO0, LED GPIO23 |
| Power supply | 12 V DC, 2 A (24 W) | Feeds the board's 7 to 30 V terminal and the valve |
| Programmer | 3.3 V USB-serial (e.g. CP2102N) | First flash only. The 6-pin header is not FTDI pinout; use the onboard buttons for boot mode |
| Enclosure | IP-rated box, cable glands, terminal block | Optional slow-blow fuse (about 1 A) on the valve feed |

### Power supply sizing

The board figure is an estimate; the valve figure comes from its 5 W rating.

| Load | At 12 V |
| --- | --- |
| ESP32 Wi-Fi peaks plus two 5 V relay coils (about 2.5 W) | about 0.25 A |
| Valve running, max 5 W | about 0.42 A |
| Valve motor start, briefly | a few times running current |

Peak stays under about 1.5 A, so 1 A is the floor and 2 A leaves headroom.
2 A is also the common adapter and DIN-rail size.

### Wiring

The relay-to-valve wiring is in the package header. The PSU `+` and `-` also
go to the board's 7 to 30 V input terminal.

## Steps

1. **Bench-identify the valve.** Check wire colours against the label. Feed
   COMMON and OPEN straight from the PSU, time the stroke and measure the
   running current; repeat for CLOSE. Confirm the limit switch cuts current at
   each end. The rated 6 to 8 s stroke may assume 24 V, so time it at 12 V.
2. **Device config.** New top-level `main-water-valve.yaml` (friendly name
   "Main Water Valve") following the repo pattern (secrets to substitutions,
   `wifi.yaml` package, encrypted API and OTA) that includes the valve package
   and sets `valve_name` and `valve_travel_time`. Set `valve_travel_time` to
   about 1.5 x the stroke measured at 12 V: 12 s if it matches the rated 8 s.
   Too short a value leaves the valve part-open.
3. **First flash** over serial, then OTA from then on.
4. **Bench test** with the valve on the bench, not in the pipe:
   - open, close, stop
   - stop mid-stroke, then reverse
   - reboot mid-stroke: the valve must stop and hold
   - the direction relay only clicks while the power relay is open
5. **Install** after the water meter, in the composite section, downstream of
   the existing manual shutoff so the motorized valve can be isolated for
   service, with its manual override reachable.
   - Connect the valve's 1" female threads through the composite system's own
     threaded adapters (brass, gunmetal or PPSU).
   - Work after the meter needs an installer registered with the water
     utility (AVBWasserV §12).
   - The removed steel section may have carried the main equipotential
     bonding (Hauptpotentialausgleich). Have an electrician check the bonding
     of the remaining steel pipes (DIN VDE 0100-540) as part of the same job.
6. **Document:** add the device to the root `README.md` Configurations section.

## Follow-ups (not in the first build)

- **Leak shutoff without Home Assistant.** Wired water probes on spare GPIOs
  close the valve on the board itself, so a leak is still handled while Home
  Assistant or Wi-Fi is down.
- **Latched leak trip.** After a leak shutoff the valve stays closed until a
  person clears the trip; it never reopens on its own.
  - While tripped, open commands are refused, from Home Assistant and locally.
  - The trip survives a reboot (restored global), so a reset cannot clear it.
  - Clearing is exposed to Home Assistant as a "Clear leak trip" button
    entity, also callable from automations and scripts. Clearing only
    re-enables opening; reopening stays a separate, deliberate action.
  - A "Leak trip" binary sensor shows the latched state in Home Assistant.
- **Monthly exercise** on the board, not in Home Assistant: close, then reopen
  at a quiet hour (03:00), so limescale does not seize a valve that sits open
  for months.
- **Current sensing.** An INA219 on the I2C header confirms end of stroke and
  detects a jam, which lets the valve drop `assumed_state`.
- **Local control.** Onboard button as a toggle, LED lit while moving.
