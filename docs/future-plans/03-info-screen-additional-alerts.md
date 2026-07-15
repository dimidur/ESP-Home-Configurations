# Plan 03 — Info screen: additional alerts (Grocy chores + sensor thresholds)

**Status:** planning only — no code yet
**Execution order:** **3rd** (after Plan 02 upgrade)
**Scope:** `lilygos3-info-screen.yaml` (LilyGo T-Display-S3, 2008 lines) + small HA-side template
sensors for per-chore alerts
**Owner:** Dimitri
**Date:** 2026-07-14

---

## 0. TL;DR

The screen already has a clean, extensible **alarm-icon grid** (8 slots, 3 used today → 5 free).
New alerts are added as **grid renderers** — the supported extension point. Per Dimitri, alerts
are **discrete: one slot per sensor group and one slot per chore**, each with its own icon,
defined incrementally:
- **Soil groups** — (A) Thuja Front `< 70 %` (conifer icon), (B) Buro plants (threshold TBD, pot
  icon), (C) other sensors TBD (tree icon).
- **Grocy chores** — separate per-chore alerts; Dimitri supplies each chore **id + icon** as we go.

**Entity ids verified against live HA** (§3). The key design decision is **how to get a per-chore
boolean** (Grocy exposes chores as a JSON array in an attribute) — recommended: small HA-side
template sensors (§3.2). **Slot budget** (8 active max) becomes the binding constraint once several
chores are added (§4).

---

## 1. How the alert system works today (verified, `lilygos3-info-screen.yaml`)

There are **two** alert families — extend the first, not the second:

### A) Alarm-icon grid — the extension point
- **Layout helper** `get_alarm_icon_position` (L742–756): 4-per-row × 2-row grid, origin
  `(142, 40)`, 48 px pitch, 40×40 icons → **8 slots** (`alarm_num` 0–7).
- **Each alert = a lambda** `bool(display::Display&, int alarm_num)`. Returns `true` when
  active (drew an icon), `false` otherwise. Uses `static` locals to cache last value/color.
  Existing: `render_rain_index_alert` (758), `render_heat_alarm` (800), `render_wind_alert`
  (845), `render_rain_rate_alert` (873), `render_lightning_alert` (901).
- **Dispatcher** `render_alarms` (L938–951): iterates a `std::vector` of renderers **in
  priority order**, incrementing `alarm_num` only when one draws → active alerts pack
  left-to-right with no gaps. Registered in `render_main_content` (L1006), captured at L973.
- **Sub-alert compositor** `render_rain_alarm` (L925–936): rain-rate → lightning → rain-index
  share **one** slot (first match wins). Template for mutually-exclusive alerts in a single slot.

### B) Waste-collection blink boxes (separate, hardcoded)
`render_hausmull/biomull/papier/gelbe_tonne` (L605–671): fixed bins, `daysTo <= 1` → red
border + evening-only blink driven by global `waste_sensor_alert_status` toggled in the 5 s
`battery_update` interval (L1978–1992). **Only** use this pattern if you specifically want a
blinking box; the grid (A) is cleaner.

### Current grid alerts & thresholds (3 slots of 8 used)

| Priority | Renderer | Source | Active when |
|---|---|---|---|
| 1 | `render_heat_alarm` | `outdoor_temperature` | `< 0` or `> 27 °C` |
| 2 | `render_wind_alert` | `wind_beaufort` | `>= 4` Bft |
| 3 | `render_rain_alarm` (rate→lightning→index) | rain/lightning | rate `> 0`, lightning `< 18 km`, index `>= 1.2` |

---

## 2. The recipe to add one alert (grid pattern)

1. **Icon** — add to `image:` block (L1012–1122), `type: grayscale`, `transparency:
   alpha_channel`, `resize: 40x40`, `file: mdi:<name>` (MDI fetched at build time — any glyph
   is available by adding an entry).
2. **HA source** — add a `platform: homeassistant` sensor (`internal: true`) with an
   `on_value:` that sets `id(screen_is_out_of_sync) = true;` (**mandatory** — redraws are
   demand-driven, L1930–2008) plus throttle/delta filters. Mirror `room_humidity` (L1522–1530).
3. **Renderer** — write `render_<x>_alert` `bool(display::Display&, int)` with the static-cache
   pattern; `auto pos = get_alarm_icon_position(alarm_num);`
   `it.image(pos.first, pos.second, id(icon), color, COLOR_OFF); return active;`
4. **Register** — capture it in `render_alarms` (L938) and insert into the `alarm_renderers`
   vector at the chosen priority.

---

## 3. The requested alerts — **discrete per-group / per-chore** (Dimitri)

Design intent (updated): **not** one generalized alert, but **separate alert slots per sensor
group and per chore**, each with its own icon. Groups/chores/thresholds get defined incrementally
("as we go"). Each becomes its own grid renderer.

### 3.1 Soil-sensor groups — one alert per group, distinct icon

| Group | Sensors | Threshold | Icon (MDI) | Status |
|---|---|---|---|---|
| **A · Thuja Front** | `sensor.thuja_front_soil_moisture` | **< 70 %** | conifer — `mdi:pine-tree` (or `mdi:tree`) | defined |
| **B · Buro plants** | buro soil sensors — `sensor.yucca_buro_soil_moisture` (+ others in buro TBD) | TBD ("as we go") | flower pot — `mdi:flower` / `mdi:pot` / `mdi:flower-pot-outline` | threshold pending |
| **C · Other sensors** | TBD (Dimitri will name them) | TBD | tree — `mdi:tree` / `mdi:forest` | pending |

Notes:
- **Thuja Front** = `sensor.thuja_front_soil_moisture` (`%`, `device_class: moisture`, a
  Plant-integration wrapper over raw `sensor.tujasfrontsensor_moisture`). This is the "Tujas front"
  meant. *(Not the ESPHome device `sensor.soil_sensor_tuyas_soil_moisture` — that's the separate
  d1-mini unit.)*
- Available plant moisture sensors to draw groups from: `thuja_front`, `yucca_buro`, `dracaena`,
  `small_dracaena`, `amelanchier_wr`, `soil_sensor_large_blue_pot` — each with a
  `number.*_min_soil_moisture` helper (usable as a per-sensor dynamic threshold instead of a
  hardcoded number, if you prefer).
- **Per group:** one substitution list of entity ids + one threshold + one icon → one
  `render_<group>_soil_alert` lambda active when **any** sensor in the group is below the
  threshold (optionally print the count of dry sensors in the group). Each group = one grid slot.
- Which sensors are "in buro": `yucca_buro` is explicit; confirm the rest as you define group B.

### 3.2 Grocy chores — one alert per chore, per-chore icon

Dimitri wants **separate alerts for individual chores** (will provide **chore IDs + the icon each
must use**, incrementally). The Grocy data shape makes this the key design question:

- `sensor.grocy_chores` — state = count of chores due (`6`), attribute `chores[]` (each with
  `id`, `name`, `next_estimated_execution_time`, …).
- `binary_sensor.grocy_overdue_chores` — state `on`/`off`, attribute `count` (`3`) +
  `overdue_chores[]` array (each entry has the chore `id`/`name`).

**Problem:** these are **JSON arrays inside an attribute** — ESPHome's `homeassistant` platform
cannot filter an array by chore `id` natively. Two ways to get a clean per-chore boolean:

- **Option 1 (recommended) — HA-side template sensors.** Define, per tracked chore, an HA
  template `binary_sensor` (e.g. `binary_sensor.chore_<id>_overdue`) that is `on` when that chore
  id is in `overdue_chores`. The info screen then imports each as a simple `binary_sensor`
  (`internal: true`, `on_state → screen_is_out_of_sync`) and renders its own icon. Clean, robust,
  and the per-chore logic stays in HA where it's easy to edit. *(Requires adding template sensors
  to the HA config — a small change in the `hass-config` repo, outside this device.)*
- **Option 2 — attribute string-match on-device.** Import `overdue_chores` (or the chores array)
  as a **`text_sensor`** and, in each chore's render lambda, substring-match the chore id/name in
  the stringified attribute. No HA change, but brittle (depends on the serialized format) and
  harder to maintain. Use only if you want zero HA-side changes.

Each chore alert = one grid slot with its provided icon; active when its boolean is `on`.

---

## 4. Constraints (must respect in the plan)

- **Capacity — now the binding constraint.** The grid is **8 slots (4×2)**; **3 are used** by
  weather → **5 free**. The new discrete design wants **3 soil groups + N per-chore alerts**, so
  with more than ~2 chores you exceed 8. Options when you run out: (a) prioritize (the dispatcher
  packs active alerts left-to-right, so inactive ones cost no slot — 8 *simultaneously active* is
  the real limit); (b) expand the grid (a 3rd row / smaller icons — edit `get_alarm_icon_position`
  L742–756); (c) collapse a group into a single shared slot with internal priority (the
  `render_rain_alarm` compositor pattern, L925). Row 2 already sits at y=88 (icon bottom ≈128 vs
  ~170 px height) — a 3rd row is tight against the bottom waste/date row, so measure before adding.
  No x-collision with the CO2/VOC/PPM icons (x<122; grid starts x=142).
- **Fonts are glyph-restricted** (L1124–1138). Digits `0-9`, `%`, `.`, `°`, `-`, `/` are
  available → **numeric counts/percentages are fine**; **arbitrary words are NOT** (missing
  glyphs render blank). Keep alerts to **icon + short number**, no text labels unless you
  extend the `glyphs:` list.
- **No truncation/scrolling** helper exists — all text is short `printf` numerics.
- **Redraw is demand-driven** — every new sensor's `on_value` **must** set
  `screen_is_out_of_sync = true` or the change won't show.
- **Deep sleep:** device sleeps on absence/night; alerts render only while awake, and there is
  **no push/notify** — this is a display-only surface. An overdue chore is seen at next wake.
- **Blinking** needs its own `bool` global + interval toggle (waste pattern L1985–1990); prefer
  a static icon unless blink is explicitly wanted.

---

## 5. Alert backlog (per-group / per-chore, filled incrementally)

| # | Alert | Source | Active when | Icon | Status |
|---|---|---|---|---|---|
| A | Soil — **Thuja Front** | `sensor.thuja_front_soil_moisture` | `< 70 %` | `mdi:pine-tree` | ready to build |
| B | Soil — **Buro plants** | `sensor.yucca_buro_soil_moisture` (+ TBD) | TBD | `mdi:flower` / `mdi:pot` | threshold pending |
| C | Soil — **Other** | TBD (Dimitri) | TBD | `mdi:tree` | pending |
| D… | **Per chore** (one per) | `binary_sensor.chore_<id>_overdue` (HA template) or `overdue_chores` match | that chore overdue | per-chore icon (Dimitri) | IDs + icons pending |

**Intake format — what to send me per new alert:**
- Soil group: list of entity ids + threshold (or "use each sensor's `min_soil_moisture` helper") + icon.
- Chore: the Grocy **chore id** (from `sensor.grocy_chores` → `chores[].id`) + the **icon** it must use.

**Open questions for Dimitri:**
1. Chores: go with **HA-side template sensors** per chore (Option 1, recommended) or on-device
   attribute string-match (Option 2)? (Option 1 needs a small `hass-config` change.)
2. Soil group B — which exact sensors are "in buro" beyond `yucca_buro`, and what threshold?
3. Show a **numeric count** on group/chore icons where >1 (e.g. "2 dry", "3 overdue"), or icon-only?
4. Static icons, or **blink** the most urgent (needs a per-alert global + interval, §4)?
5. Slot budget: if active alerts can exceed 8, do you prefer a 3rd grid row, or grouping some into
   one shared slot with internal priority (§4)?
