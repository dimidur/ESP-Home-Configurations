# Future plans — execution order

Plans are numbered in the **agreed execution order** (Dimitri, 2026-07-14): cheapest / highest
immediate benefit first, biggest structural change last. Do them **one at a time**, top to bottom.

| # | Plan | Why here | Touches |
|---|---|---|---|
| 01 | [WiFi roaming 802.11k/v](./01-wifi-roaming-80211kvr.md) | Cheap, immediate benefit; 7 esp-idf devices | `packages/`, 7 configs |
| 02 | [ESPHome upgrade + IoT firewall allowlist](./02-esphome-updates-and-iot-firewall-allowlist.md) | Bump fleet to latest; fix the BT-proxy "failed to fetch template" egress error | all configs, router firewall |
| 03 | [Info-screen additional alerts](./03-info-screen-additional-alerts.md) | Per-group soil + per-chore Grocy alerts | `lilygos3-info-screen.yaml`, HA templates |
| 04 | [CapiBridge WiFi reconnect fix](./04-capibridge-wifi-reconnect-fix.md) | Firmware fix + upstream PR; needs GitHub key rotation (Phase 0) first | `../CapiBridge` |
| 05 | [Extract configs → remote packages](./05-extract-configs-remote-packages.md) | Public/private split; do last, migrate the stabilized set once | whole repo + new private repo |

Related earlier note: [ble-proxy-composable-package.md](./ble-proxy-composable-package.md) —
orthogonal BLE-composability refactor; folds into Plan 05's packaging.

**Cross-plan threads**
- Plans 01, 02, 05 all edit each device's `packages:`/`wifi:` block → 05 is the carrier that lifts
  the stabilized result into the remote layout (migrate once).
- CapiBridge gets both a WiFi-reconnect fix (Plan 04) and an optional 11k/v roaming add-on
  (Plan 01 §5 → Plan 04 §3).
- **Prerequisite for Plan 04:** rotate GitHub keys/PAT (the read-only MCP PAT expires **2026-07-20**).
