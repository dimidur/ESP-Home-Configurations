# Memory index

- [feedback_no_tasmota_mentions.md](feedback_no_tasmota_mentions.md): Never mention Tasmota in configs, docs, or commits
- [feedback_no_coauthored.md](feedback_no_coauthored.md): Never add Co-Authored-By Claude lines to git commits
- [project_smart_meter_hw_config.md](project_smart_meter_hw_config.md): Smart meter reader verified HW config — 10 kΩ external RX pullup + 33 kΩ head load resistor mod (Logarex LK13BE 803039)
- [feedback_one_git_commit_per_command.md](feedback_one_git_commit_per_command.md): One git commit per shell command — stage→commit→verify sequentially, never chain (GPG-sign can fail & tangle staging)
- [feedback_explain_before_changes.md](feedback_explain_before_changes.md): Explain diagnosis + proposed fix first, wait for approval, only then edit — never both in one turn
- [feedback_thresholds_as_substitutions.md](feedback_thresholds_as_substitutions.md): Never hardcode magic numbers ANYWHERE (antipattern) — tunables go in substitutions/named constants
