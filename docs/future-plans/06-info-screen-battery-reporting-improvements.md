# Plan 06 — Info-screen: battery reporting trustworthiness (deferred items)

**Status:** documented, not implemented — items approved-in-principle 2026-07-28, deferred
**Scope:** `lilygos3-info-screen.yaml` (battery voltage/charge chain only)
**Owner:** Dimitri
**Date:** 2026-07-29

---

## 0. Context / what's already done

Analysis on 2026-07-28 (24 h of HA recorder data from `ha_db` on mini-server, matched
against the yaml pipeline) found four failure modes in battery reporting:

1. **Hours of `unknown` in HA** — every wake, ESPHome pushes "missing state" for the
   `update_interval: never` template sensors, wiping the last good value; wakes shorter
   than ~52 s never publish (recorder: `unknown` 00:57→07:59, 08:44→09:43, 15:24→16:24).
2. **Charging % flapping 20↔100 %** — the charging flag follows the *sign of ±25 mV rail
   noise* (`rate > -0.001`), flipping between the disjoint discharge [3.6–4.06] and
   charge [4.63–4.83] maps (recorder: `23→30→100→22→25→30→100→32→33→100` in 30 min).
3. **Device deep-slept while plugged in** (15:24→16:24) — direct consequence of (2): a
   noise dip published `is_charging=false`, sleep logic proceeded.
4. **Non-monotonic discharge %** — load-sag (±15 mV WiFi bursts) → `16.7→18.6`, `0→2.9→0`.

**Implemented already (fix #1, "seed-republish", 2026-07-29):** three `restore_value: true`
globals (`last_published_battery_voltage/_level/_is_charging`) captured on every publish and
re-published in `on_boot`, so HA sees the last known value instead of `unknown` on every
wake. Kills failure mode 1. NVS wear: negligible (log-structured, wear-leveled, ~2-3 small
entries per sleep cycle, same mechanism as the existing restored globals — centuries of
margin).

The items below are the **rest of the agreed set** — documented here for later.

### Hard constraints (from iteration history — do not violate)

- **Do not touch** `skip_initial`, the 4 s ADC cadence, or the sliding-window sizes: boot-time
  processing/radio activity makes early ADC readings garbage (±50 % jumps without them).
- **Do not change the calibration values** (`3.6/4.06` discharge, `4.63/4.83` charge). The
  4.06→4.63 gap is intentional and empirically real: on battery the reading never exceeds
  ~4.06 (even freshly charged); on charger the rail never reads below ~4.666 (observed even
  with a dead pack at plug-in).
- No extra wakes / radio time / measurable energy cost.

---

## 1. Spike-filter seeding (fix #3-lite) — small, do first

**Bug:** after every wake `last_battery_voltage` starts at `0.0`, so the **first windowed
value is always rejected** as a "spike" (`|3.7 − 0| ≥ 0.02`) — even though it is already
post-`skip_initial`, post-20-sample-average, i.e. from the stable period. Costs +20 s to
first publish on every wake, for nothing.

**Fix:** seed `last_battery_voltage` at boot from `last_published_battery_voltage` (the
Plan-06 §0 restored global). First windowed value then gets a *fair* spike comparison
against the previous session's value instead of a guaranteed-fail one. All filters stay.

Expected: first fresh publish ~32 s instead of ~52 s on stable wakes. Note: after a very
long sleep, genuine drift > 20 mV still rejects the first value once (by design; second
passes 20 s later) — acceptable, since the boot seed already prevents `unknown`.

## 2. Charging flag: latched hysteresis + dead-zone hold (fix #4) — the big one

**Replace** the rate-sign flag (and delete the dead code at the step-4 lambda,
lines ~1715–1721 — it compares a V/min rate against `4.63` **volts**; dimensionally
impossible, never fires) with a **latched two-threshold flag** exploiting the calibrated gap:

```
enter charging:  v >= 4.60   (on-charger rail never reads below ~4.666)
exit  charging:  v <= 4.40   (on-battery reading never exceeds ~4.06)
between:         keep previous flag (latch, stored in a restored global)
```

Flapping now requires a 200 mV swing; observed rail noise is ±25 mV. This kills failure
modes 2 **and** 3 (sleeping while plugged).

**Dead-zone hold:** if `4.06 < v < 4.60` (physically impossible except during the
plug/unplug ramp), **do not recompute %** — hold the last value. This preserves the two
behaviours the current values were tuned for, *by construction* instead of by filter-timing:
- no "100 %" flash for a minute after plugging in (today prevented only because the
  spike/rate filters happen to eat the ramp samples);
- no false "charging" on wake with a freshly charged battery (impossible: on battery
  v ≤ 4.06 < 4.60).

**Accepted tradeoff:** charging-indication latency after plug-in stays ~1–2 min (the 80 s
rolling window walks the transition in ~0.2 V steps and the spike filter rejects each step).
A big-delta bypass was considered and **withdrawn**: against a rolling average the steps
never exceed ~0.25 V, so a safe (>0.4 V) bypass rarely fires, and a lower threshold would
touch the tuned chain. Not worth it.

## 3. Monotonic gauge clamps (fix #5)

Classic fuel-gauge trick, applied at the `battery_level` template after §2:

- while **discharging**: published % may only **fall** (clamp new ≤ last-published);
- while **charging**: published % may only **rise**;
- **re-anchor** (allow a jump) whenever the §2 charging latch flips.

Uses only data already in hand; kills failure mode 4 (`16.7→18.6`, `0→2.9→0` wobble) and
the residual rail-noise wobble while charging (`22↔33 %`). Store the anchor in the existing
`last_published_battery_level` global — no new state needed.

## 4. Optional observation — low-battery protection

Recorder showed the pack ran **35 min below "0 %"** (3.60 V floor) and browned out at
~3.31 V → 3.5 h `unavailable` until manually plugged in. LiPo under load at 3.3 V is close
to damage territory. Option: a critical guard — at v < **3.45 V** (filtered, on battery),
publish 0 %, show an empty-battery glyph, and enter max-duration deep sleep immediately
instead of the normal schedule. Deliberately **not** changing the 3.6 V = 0 % calibration.
Low priority; decide when implementing §2/§3.

## 5. Implementation order & verification

1. §1 (one-line seed) → flash → confirm first fresh publish ~32 s after wake.
2. §2 (+ delete dead code) → flash → plug/unplug tests: no 100 %-flash, no flap while
   charging overnight, device does **not** sleep while plugged, fresh-full wake shows
   "discharging".
3. §3 → observe a full discharge day: % strictly monotonic ↓; a charge session: monotonic ↑.
4. §4 only if wanted.

Verification data source: `ha_db` recorder on mini-server (`mariadb` container, `states`
join `states_meta`), as used for the 2026-07-28 analysis.
