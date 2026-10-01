# Plan 07: yamllint cleanup of the two large configs

**Status:** queued 2026-10-02, no code yet
**Scope:** `lilygos3-info-screen.yaml`, `air-quality-sensor-1.yaml`
**Owner:** Dimitri

## Why this exists

The OTA encryption change on 2026-10-02 touched both files. Touched files
are supposed to leave the linter clean, but these two carry far more
findings than that one-line change could absorb, so the cleanup is queued
here instead of bolted onto that commit.

Counts from `yamllint -c .yamllint` on that date:

| File | Findings | Mostly |
| --- | --- | --- |
| `lilygos3-info-screen.yaml` | 234 | long lambda lines, comment gaps, a tab |
| `air-quality-sensor-1.yaml` | 38 | long lambda lines, trailing spaces |

The tab is a yamllint syntax error at `lilygos3-info-screen.yaml:2168`.
ESPHome still validates the file, because the tab sits inside a block
scalar, but it blocks yamllint from checking anything after it.

## What makes it more than a mechanical sweep

- Trailing spaces, comment spacing, `True` instead of `true`, and the tab
  are mechanical and safe.
- Most line-length findings sit inside `lambda: |-` blocks. Rewrapping a
  C++ call across lines is safe but changes a lot of lines, and every
  edited lambda deserves a compile, not just `esphome config`, before it
  is flashed to a battery device.

## How to do it

1. Fix the mechanical class first and commit it on its own.
2. Rewrap the lambdas file by file. Prefer one argument per line for long
   `it.printf` calls. Keep each lambda's behaviour identical.
3. Validate each file with `esphome config` and `esphome compile`, then
   flash and watch one boot log.
4. Confirm `yamllint -c .yamllint <file>` is clean before committing.
