# Electricity Meter Package

A comprehensive ESPHome package for reading OBIS-compliant smart electric
meters via optical interface (IR head) or serial connection.

## Features

- **OBIS Protocol Support**: Reads standard OBIS codes for energy
  consumption, power, voltage, current, frequency, and phase angles
- **Data Validation**: Built-in validation to filter out corrupted readings
  and unrealistic values
- **Spike Protection**: Prevents sudden large changes in sensor values that
  could indicate data corruption
- **Communication Quality Monitoring**: Tracks frame reception rates and
  corruption statistics
- **Text Sensor Filtering**: Requires consecutive identical readings (3 by
  default) for text sensors to prevent corruption
- **Comprehensive Sensors**: Supports energy consumption (total, daily,
  weekly, monthly, yearly), power, voltage, current, frequency, and phase
  angle measurements
- **Reset Controls**: Buttons to reset validation state and communication
  quality counters
- **Positioning Mode**: Real-time raw sensor feedback for physically
  aligning the optical IR head without touching production data
- **Highly Configurable**: All OBIS codes and validation parameters
  configurable via substitutions

## Important Notes

⚠️ **Custom Components Required**: This package uses the custom
`electricity_meter_reader`, `uart_edge_rx` and `deduplicate_text`
components. You must include the external components in your main
configuration:

```yaml
external_components:
  - source:
      type: local
      path: components
```

⚠️ **C++17 Support for ESP32**: This package uses C++17 features (like
`std::string_view` and `constexpr` lambdas), and the
`electricity_meter_reader` and `deduplicate_text` components also require
C++17 or later support when compiling for ESP32. ESP8266 works out of the
box.

**ESPHome 2025.7.0 and Later:**
Starting with ESPHome 2025.7.0, the ESP32 toolchain supports C++20 by
default and no manual configuration is required. This package will work out
of the box without any additional settings.

**ESPHome Versions Below 2025.7.0 or Standalone Usage:**
For ESPHome versions below 2025.7.0, or when using this package separately
outside of ESPHome, you still need to manually enable C++17 support for
ESP32. Add the following to your ESPHome configuration:

```yaml
esphome:
  name: your-device-name
  platformio_options:
    build_unflags:
      - -std=gnu++11
    build_flags:
      - -std=gnu++17
```

⚠️ **ESP32 Testing Status**: While this package compiles successfully for
ESP32 with C++17/C++20 enabled, it has only been physically tested and
verified on ESP8266 devices. ESP32 compatibility is theoretical based on
successful compilation.

⚠️ **Linter Warning**: ESPHome linters may show warnings/errors about "Could
not find directory ..." or "Platform not found: ...". This is expected and
will resolve when the package is used in a configuration that includes the
external component, IDE restart might be required.

## Required Substitutions

### UART Configuration

```yaml
uart_rx_pin: "D2"           # GPIO pin for UART RX
uart_baud_rate: "9600"      # UART baud rate (1200, 2400, 4800, 9600)
uart_parity: "NONE"         # UART parity (EVEN, ODD, NONE)
uart_data_bits: "8"         # UART data bits (7, 8)
uart_stop_bits: "1"         # UART stop bits (usually 1)
uart_rx_buffer_size: "1024" # UART RX buffer size
```

SML meters need 8 data bits and no parity; text meters that send 7E1 are
read correctly at that setting too.

### Sensor Group Control

Each group is required; `"true"` disables its processing and hides its
sensors by default:

```yaml
disabled_core_sensors: "false"         # total consumption, current power
disabled_feed_in_sensors: "true"       # total feed-in
disabled_phase_power_sensors: "false"  # active power L1/L2/L3
disabled_voltage_sensors: "false"      # voltage L1/L2/L3
disabled_current_sensors: "false"      # current L1/L2/L3
disabled_frequency_sensors: "false"    # frequency
disabled_phase_angle_sensors: "false"  # phase angles
disabled_tariff_sensors: "true"        # T1/T2 consumption
disabled_consumption_since_reset_sensors: "true"
disabled_rolling_window_sensors: "false"  # 1d/7d/30d/365d consumption
disabled_diagnostic_sensors: "true"    # serial, firmware, CRC, status
disabled_quality_sensors: "false"      # communication quality
```

### OBIS Codes

All OBIS codes must be defined according to your meter's specification.
Each code must be unique: two equal codes give the dispatch `switch` two
identical `case` labels, and the build fails.

```yaml
# Energy consumption and feed-in
obis_total_consumption: "1-0:1.8.0*255"
obis_total_feed_in: "1-0:2.8.0*255"
obis_consumption_1d: "1-0:1.8.0*96"
obis_consumption_7d: "1-0:1.8.0*97"
obis_consumption_30d: "1-0:1.8.0*98"
obis_consumption_365d: "1-0:1.8.0*99"
obis_consumption_t1: "1-0:1.8.1*255"
obis_consumption_t2: "1-0:1.8.2*255"
obis_consumption_since_reset: "1-0:1.8.0*100"

# Power and electrical parameters
obis_current_power: "1-0:16.7.0*255"
obis_power_l1: "1-0:36.7.0*255"
obis_power_l2: "1-0:56.7.0*255"
obis_power_l3: "1-0:76.7.0*255"
obis_voltage_l1: "1-0:32.7.0*255"
obis_voltage_l2: "1-0:52.7.0*255"
obis_voltage_l3: "1-0:72.7.0*255"
obis_current_l1: "1-0:31.7.0*255"
obis_current_l2: "1-0:51.7.0*255"
obis_current_l3: "1-0:71.7.0*255"
obis_frequency: "1-0:14.7.0*255"

# Phase angles
obis_phase_angle_ul2_ul1: "1-0:81.7.1*255"
obis_phase_angle_ul3_ul1: "1-0:81.7.2*255"
obis_phase_angle_il1_ul1: "1-0:81.7.4*255"
obis_phase_angle_il2_ul2: "1-0:81.7.15*255"
obis_phase_angle_il3_ul3: "1-0:81.7.26*255"

# Meter information
obis_serial_number: "1-0:96.1.0*255"
obis_firmware_version: "1-0:0.2.0*255"
obis_parameter_crc: "1-0:96.90.2*255"
obis_status_register: "1-0:97.97.0*255"
```

### Validation Constants

```yaml
# Spike protection limits
max_power_change: "3000.0"    # Maximum power change per reading (W)
max_current_change: "13.0"    # Maximum current change per reading (A)
max_energy_change: "1.0"      # Maximum energy change per reading (kWh)
max_rolling_energy_change: "2.0" # Same, for rolling window sensors (kWh)

# Absolute limits
max_energy_total: "100000.0"  # Maximum total energy (kWh)
max_absolute_power: "34500.0" # Maximum absolute power (W)
min_energy_value: "0.0"       # Minimum energy value (kWh)
min_power_value: "0.0"        # Minimum power value (W)

# Voltage limits
max_voltage: "280.0"          # Maximum voltage (V)
min_voltage: "0.0"            # Minimum voltage (V)

# Current limits
max_current: "30.0"           # Maximum current (A)
min_current: "0.0"            # Minimum current (A)

# Frequency limits
max_frequency: "52.0"         # Maximum frequency (Hz)
min_frequency: "48.0"         # Minimum frequency (Hz)

# Phase angle limits
max_phase_angle: "360.0"      # Maximum phase angle (degrees)
min_phase_angle: "-360.0"     # Minimum phase angle (degrees)

# Rolling window maximum values (kWh)
max_consumption_1d: "30.0"
max_consumption_7d: "210.0"
max_consumption_30d: "800.0"
max_consumption_365d: "10000.0"
```

### Optional Tunables

The package's own `substitutions:` block holds tunables with defaults: the
line filter's minimum length, the identical readings a text sensor needs,
each sensor group's throttle and delta, the quality sensors' timing and
the ESP8266 heap sweep. Define any of them in your configuration to
override the default.

## Usage

1. **Include the package** in your ESPHome configuration:

   ```yaml
   packages:
     electricity-meter: !include packages/device-configs/electricity-meter.yaml
   ```

2. **Define all required substitutions** (see above sections)

3. **Configure UART hardware** according to your meter's specifications

4. **Connect the IR head** to the specified UART RX pin

## Hardware Setup

### IR Head Connection

- Connect the IR head's data output to the specified UART RX pin
- Connect power and ground as required by your IR head
- Position the IR head over the meter's optical interface

### Common UART Settings by Meter Type

- **German smart meters**: Often use 9600 baud, 7 data bits, even parity
- **SML meters** (binary, for example Logarex LK13BE606xxx): 9600 baud, 8
  data bits, no parity. The package decodes each CRC-checked telegram into
  the same OBIS lines a text meter sends; text meters are read correctly
  at this setting too
- **Older meters**: May use 2400 or 4800 baud
- **Check your meter's documentation** for the correct settings

## Sensors Provided

### Energy Sensors

- `Total Consumption` - Total energy consumption (kWh)
- `Consumption 1 Day` - Energy consumption last 24 hours
- `Consumption 7 Days` - Energy consumption last 7 days
- `Consumption 30 Days` - Energy consumption last 30 days
- `Consumption 365 Days` - Energy consumption last 365 days
- `Consumption T1` - Energy consumption tariff 1
- `Consumption T2` - Energy consumption tariff 2
- `Consumption Since Reset` - Energy since last reset

### Power and Electrical Sensors

- `Current Power` - Current power consumption (W)
- `Power L1/L2/L3` - Active power per phase (W)
- `Voltage L1/L2/L3` - Line voltages (V)
- `Current L1/L2/L3` - Line currents (A)
- `Frequency` - Network frequency (Hz)
- `Phase Angle UL2:UL1` - Phase angle between L2 and L1 voltages
- `Phase Angle UL3:UL1` - Phase angle between L3 and L1 voltages
- `Phase Angle IL1:UL1` - Phase angle between L1 current and voltage
- `Phase Angle IL2:UL2` - Phase angle between L2 current and voltage
- `Phase Angle IL3:UL3` - Phase angle between L3 current and voltage

### Diagnostic Sensors

- `OBIS Valid Frame Rate` - Percentage of successfully processed frames
- `Serial Number` - Meter serial number
- `Firmware Version` - Meter firmware version
- `Parameter CRC` - Parameter checksum
- `Status Register` - Meter status information
- `UART Lines Seen` - Lines the reader delivered, counted while
  Positioning Mode is on

### Positioning Mode Sensors (disabled by default)

These sensors are only active while the `Positioning Mode` switch is on.
They bypass all validation and throttling, publishing every raw parsed value
(~1 per second from the meter).

- `Raw Current Power` - Unvalidated current power reading (W)
- `Raw Current L1` - Unvalidated L1 current reading (A)
- `Raw Current L2` - Unvalidated L2 current reading (A)
- `Raw Current L3` - Unvalidated L3 current reading (A)

### Control Buttons

- `Reset Validation State` - Resets all validation counters
- `Reset Communication Quality` - Resets communication quality counters

### Control Switches

- `Enable Warning Logs` - Toggles WARN-level rejection log messages
- `Positioning Mode` - Enables raw real-time sensor output for IR head
  alignment

## Data Validation

The package includes several validation mechanisms:

1. **Energy Validation**: Ensures energy values are ever-growing and changes
   are reasonable
2. **Spike Protection**: Prevents sudden large changes in power and current
   readings
3. **Text Sensor Filtering**: Requires consecutive identical readings (3 by
   default) for text sensors
4. **Bounds Checking**: Rejects values outside realistic ranges and counts
   them as corrupt
5. **Corruption Detection**: Tracks and reports data corruption events

## Positioning Mode

When physically mounting and aligning the optical IR head on the meter, use
Positioning Mode to get near real-time feedback without needing to enable
debug logging or waiting for throttled sensor updates.

### How to use

1. In Home Assistant, **enable** the four raw sensors: `Raw Current Power`,
   `Raw Current L1`, `Raw Current L2`, `Raw Current L3` (they are
   `disabled_by_default: true`)
2. **Turn on** the `Positioning Mode` switch in Home Assistant
3. **Adjust the IR head position** while watching the raw sensor values
   update approximately every second; stable, realistic values indicate good
   optical coupling
4. Once positioned correctly, **turn off** `Positioning Mode`: the raw
   sensors are immediately cleared to unavailable (NaN) so they don't
   clutter dashboards
5. Optionally disable the raw sensors again in Home Assistant

### Log output

While Positioning Mode is active, each parsed power reading also emits an
INFO-level log line visible in the ESPHome log stream (no debug logging
required):

```text
[I][meter]: [positioning] Power: 1234 W
```

### Notes

- Raw sensors bypass all validation (spike protection, range limits, delta
  checks) and all throttle/delta filters; every parseable reading is
  published immediately
- Production sensors (`Current Power`, `Current L1/L2/L3`, etc.) are
  completely unaffected while Positioning Mode is active
- `Positioning Mode` switch restores to OFF after a reboot
  (`restore_mode: RESTORE_DEFAULT_OFF`) so it cannot be accidentally left on

## Troubleshooting

### No Data Received

- Check UART wiring and settings
- Use **Positioning Mode** (see above) for real-time feedback while
  adjusting the IR head
- Check baud rate and parity settings
- Raw lines are logged at INFO level (`meter_raw`) while Positioning Mode
  is on

### Incorrect Values

- Verify OBIS codes match your meter's specification
- Check units and scaling factors
- Adjust validation constants if needed
- Review meter documentation for correct codes

### High Corruption Rate

- Check IR head positioning and cleanliness
- Verify UART settings match meter output
- Check for electrical interference
- Ensure stable power supply

## Example Configuration

See `packages/device-configs/examples/example-electric-meter-usage.yaml` for
a complete example configuration.

## Compatibility

This package is designed to work with:

- OBIS-compliant smart meters
- IEC 62056-21 text protocol and SML; SML telegrams must carry a
  CRC-16/X25 checksum (a meter using CRC-16/KERMIT has every telegram
  dropped)
- ESPHome version 2024.6.0 or later
- ESP8266 platforms (tested and verified)
- ESP32 platforms (compiles successfully but not tested on real hardware)

## Contributing

When adding support for new meter types:

1. Add the specific OBIS codes to the documentation
2. Test with real hardware
3. Update validation constants as needed
4. Consider adding meter-specific examples
