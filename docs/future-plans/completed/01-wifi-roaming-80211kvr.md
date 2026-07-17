# Plan 01 — Enable 802.11k/v (and prepare for 11r) roaming on ESPHome devices

**Status:** ✅ **COMPLETED 2026-07-15** — implemented, validated, flashed, pushed.
`packages/wifi-roaming-11kv.yaml` (enable_rrm/btm) on the 6 BT proxies; `wifi-powersave-off.yaml`
on the mains ESP32-arduino devices; diesel-heater (CC1101) and single-AP sockets deliberately
excluded. Commits `16ff65d` (roaming+powersave) and `f7d5790` (socket fast_connect revert).
**Execution order:** **1st** (cheapest change, immediate benefit) — then Plan 02 → 03 → 04 → 05
**Scope:** `packages/wifi.yaml` + per-device `wifi:` on eligible ESP32 devices
**Owner:** Dimitri
**Date:** 2026-07-14

---

## 0. TL;DR

The IoT network now runs AP-side soft-roaming (802.11k RRM, 802.11v BSS-TM/WNM, usteer, all live
on 3 APs; 802.11r FT staged OFF, `mobility_domain=f1a7`). On the ESPHome side:

- **Only ESP32-on-esp-idf devices can opt into 11k/v** via `enable_rrm:` / `enable_btm:`. That is
  7 esp-idf devices, but **diesel-heater is EXCLUDED** (its CC1101 RF is corrupted by roam-scan
  channel hopping — it deliberately runs `fast_connect: true`), leaving **6 targets: the Bluetooth
  proxies**.
- **✅ IMPLEMENTED (2026-07-15):** generic package `packages/wifi-roaming-11kv.yaml` created;
  included **per-device** (opt-in) in all 6 proxies so the capability is reusable by any future
  ESP32/esp-idf device, not coupled to the proxy role. All 6 pass `esphome config`.
- **ESP32-on-arduino (5 devices)** and **all ESP8266 (10 devices)** **cannot** set these keys —
  they'd fail to compile. They still benefit from **AP-side** steering (usteer 11v hints +
  deauth-kick fallback); nothing to configure on-device.
- **802.11r / FT has no ESPHome support at all** — the pending 11r cannot be consumed by any
  ESPHome device regardless of framework. Action there is **network-side only** (keep it in mixed
  mode when you enable it) plus tracking the upstream ESPHome feature request.

So the concrete change is small: add `enable_rrm: true` + `enable_btm: true` to the 7 esp-idf
ESP32 devices, ideally via a small composable package so it never lands on an incompatible target.

---

## 1. Network capabilities (from Dimitri) vs ESPHome support

| Standard | Network status | ESPHome client support |
|---|---|---|
| **802.11k** (RRM — neighbor/beacon reports) | ✅ live, 3 APs | `enable_rrm:` — **ESP32 + esp-idf only** |
| **802.11v** (BSS-TM / WNM — AP suggests better AP) | ✅ live, 3 APs (wpad-mbedtls) | `enable_btm:` — **ESP32 + esp-idf only** |
| **usteer** (OpenWrt AP-side steering brain) | ✅ live, peered | AP-side; no client config. Uses 11v hint, deauth-kick fallback |
| **802.11r** (FT — fast key handoff) | ⏸️ OFF, `ieee80211r=0`, `mobility_domain=f1a7` staged | **Not supported by ESPHome** (no `ieee80211r`/FT key) |

**Verified facts (esphome.io + source):**
- `enable_rrm` (802.11k) and `enable_btm` (802.11v): **ESP32 only, esp-idf framework only** — not
  ESP8266, not ESP32-arduino. Gated by the `USE_WIFI_11KV_SUPPORT` build flag → sets
  `btm_enabled`/`rm_enabled` in the IDF WiFi stack. Both default **off**.
- Setting either **auto-disables** ESPHome's crude built-in `post_connect_roaming` (default true;
  checks ≤3×/5 min, switches on +10 dB) — which is what we want: hand steering to the APs/usteer.
- `power_save_mode` (ESP32 default `LIGHT`) — higher levels "decrease reliability … frequent
  disconnections". `fast_connect` skips the scan and locks to the first BSSID seen → **counter-
  productive for roaming**; keep it **off**.

---

## 2. Per-device eligibility (from Plan 02 framework matrix)

**Full fleet accounting (nothing missed):** 21 devices = **12 ESP32 + 9 ESP8266**. Of the 12
ESP32: **7 esp-idf** (eligible) + **5 arduino** (not eligible as-is). So **7 eligible now**.

| Class | Devices | 11k/v action |
|---|---|---|
| **ESP32 + esp-idf → ELIGIBLE (7)** | 6× bluetooth-proxy (bedroom, buro, buro-meraner-2, kitchen, living-room, wr) + diesel-heater | **Add `enable_rrm: true` + `enable_btm: true`** |
| **ESP32 + arduino → not eligible as-is (5)** | air-quality-sensor-1 (S2), garden-watering-controller, lilygos3-info-screen (S3), **qrcode2-atom-lite-1/2 (the barcode scanners)** | No on-device change. *Optional:* migrate to esp-idf to gain 11k/v (bigger change — see §4) |
| **ESP8266 → never eligible (9)** | sonoff ×2, fantastic sockets ×2, gaszahler, minimal-ota, smart-electric-meter, soil-sensor-tuyas, wasserstand | No on-device change; rely on AP-side usteer + deauth-kick |

> **Re: the barcode scanners** — `qrcode2-atom-lite-1/2` (M5Stack Atom) **are ESP32**, but on the
> **arduino** framework, so they are in the middle bucket, not eligible for `enable_rrm`/`btm`
> until migrated to esp-idf (§4). They are not missed — they're the two arduino-ESP32 scanners.
> No other ESP32 devices exist beyond the 12 listed here.

---

## 3. How to apply it safely (don't break the 14 ineligible devices)

`enable_rrm`/`enable_btm` are **invalid keys** on ESP8266/arduino → putting them in the shared
`packages/wifi.yaml` (included by everything) would **break every non-idf build**. Two options:

**Option A (recommended) — composable package.** Add `packages/wifi-roaming-11kv.yaml`:
```yaml
wifi:
  enable_rrm: true
  enable_btm: true
```
Include it **only** from the 7 esp-idf targets. The 6 BT proxies already pull `../wifi.yaml` via
`packages/device-configs/bluetooth-proxy.yaml:30`, so the cleanest wiring is to add the roaming
include there (all 6 proxies inherit it) + add it once to `diesel-heater.yaml`. This matches the
repo's package idiom and Plan 05's "thin, composable" principle.

**Option B — inline.** Add the two keys directly to each of the 7 configs' `wifi:` block. Simpler
to read, but 7 edit sites and easy to drift.

**Also consider (mains-powered ESP32 only):** `power_save_mode: none` on the 6 BT proxies +
diesel-heater for snappier 11v response and fewer marginal-RSSI drops (same lesson as the
CapiBridge WiFi fix, Plan 04). Do **not** disable power save on battery/deep-sleep devices — moot
here since those are all esp8266/arduino anyway. Keep `fast_connect` off everywhere (roaming).

---

## 4. Optional: migrate ESP32-arduino devices to esp-idf (to gain 11k/v)

Only if you want the 5 arduino-ESP32 devices to do client-side 11k/v too. Not free:
- `lilygos3-info-screen` depends on `github://landonr/lilygo-tdisplays3-esphome` — **verify it
  builds under esp-idf** before attempting (display/LVGL components are often arduino-tuned).
- `qrcode2-atom-lite-1/2` use the local `qrcode2_uart` C++ component — must compile under esp-idf.
- `garden-watering-controller` uses XL9535 + `web_server` — check component compatibility.
- `air-quality-sensor-1` (S2) — check sensor stack.

Recommendation: **defer**. These 5 already get AP-side steering (usteer deauth-kick). Treat esp-idf
migration as a separate, per-device evaluation, not part of this pass.

---

## 5. CapiBridge — 11k/v as a separate workstream

Per Dimitri: worth adding 11k/v to the **CapiBridge gateway** too (tracked under Plan 04). It's a
custom Arduino-ESP32 sketch, and the Arduino-ESP32 core sits on top of ESP-IDF, so the underlying
RRM/BTM APIs are reachable directly — `esp_rrm_send_neighbor_rep_request()` (11k neighbor report)
and `esp_wnm_send_bss_transition_mgmt_query()` (11v BTM query), plus `esp_wifi_set_rssi_threshold()`
to drive a roam scan on weak signal. This is **out of scope for this ESPHome plan** but should ride
along with the CapiBridge WiFi-reconnect fix (Plan 04) since both touch the same WiFi bring-up code.
→ see Plan 04 §3 for where it plugs in.

## 6. 802.11r / FT — no ESPHome action (intentionally off)

- ESPHome exposes **no** `ieee80211r`/FT option, so ESPHome clients can't consume 11r regardless of
  framework. This is consistent with the network keeping it **codified but not applied**
  (`ieee80211r=0`, `mobility_domain=f1a7` pre-staged) — **leave it off until there's a real
  use case**. Nothing to do on the device side now.
- If/when a real use case appears and it's enabled: keep it **mixed / FT-optional**, never FT-only,
  so ESP8266 and non-FT ESP32 clients can still associate in the mobility domain. Track the upstream
  ESPHome FT feature request in parallel.

---

## 7. Validation

- After enabling, device logs should show BTM/RRM negotiated at association (esp-idf WiFi logs).
- Walk a device between APs; confirm it follows usteer's 11v `bss_transition` hint (roam without a
  full disconnect) instead of only reacting to a deauth-kick.
- Check usteer logs AP-side: 11v transition **accepted** (good) vs **ignored → deauth** (client
  didn't honor the hint — expected for the ESP8266/arduino devices).
- Watch for regressions: no increase in disconnect/reconnect churn on the 7 changed devices
  (compare `wifi-signal-sensors` / uptime before/after).

---

## 8. Open questions for Dimitri
1. Package (Option A) or inline (Option B) for the 7 devices?
2. Also set `power_save_mode: none` on the 6 BT proxies + diesel-heater?
3. Do you want me to attempt the esp-idf migration for any of the 5 arduino-ESP32 devices
   (incl. the barcode scanners), or defer all of them?
4. Sequence vs Plan 05 (remote packages): land the new `wifi-roaming-11kv.yaml` as a local
   package now, or author it directly in the remote-packages layout?

---

### Sources
- [ESPHome WiFi component](https://esphome.io/components/wifi.html)
- [ESPHome PR #3600 — support 802.11k and 802.11v](https://github.com/esphome/esphome/pull/3600)
- [ESPHome issue #4725 — can't explicitly disable BTM/RRM](https://github.com/esphome/issues/issues/4725)
