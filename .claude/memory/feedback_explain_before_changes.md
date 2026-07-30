---
name: explain-before-changes
description: "Always explain the diagnosis/proposed fix FIRST and wait for approval, only then edit — never bundle explanation + edits in one turn"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: aaacfe53-d320-450d-a838-9ae22076e0a6
  modified: 2026-07-29T23:48:24.876Z
---

Always offer the explanation first, and only after the user agrees attempt the
changes — never the other way around, and never both in the same turn.

**Why:** The user wants to vet the diagnosis and the proposed approach before any
file is touched (their configs are heavily iterated; a plausible-sounding fix can
destroy tuned behavior). Stated explicitly 2026-07-29 after I started editing the
soil-import filters while still explaining the throttle/delta bug: "first you
always offer explanation and only then attempt to do changes, never other way
around, memorise."

**How to apply:** When finding a bug or proposing an improvement in this repo:
1. Present diagnosis + exact intended change (which lines/blocks, what semantics change).
2. Stop and wait for explicit go-ahead ("do it", "green light", etc.).
3. Only then edit.
An explicit imperative request ("fix X", "commit", "rename Y") is itself the
go-ahead for exactly that scoped action — but anything beyond the asked scope
needs its own explanation + approval round first.
