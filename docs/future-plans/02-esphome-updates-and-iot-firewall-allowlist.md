# Plan 02 — ESPHome fleet update + IoT-VLAN firewall allowlist

**Status:** planning only — no code yet
**Execution order:** **2nd** (after Plan 01 roaming) — bump fleet to latest ESPHome; fix the
BT-proxy egress error surfaced by the last flash
**Scope:** all 21 device configs + `packages/`; the IoT VLAN firewall (router)
**Owner:** Dimitri
**Date:** 2026-07-14

---

## 0. TL;DR

- The repo is **already broadly modern** — the big 2024→2025 breakers (`ota:` list form,
  `api: encryption`, `api: actions:`, `esp32:`/`esp8266:` blocks, current BLE keys) are all done.
  Only a **handful of small warnings** remain (§A.3).
- The IoT VLAN blocks outbound by default. Only **two classes of device need genuine INTERNET
  egress**: the **6 Bluetooth proxies** (firmware-update manifest at `firmware.esphome.io:443`)
  and **gaszahler** (public NTP pool). Everything else is **LAN-only** and instead needs an
  **inbound** allow from the Home Assistant host (native API is inbound, not outbound).
- **Blocking prerequisite:** `manual_ip` currently sets **`dns1: 0.0.0.0`** (no resolver) — the
  two internet cases will **silently fail to resolve** until a real `dns1:` is configured and
  UDP 53 is allowed.
- The "bluetooth proxies fetch templates from the internet" belief is **partly a
  misconception**: the github `dashboard_import` is **compile-time only**. Their *runtime* reach
  is the **firmware update manifest**, not templates.

Related: this touches every device's `packages:` block — sequence with Plan 05 (remote packages)
and Plan 01 (roaming); see §4.

> **Observed symptom (Dimitri):** after flashing the latest BT-proxy firmware, the device log
> shows a **"failed to fetch template"** (or similar fetch/HTTP) error. This is almost certainly
> the runtime egress failure this plan predicts — see §B.6. It is the concrete trigger for doing
> the firewall + DNS fix as part of this workstream.

---

## SECTION A — Update readiness

### A.1 Device / framework matrix (verified from configs + `.esphome/storage/*.json`)

Installed ESPHome tooling seen in build artifacts: up to **2026.5.3**.

| # | Device | MCU / board | Framework | `min_version` | Last compiled |
|---|---|---|---|---|---|
| 1 | air-quality-sensor-1 | ESP32-S2 (`esp32-s2-saola-1`) | **arduino** | 2024.6.0 | 2025.8.0 |
| 2 | 3d-printer-socket → sonoff-basic | ESP8266 (`esp01_1m`) | arduino | — | 2026.2.2 |
| 3 | biqu-air-filter-socket → sonoff-basic | ESP8266 (`esp01_1m`) | arduino | — | 2026.2.2 |
| 4 | compressor → fantastic-outdoor-socket | ESP8266 (`esp8285`) | arduino | — | 2026.2.2 |
| 5 | compressor-valve → fantastic-outdoor-socket | ESP8266 (`esp8285`) | arduino | — | 2026.2.2 |
| 6 | bluetooth-proxy-bedroom | ESP32 (`esp32dev`) | **esp-idf** | 2026.5.1 | 2026.5.3 |
| 7 | bluetooth-proxy-buro (+mmWave) | ESP32 (`esp32dev`) | esp-idf | 2026.5.1 | 2025.11.4 |
| 8 | bluetooth-proxy-buro-meraner-2 | ESP32 (`m5stack-atom`) | esp-idf | 2026.5.1 | 2025.11.4 |
| 9 | bluetooth-proxy-kitchen | ESP32 (`esp32dev`) | esp-idf | 2026.5.1 | 2025.11.4 |
| 10 | bluetooth-proxy-living-room | ESP32 (`m5stack-atom`) | esp-idf | 2026.5.1 | 2026.2.4 |
| 11 | bluetooth-proxy-wr (+RTTTL) | ESP32 (`esp32dev`) | esp-idf | 2026.5.1 | 2026.2.4 |
| 12 | diesel-heater (CC1101 RF) | ESP32 (`esp32dev`) | esp-idf | — | 2026.3.3 |
| 13 | garden-watering-controller (XL9535) | ESP32 (`esp32dev`) | **arduino** | — | 2025.7.2 |
| 14 | gaszahler | ESP8266 (`esp01_1m`) | arduino | — | 2025.8.0 |
| 15 | lilygos3-info-screen (T-Display-S3) | ESP32-S3 | **arduino** | 2025.7.0 | 2025.8.0 |
| 16 | minimal-ota | ESP8266 (`d1_mini`) | arduino | — | 2025.6.3 |
| 17 | qrcode2-atom-lite-1 | ESP32 (`m5stack-atom`) | **arduino** | 2024.6.0 | 2025.8.0 |
| 18 | qrcode2-atom-lite-2 | ESP32 (`m5stack-atom`) | arduino | 2024.6.0 | 2025.8.0 |
| 19 | smart-electric-meter (OBIS, MQTT) | ESP8266 (`d1_mini`) | arduino | 2025.7.0 | 2026.3.3 |
| 20 | soil-sensor-tuyas → soil-sensor-d1-mini | ESP8266 (`d1_mini`) | arduino | — | 2025.5.0 |
| 21 | wasserstand-regenreservoir (ADS1115) | ESP8266 (`d1_mini`) | arduino | — | 2025.8.0 |

**Split:** **12 ESP32-family, 9 ESP8266** (the BT-proxy/qrcode `esp32:` blocks live in their
packages, so a top-level grep undercounts). Only the **6 BT proxies + diesel-heater are esp-idf**;
everything else is arduino. (This matters for Plan 01 — roaming needs esp-idf.)

### A.2 Already correct (no action)

`ota:` list form everywhere · `api: encryption: key:` (never `password:`) · `api: actions:`
(gaszahler:30, rtttl-buzzer:100) · `esp32:`/`esp8266:` blocks · BLE `scan_parameters:`/
`bluetooth_proxy: active:` current · modern sensor filters (`or: [throttle, delta]`,
`calibrate_linear`, EMA/SMA, `clamp`) · `deep_sleep: esp32_ext1_wakeup:` · `time: sntp|homeassistant`.

### A.3 Warnings / attention items (file:line)

| # | Item | Where | Action |
|---|---|---|---|
| 1 | `api: custom_services: true` (legacy naming) | `diesel-heater.yaml:56` | Migrate to `api: actions:`; verify on next 2026.x bump |
| 2 | **DNS = `0.0.0.0`** (no resolver) | `packages/wifi.yaml:6-9` `manual_ip` (expanded: `…bedroom…validated.yaml:86-87`) | Add real `dns1:` — **blocks §B internet cases** |
| 3 | `min_version: 2026.5.1` on all 6 BT proxies | `packages/device-configs/bluetooth-proxy.yaml:5,10` | Intentional forward pin; move whole fleet to one current release together |
| 4 | `web_server:` with no `version:` | `garden-watering-controller.yaml:40`, `air-quality-sensor-1.yaml:69` | Pin `web_server: version: 3` (or 2) to avoid upgrade ambiguity |
| 5 | Stale `min_version: 2024.6.0` (~2 yr old) | `air-quality-sensor-1.yaml:28`, `qrcode2-atom-lite.yaml:24` | Bump to the version you actually validate |
| 6 | Arduino-ESP32 fragile compile-time asset fetches | `air-quality-sensor-1.yaml:129-219`, `lilygos3-info-screen.yaml:1012-1138,141` (`gfonts://`, `mdi:`, `github://landonr/...`) | These break the **build host**, not the device; pin/verify on bump |

**Overall:** low-risk. Recommended approach — bump the whole fleet to a single current ESPHome
release, rebuild each, fix warnings #1/#4 opportunistically, and re-flash. No hard breakers.

---

## SECTION B — IoT-VLAN firewall allowlist

### B.1 Key concept — inbound vs outbound

ESPHome's **native API is inbound**: Home Assistant dials the device on **TCP 6053**. So
`time: homeassistant`, `homeassistant`-platform sensors, and `ota: platform: esphome` (HA/dashboard
pushes, TCP 3232 esp32 / 8266 esp8266) are **not** device egress. For a pure-API device the only
outbound is mDNS multicast (224.0.0.251:5353), and the real firewall need is an **inbound** allow
from the HA host.

**Compile-time ≠ runtime:** `dashboard_import` (`github://…`), remote `external_components`,
`gfonts://`, `mdi:` fetches all happen on the **build host** — **not** IoT-VLAN egress. → answers
the "BT proxies fetch templates from the internet" question: they don't, at runtime; they fetch a
**firmware update manifest** (below).

### B.2 Devices needing genuine INTERNET egress (must be explicitly allowed out)

| Device(s) | Destination | Port/proto | Source of requirement |
|---|---|---|---|
| **6× bluetooth-proxy** | **`firmware.esphome.io`** | TCP 443 (HTTPS) | `update:` + `ota: platform: http_request` (`packages/device-configs/bluetooth-proxy.yaml:26-48`; manifest URL e.g. `bluetooth-proxy-bedroom.yaml:14`) |
| **gaszahler** | **`0.pool.ntp.org`, `1.pool.ntp.org`** | UDP 123 | `gaszahler.yaml:116-117` |
| *(both of the above)* | **DNS resolver** (see §B.4) | UDP 53 | needed to resolve the hostnames above |

### B.3 LAN-only devices (no internet egress; need inbound-from-HA and/or LAN broker)

| Device | Outbound need | Inbound need |
|---|---|---|
| smart-electric-meter | MQTT broker `MQTT_BROKER_IP:1883` (device→broker) | — (no API) |
| wasserstand-regenreservoir | SNTP → gateway/router IP, UDP 123 | HA→6053 |
| soil-sensor-tuyas | mDNS only | HA→6053 |
| lilygos3-info-screen | mDNS only (all data over inbound API) | HA→6053 |
| qrcode2-atom-lite-1/2 | mDNS only | HA→6053 |
| air-quality-sensor-1 | mDNS only | HA→6053, web_server |
| garden-watering-controller | mDNS only | HA→6053, web_server |
| diesel-heater | mDNS only (RF is 433 MHz, not IP) | HA→6053 |
| compressor / compressor-valve / 3d-printer-socket / biqu-air-filter-socket | mDNS only | HA→6053, OTA 3232/8266 |
| minimal-ota | mDNS only | OTA push |

### B.4 Cross-cutting prerequisites (must fix before the allowlist works)

1. **DNS.** `manual_ip` yields `dns1: 0.0.0.0`. Add a real `dns1:` (router or chosen resolver) to
   `packages/wifi.yaml` **and** allow UDP 53 to it, or the two internet cases silently fail. (An
   IP-literal broker/NTP works without DNS; the two public-hostname cases do not.)
2. **Concrete IPs live in `secrets.yaml`** (`mqtt_broker`, `wifi_manual_ip_gateway`, HA host).
   **Do NOT paste those into this doc — the repo is public.** Fill them into the router firewall
   rules directly from `secrets.yaml`. This doc uses symbolic names (`MQTT_BROKER_IP`,
   `GATEWAY_IP`, `HA_HOST_IP`) on purpose.

### B.5 Concrete allowlist to build (symbolic → fill from secrets.yaml)

```
# Egress (IoT VLAN → out)
ALLOW  iot/<6 bt-proxy IPs>   ->  firmware.esphome.io   tcp/443
ALLOW  iot/<gaszahler IP>     ->  0.pool.ntp.org,1.pool.ntp.org  udp/123
ALLOW  iot/<bt-proxies+gaszahler>  ->  <DNS_RESOLVER_IP>  udp/53
ALLOW  iot/<smart-electric-meter IP>  ->  MQTT_BROKER_IP  tcp/1883
ALLOW  iot/<wasserstand IP>   ->  GATEWAY_IP  udp/123
ALLOW  iot/all                ->  224.0.0.251  udp/5353   # mDNS (LAN multicast)
# Inbound (HA host -> IoT VLAN) for native-API + OTA devices
ALLOW  HA_HOST_IP  ->  iot/<api devices>  tcp/6053
ALLOW  HA_HOST_IP  ->  iot/<esp32 devices> tcp/3232   # esphome OTA push
ALLOW  HA_HOST_IP  ->  iot/<esp8266 devices> tcp/8266 # esphome OTA push
```

### B.6 Diagnosing the observed BT-proxy "failed to fetch template" error

After the last flash the BT proxies log a fetch/HTTP failure. Expected root cause = the runtime
egress from B.2 being blocked. Diagnose in this order:

1. **DNS first** — with `dns1: 0.0.0.0`, the `update:`/`ota: http_request` component cannot resolve
   `firmware.esphome.io`, which surfaces as a fetch failure even if the firewall were open. Fix
   B.4 #1 before blaming the firewall.
2. **Firewall egress** — once DNS resolves, confirm the IoT VLAN permits the proxy IP →
   `firmware.esphome.io:443` (B.5). A blocked TLS connect looks identical in the log.
3. **Decide if you even want it** — this fetch is the **firmware auto-update manifest**, not a
   runtime dependency for BLE proxying. If you'd rather not open internet egress for the proxies,
   **disable the `update:`/`ota: http_request` block** (`packages/device-configs/bluetooth-proxy.yaml:26-48`)
   and update via the ESPHome dashboard (inbound) instead — the error disappears and no egress rule
   is needed. This is the cleaner option for a locked-down IoT VLAN (see Open Q4).

Reproduce/verify: watch the device log (`esphome logs`) right after boot; the fetch attempt fires
on the `update:` poll interval.

## Open questions for Dimitri
1. Which resolver should `dns1:` point at (router `GATEWAY_IP`, or a specific DNS host)?
2. Bump the whole fleet to one target ESPHome version now — which? (latest 2026.5.x seen is 2026.5.3.)
3. OK to migrate `diesel-heater` `custom_services` → `actions:` in the same pass?
4. Do you want the BT proxies' runtime firmware auto-update to stay enabled at all? If not, we can
   drop the `firmware.esphome.io` egress entirely and update via the dashboard (inbound) instead.
