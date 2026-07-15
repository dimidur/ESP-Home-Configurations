# Plan 04 — CapiBridge gateway: WiFi drop / never-rejoins fix

**Status:** planning only — no code yet
**Execution order:** **4th** (after Plan 03 alerts). Related roaming add-on tracked from Plan 01 §5.
**Scope:** `../CapiBridge` (sibling clone), gateway firmware `code/Gateway/ESP1/ESP1.ino`
**Owner:** Dimitri
**Date:** 2026-07-14

---

## 0. TL;DR / decision

- **Is the bug fixed in the latest upstream release? → No.**
  The local clone is at `ea93085`; upstream `main` has advanced to `9c0a3b1`, but the
  entire delta `ea93085..9c0a3b1` is **README / WebUI / info.md documentation only** —
  there are **zero firmware or WiFi-logic changes**. The reconnect defect is still present
  on upstream `main`.
- Therefore we take **both** tracks: (A) fix it ourselves on top of latest upstream and
  reflash, and (B) contribute the fix back as an upstream PR. Both need a **fork first** and a
  valid GitHub token — see **Phase 0** (§4).
- **Important reality check:** `../CapiBridge` is **not** a personal fork with your config
  applied. `origin` points straight at `PricelessToolkit/CapiBridge`, the working tree is
  **clean**, and `config.h` is the upstream template with **blank** `WIFI_SSID` /
  `WIFI_PASSWORD` / `MQTT_SERVER`.
- **Config LOCATED** (2026-07-14): not in the Linux clone, but on the **Windows host at
  `H:\source\repos\CapiBridge`** (WSL `/mnt/h/source/repos/CapiBridge`), on branch
  **`my-configuration`** (currently checked out there). Its `config.h` has all real values
  populated: `GATEWAY_KEY`, `WIFI_SSID/PASSWORD`, `MQTT_USERNAME/PASSWORD/SERVER`.
- **Hardware = SX1276, not SX1262.** That `config.h` selects `LORA_MODULE_SX1276` @ `BAND 868.0`
  (pre-Aug-2025 module). The upstream template defaults to SX1262 — **flashing the default would
  brick RX**. This answers §5 Q2: use **SX1276**.
- **⚠ Secrets are committed on `my-configuration`.** `config.h` (with real WiFi/MQTT creds) is
  tracked on that branch and `origin` there still points at **upstream** (not a fork). So: **never
  push `my-configuration` to a public fork**, and gitignore `config.h` before any push (Phase 0 / §4).

---

## 1. Current state (verified)

| Fact | Evidence |
|------|----------|
| Remote is upstream, not a fork | `git remote -v` → `origin  https://github.com/PricelessToolkit/CapiBridge.git` |
| Working tree clean, no local commits | `git status` clean; `git log origin/main..HEAD` empty |
| Local HEAD | `ea93085` ("Update README.md") |
| Upstream HEAD | `9c0a3b1` ("Fix YouTube link and update README content", 2026-04-18) |
| Delta is docs-only | `ea93085..9c0a3b1` = README/WebUI/info.md commits, no `.ino`/`.h` firmware change |
| Config not committed | `config.h` has `WIFI_SSID ""`, `WIFI_PASSWORD ""`, `MQTT_SERVER ""` |
| License | MIT (software) — clean for a PR. Hardware is CC BY-NC 4.0 (irrelevant to this change) |
| Upstream activity | 2 issues total (none about reconnect), **1 PR ever** (#23, closed unmerged) — maintainer is low-throughput on external contributions |

The gateway is a **2-ESP design**: `ESP1.ino` is the WiFi/MQTT/LoRa gateway (the one with
the bug); `ESP2.ino` is the ESP-NOW receiver over a serial link (no WiFi, unaffected).

---

## 2. Root-cause analysis of "disconnects and never rejoins"

All references are `code/Gateway/ESP1/ESP1.ino`.

### 2.1 Primary cause — blocking MQTT reconnect traps the whole loop
`reconnect()` (lines 101–130) is a **blocking** `while (!client.connected()) { … }` loop.
In `loop()` it is called unconditionally whenever MQTT is down:

```
675  if (!client.connected()) {
676    reconnect();
677  }
```

When WiFi drops, MQTT is also down, so `loop()` enters `reconnect()` and **spins forever**
attempting `client.connect()` (each failed attempt `delay(1000)`). While trapped there:
- the WiFi-recovery branch (lines 667–673) **never runs again**,
- `client.loop()`, LoRa RX (`radio`), and `SendLoRaCommands()`/`SendESPNOWCommands()` all
  **starve**.

This is *the* "never rejoins" mechanism: once WiFi is lost, the firmware is stuck trying MQTT
over a dead network and never gets back to re-establishing WiFi.

### 2.2 `WiFi.reconnect()` alone is unreliable on ESP32
```
667  if (WiFi.status() != WL_CONNECTED) {
670      WiFi.reconnect();
```
After an AP deauth / router reboot / roam, ESP32's `WiFi.reconnect()` frequently fails to
re-associate. The robust recovery is a full cycle:
`WiFi.disconnect(true, true); WiFi.mode(WIFI_STA); WiFi.begin(ssid, pass);`

### 2.3 Missing hardening flags
`setup_wifi()` (lines 65–85) never sets:
- `WiFi.persistent(false)` — avoids flash wear + stale-config reconnect quirks,
- `WiFi.setAutoReconnect(true)` — lets the SDK auto-retry association,
- `WiFi.setSleep(false)` — **modem sleep** (default) makes an always-on gateway miss beacons
  and drop on marginal RSSI (issue #5 reports "WiFi issues at home"),
- no `WiFi.onEvent()` handler for `ARDUINO_EVENT_WIFI_STA_DISCONNECTED` to drive clean recovery.

### 2.4 No last-resort reboot / no watchdog
There is no "offline for > N minutes → `ESP.restart()`" fallback and no task/hardware
watchdog. For an unattended field gateway this is the single most reliable safety net.

### 2.5 Boot-time trap (secondary)
`setup()` calls the blocking `reconnect()` (line 571) **before** the LoRa radio is initialised
(line 585). If MQTT is unreachable at boot, the radio never comes up and no LoRa/ESP-NOW
packets are received either.

---

## 3. Fix design (non-blocking connectivity state machine)

Guiding principles: **minimal, dependency-free, upstream-friendly** (no new libs, keep the
Arduino `.ino` style, no wholesale reformat so the diff stays reviewable).

1. **`setup()` / `setup_wifi()`**
   - `WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setSleep(false);
     WiFi.setAutoReconnect(true);`
   - register a `WiFi.onEvent()` handler for connect/disconnect logging + recovery flag,
   - `WiFi.begin(...)` **without** a blocking wait (or a short bounded wait only),
   - move LoRa radio init so it does **not** depend on MQTT being up (init radio before the
     first MQTT attempt).

2. **Make MQTT `reconnect()` non-blocking**
   - one `client.connect()` attempt per call, guarded by `if (WiFi.status()==WL_CONNECTED)`,
   - a backoff timer (e.g. retry every 2–5 s) instead of the inner `while`+`delay`.

3. **Staged WiFi recovery in `loop()`**
   - if `WiFi.status()!=WL_CONNECTED`: on a timer, escalate
     `WiFi.reconnect()` → full `disconnect(true,true)+begin()`,
   - only attempt MQTT when WiFi is up,
   - track `lastOnlineMs`; if offline continuously > **N minutes** (e.g. 10), `ESP.restart()`.

4. **Optional watchdog** — `esp_task_wdt` armed in `setup()`, fed in `loop()`; reboots on hang.

5. **Optional roaming add-on (from Plan 01 §5)** — while we're already rewriting the WiFi
   bring-up, this is the natural place to add 802.11k/v client support: after a clean
   `WiFi.begin()`, call `esp_rrm_send_neighbor_rep_request()` (11k) and
   `esp_wnm_send_bss_transition_mgmt_query(REASON_FRAME_LOW_RSSI)` (11v), and use
   `esp_wifi_set_rssi_threshold()` to trigger a roam scan on weak signal. Arduino-ESP32 exposes
   these IDF symbols directly. Keep it behind a compile flag so it doesn't complicate the upstream
   reconnect PR (ship the reconnect fix first, roaming as a follow-up commit/PR).

The result: `loop()` never blocks, LoRa RX keeps running even while WiFi is down, and the
gateway deterministically recovers or reboots.

---

## 4. Execution plan

### Phase 0 — rotate GitHub keys FIRST (prerequisite, blocking) — **STARTING HERE**

Status checked 2026-07-14:
- ✅ **`gh` CLI writes are already good.** `gh auth status` → logged in as **`dimidur`** (active)
  with scopes `gist, read:org, repo, workflow` (an OAuth token, not the expiring PAT). `repo` +
  `workflow` are sufficient for fork + push + PR — **no rotation needed for the CapiBridge work**.
- ⏳ **The expiring key is the read-only GitHub _MCP_ PAT** — the fine-grained token at
  **`~/.config/github-mcp/token`** (0600, single line), read by
  `~/.local/bin/github-mcp-launch`. Per global config it **expires 2026-07-20** (6 days out).
  This is what gives *Claude* GitHub read access; if it lapses, MCP browsing breaks (writes via
  `gh` are unaffected).

**Rotation steps (Dimitri does the web part):**
1. Create a new **fine-grained PAT** at <https://github.com/settings/personal-access-tokens> with
   the same read-only scopes as the current one and a fresh expiry.
2. Write it to `~/.config/github-mcp/token` as a **single line, no trailing newline**, keep perms
   `600`: `printf '%s' '<NEW_PAT>' > ~/.config/github-mcp/token && chmod 600 ~/.config/github-mcp/token`.
3. Restart the MCP (new Claude session / reconnect) so the launcher re-reads the token.
4. Update the expiry reminder in the global `CLAUDE.md` to the new date.

**Full credential-expiry inventory (verified 2026-07-14):**

| Credential | Expires | Impact if lapsed | Action |
|---|---|---|---|
| GitHub MCP PAT `~/.config/github-mcp/token` | **2026-07-20** | Claude loses GitHub MCP read access | rotate (steps above) |
| `gh` OAuth token (`gho_…`) | **never** (no expiry header) | — | none |
| GPG primary `[SC]` `647DC7148E1CAE1D90B03661B392301217C8C34D` | **2026-08-16** | **git commit signing breaks in every repo** | extend (below) |
| GPG `[E]` subkey `0AA85C4B0E72CF8DB0D621B7ABA80D562B48B204` | **2026-08-16** | **`pass` can't decrypt → pass-backed `sudo` breaks on all hosts** | extend (below) |

**GPG extension** (pops Kleopatra pinentry; from the tracked Grocy task) — higher blast radius
than the PAT, so do it in the same Phase 0 pass:
```
gpg --quick-set-expire 647DC7148E1CAE1D90B03661B392301217C8C34D 2y
gpg --quick-set-expire 647DC7148E1CAE1D90B03661B392301217C8C34D 2y 0AA85C4B0E72CF8DB0D621B7ABA80D562B48B204
```
After extending, run **`/sudo-health`** to confirm `pass` still decrypts. Full guide:
`homelab-ansible/docs/agent-sudo-setup.md`.

- **Then start Track B** — fork access is already confirmed via `gh` (step 1 of Track A).

### Track A — fork, fix locally + reflash
1. **Make it a fork first** (Dimitri): `gh repo fork PricelessToolkit/CapiBridge --remote` so this
   clone's `origin` → `dimidur/CapiBridge` and `upstream` → `PricelessToolkit/CapiBridge`.
2. **Resolve config first** (see Open Questions) — obtain your real `WIFI_SSID`,
   `WIFI_PASSWORD`, `MQTT_*`, `GATEWAY_KEY`, encryption key, LoRa band/module. If the missing
   config branch is found on another machine, fetch it into the fork.
3. Sync latest upstream: `git fetch upstream && git merge upstream/main` (trivial fast-forward —
   no local divergence).
4. Create branch `fix/wifi-reconnect-robustness`.
5. Apply the §3 firmware changes to `ESP1.ino` (and `setup_wifi`/`reconnect` helpers only).
6. Populate `config.h` with real values **locally, uncommitted** (it's the upstream template;
   keep secrets out of any commit — mirror the concern raised in upstream PR #23; `config.h`
   should be gitignored in the fork).
7. Flash ESP1, leave ESP2 as-is.
8. Validate (§6).

### Track B — upstream PR
1. Fork already exists from Track A step 1 (`dimidur/CapiBridge`).
2. Push the **firmware-only** branch `fix/wifi-reconnect-robustness` (no `config.h`, no
   secrets).
3. Open an **issue** first: clear repro ("gateway drops WiFi after AP reboot / marginal RSSI
   and never rejoins; loop trapped in blocking MQTT reconnect"), then a PR that references it.
4. Follow upstream norms: no `CONTRIBUTING.md` and no PR template exist, so a standard
   fork→PR is expected. Keep scope tight, keep the existing code style, describe the failure
   mode + fix + how you tested. Set expectations: maintainer has merged 0 external PRs so far,
   so local Track A is the one we rely on regardless of PR outcome.

---

## 5. Open questions (need Dimitri's input before Track A step 4/5)

1. ✅ **RESOLVED — config located.** `H:\source\repos\CapiBridge`, branch `my-configuration`,
   real values present. Decide how to carry it: cleanest is to (a) fork, (b) apply the WiFi fix on
   a `fix/wifi-reconnect-robustness` branch, (c) keep `config.h` **gitignored/local** with your
   values copied from `my-configuration` — do **not** merge the secret-bearing `my-configuration`
   branch into anything you push.
2. ✅ **RESOLVED — hardware is `SX1276` @ `868.0`** (from the located `config.h`). Set
   `LORA_MODULE_SX1276` before flashing; the SX1262 default would brick RX.
3. **Reboot fallback acceptable?** OK to add "offline > 10 min → `ESP.restart()`"? Any reason
   a hard reboot is undesirable (e.g. in-flight LoRa command loss)?
4. **Watchdog** — include the `esp_task_wdt` hardware watchdog, or keep the change purely to
   WiFi/MQTT logic for a smaller upstream diff?

## 6. Validation / repro

- **Repro:** kick the client off the AP (router "disconnect device" or reboot the router),
  or move to marginal RSSI. Current firmware: gateway goes dark and stays dark until power
  cycle.
- **Verify fix:** serial monitor shows staged recovery; MQTT LWT (`CAPIBRIDGE_LWT_TOPIC`)
  flips `offline`→`online` automatically after the AP returns; LoRa packets continue to be
  received during the WiFi outage.
- **Soak:** run 24–48 h with periodic forced deauths; confirm auto-recovery every time and
  that `ESP.restart()` fallback only triggers on genuine prolonged outages.
