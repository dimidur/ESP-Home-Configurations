# Plan 05 — Split reusable packages from private configs; consume via remote references

**Status:** planning only — no code yet
**Execution order:** **5th / last** (biggest structural change; do after 01–04 have stabilized the
configs so we're not chasing a moving target)
**Scope:** whole repo (public `dimidur/ESP-Home-Configurations`) + a new private repo
**Owner:** Dimitri
**Date:** 2026-07-14

---

## 0. TL;DR

The repo is public. The **reusable layer is already remote-ready** — `packages/` and
`components/` are 100 % secret-free and substitution-driven (`grep -rn '!secret' packages/
components/` → nothing). The work is mostly **organizational**:

1. **Split into two repos:** keep a **public packages repo** (`components/`, `packages/`,
   `docs/`, examples) and create a **private configs repo** for the ~21 personal device
   `*.yaml` + `secrets.yaml`.
2. **Rewrite each device config** to consume the public repo via remote `packages:` and remote
   `external_components:` (`github://dimidur/ESP-Home-Configurations/...@<tag>`) instead of
   local `!include` / `type: local` paths.
3. **Pin a version tag** so builds are reproducible.

ESPHome's rule *"remote packages cannot have `secret` lookups"* is **already satisfied** — no
package rewrite needed for it. Relates to but does not block
[`ble-proxy-composable-package.md`](./ble-proxy-composable-package.md) (treat as orthogonal;
inherit its "thin, no-top-level-keys, substitution-only" principle).

---

## 1. Why this matters (what leaks today)

Credentials do **not** leak — `secrets.yaml` is git-ignored (`.gitignore` `/secrets.yaml`) and
untracked. But the public repo still publishes **home topology**: the device inventory
(every `bluetooth-proxy-*`, `diesel-heater`, `garden-watering-controller`, …), room names, and
installation-specific non-secret constants (MQTT topic prefixes, the diesel-heater RF crystal
offset, OBIS meter map/limits). The goal is to stop publishing *your specific installation*
while still sharing the reusable building blocks.

---

## 2. Current layout: reusable vs personal (verified)

### Reusable (stays public)
- **Custom C++ components** — `components/{uart_line_reader,deduplicate_text,qrcode2_uart,diesel_heater_rf}/`
- **Infra packages** — `packages/{wifi,wifi-signal-sensors,wifi-diagnostic-sensors,mqtt,mqtt-diagnostic-sensors,factory-reset-button,internal-temp-sensor-esp32}.yaml`
- **Device-role packages** — `packages/device-configs/*.yaml` (+ `examples/`, `.md` docs)
- **`docs/`**

### Personal (moves to private repo) — 21 top-level device configs
`3d-printer-socket`, `air-quality-sensor-1`, `biqu-air-filter-socket`,
`bluetooth-proxy-{bedroom,buro,buro-meraner-2,kitchen,living-room,wr}`, `compressor`,
`compressor-valve`, `diesel-heater`, `garden-watering-controller`, `gaszahler`,
`lilygos3-info-screen`, `minimal-ota`, `qrcode2-atom-lite-{1,2}`, `smart-electric-meter`,
`soil-sensor-tuyas`, `wasserstand-regenreservoir` + `secrets.yaml`.

**Architectural fact that makes this easy:** secrets enter only at the top of each device
config, are mapped into `substitutions:`, and packages consume `${var}` (e.g. `packages/wifi.yaml:3`
`ssid: ${wifi_ssid}`). That's exactly the contract ESPHome remote packages require.

---

## 3. Current reference mechanisms (what changes)

Two local mechanisms are in use:
- `packages: { name: !include packages/....yaml }`
- `external_components: - source: { type: local, path: components }`

`lilygos3-info-screen.yaml:141` **already** consumes a remote component
(`github://landonr/lilygo-tdisplays3-esphome`) — proof the remote path works here.

### Remote target syntax (verified against esphome.io)
Per-file shorthand:
```yaml
packages:
  wifi: github://dimidur/ESP-Home-Configurations/packages/wifi.yaml@v1.0.0
  bt_proxy: github://dimidur/ESP-Home-Configurations/packages/device-configs/bluetooth-proxy.yaml@v1.0.0
```
Block form (one repo, many files, one ref — DRYer, preferred):
```yaml
packages:
  shared:
    url: https://github.com/dimidur/ESP-Home-Configurations
    ref: v1.0.0
    refresh: 1d
    files:
      - packages/wifi.yaml
      - packages/wifi-signal-sensors.yaml
      - packages/device-configs/electricity-meter.yaml
```
Remote components (default `components/` path matches this repo — no `path:` override needed):
```yaml
external_components:
  - source: github://dimidur/ESP-Home-Configurations@v1.0.0
    components: [qrcode2_uart]     # or [uart_line_reader, deduplicate_text] for the meter
    refresh: 1d
```

---

## 4. Hard decisions

**A. Repo topology — recommend two repos.** Public packages repo (keep this repo, or rename to
`esphome-packages`) + a **new private** configs repo. Keeping one repo gains nothing (remote-
consuming yourself) and still publishes the device inventory. → **Decision needed: rename or keep
name; new private repo name.**

**B. Secrets across the boundary — already solved.** Global `substitutions:` merge into remote
packages exactly as for local includes, so `!secret → substitutions → ${var}` keeps every secret
inside the private repo. `secrets.yaml` can live in-tree in the *private* repo (or stay ignored).

**C. Versioning/pinning.** Pin device configs to a **git tag** (`@v1.0.0`) for reproducibility;
bump deliberately. `refresh:` only controls cache re-fetch for a *moving* ref and is irrelevant
for a pinned tag. → **Decision: tag-pinning (recommended) vs `@main`+refresh.**

**D. Nested relative includes inside role-packages** — `packages/device-configs/bluetooth-proxy.yaml:30-32`
and `soil-sensor-d1-mini.yaml:38-39` do `!include ../wifi.yaml` etc. When pulled remotely,
ESPHome caches the repo tree and *should* resolve these — **but this must be proven with a real
`esphome compile`** after migration. Safer alternative: **flatten** — have device configs include
the leaf packages directly rather than relying on a role-package's internal `../` includes.
→ **Decision: verify nested-include-over-remote, or flatten.**

**E. Component coupling** — `packages/device-configs/electricity-meter.yaml` needs
`uart_line_reader` + `deduplicate_text` but its own `external_components` is commented out; it
relies on the consumer (`smart-electric-meter.yaml:105-108`) to supply them. After the split the
consumer must supply them via the remote `external_components` source.

**F. CI/auth** — public packages repo needs no auth to consume. If the private configs repo ever
builds in CI it needs its own `secrets.yaml`; if the packages repo were made private, add
`username`/`password` (token via `!secret`) to the remote source.

---

## 5. Full inventory of references to migrate

| File | local `!include packages/...` | local `external_components` |
|---|---|---|
| 3d-printer-socket.yaml | 33-36 | — |
| air-quality-sensor-1.yaml | 21-23 | — |
| biqu-air-filter-socket.yaml | 33-36 | — |
| bluetooth-proxy-bedroom.yaml | 19 | — |
| bluetooth-proxy-buro-meraner-2.yaml | 19 | — |
| bluetooth-proxy-buro.yaml | 19-20 | 22-25 |
| bluetooth-proxy-kitchen.yaml | 19 | — |
| bluetooth-proxy-living-room.yaml | 18 | — |
| bluetooth-proxy-wr.yaml | 20-21 | — |
| compressor.yaml | 38-41 | — |
| compressor-valve.yaml | 38-41 | — |
| diesel-heater.yaml | 72-76 | 38-41 |
| garden-watering-controller.yaml | 34-36 | — |
| gaszahler.yaml | 51-52 | — |
| lilygos3-info-screen.yaml | 195-197 | 143-145 (keep remote `landonr` at 141) |
| minimal-ota.yaml | 54 | — |
| qrcode2-atom-lite-1.yaml | 23-27 | 16-20 |
| qrcode2-atom-lite-2.yaml | 23-27 | 16-20 |
| smart-electric-meter.yaml | 127-133 | 105-108 |
| soil-sensor-tuyas.yaml | 19 | — |
| wasserstand-regenreservoir.yaml | 33-34 | — |

`examples/*` keep local `!include ../*.yaml` — they live inside the public repo to demo local usage.

---

## 6. Execution phases

1. **Prep (public repo):** optionally add `substitutions:` default blocks to packages for
   standalone-testability (hardening, non-blocking). Tag `v1.0.0`.
2. **Create private repo:** move the 21 device configs + `secrets.yaml`; set up `.gitignore`,
   `.esphome/`, dashboard.
3. **Rewrite one device end-to-end** (recommend `soil-sensor-tuyas` — single include, simplest)
   → remote refs → `esphome compile` → confirm identical build. This proves the whole mechanism
   incl. the nested-`../`-include question (decision D) on a real target.
4. **Roll out** the remaining 20 configs (block-form `packages:` per repo).
5. **Verify** each compiles and flashes; then remove the personal configs from the public repo
   history if desired (note: rewriting public history is disruptive — likely just delete going
   forward and accept they exist in old history, or do a filtered history purge — **decide**).
6. **Coordinate ordering** with the other plans: this touches every device's `packages:` block —
   the same block Plan 01 (roaming) and Plan 02 (updates/firewall) also edit. Per Dimitri's chosen
   sequence (01 → 02 → 03 → 04 → this), the roaming package (`wifi-roaming-11kv.yaml`) and the
   version bumps land **first as local edits**, and **this plan is the carrier that lifts the whole
   stabilized set into the remote-packages layout** — so we migrate once, not against a moving target.

---

## 7. Open questions for Dimitri
1. Rename the public repo (`esphome-packages`?) or keep `ESP-Home-Configurations`? New private
   repo name?
2. Tag-pinning vs `@main`+refresh?
3. Flatten role-package `../` includes, or verify remote nested includes work as-is?
4. Purge the personal configs from public git **history**, or only stop tracking them going
   forward?
5. Will the private repo build in CI, or only locally? (Determines secrets/auth setup.)
