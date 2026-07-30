---
name: no-magic-numbers
description: Never hardcode magic numbers ANYWHERE — it is an antipattern; every tunable value goes into the substitutions block (or a named constant)
metadata: 
  node_type: memory
  type: feedback
  originSessionId: aaacfe53-d320-450d-a838-9ae22076e0a6
  modified: 2026-07-30T00:16:21.068Z
---

Never hardcode magic numbers anywhere — not in lambdas, not in YAML blocks, not
in scripts. It is an antipattern, period. In ESP-Home-Configurations every
tunable value (thresholds, timing windows, calibration bounds, sizes, limits)
must be a `substitutions:` entry referenced as `${name}`; in other code, a named
constant.

**Why:** The user tunes values iteratively without hunting through code; the
whole repo follows this convention (`battery_threshold_yellow`,
`humidity_upper_threshold_red`, `deepsleep_step_*`, …). Told explicitly, and
repeated 2026-07-30 after I hardcoded `moisture < 70` in the soil alert
(`soil_moisture_threshold_dry` was the fix); user clarified the rule is
universal, not lambda-specific.

**How to apply:** Before writing any numeric literal into config or code, ask
"would anyone ever tune this?" If yes (or unsure): add a commented substitution
near its related group (naming style: `subject_threshold_meaning`) / a named
constant, and reference it.
