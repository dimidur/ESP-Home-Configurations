# Memory index

- [feedback_memory_privacy.md](feedback_memory_privacy.md): This dir is public: repo-relevant memories only, no private-infra details, no links outside the repo

- [feedback_no_tasmota_mentions.md](feedback_no_tasmota_mentions.md): Never mention Tasmota in configs, docs, or commits
- [feedback_no_coauthored.md](feedback_no_coauthored.md): Never add Co-Authored-By Claude lines to git commits
- [project_smart_meter_hw_config.md](project_smart_meter_hw_config.md): Smart meter reader HW: LK13BE606739 sends SML 9600 8N1; sensitised IR head needs an optical filter; 10 kOhm pullup
- [feedback_one_git_commit_per_command.md](feedback_one_git_commit_per_command.md): One git commit per shell command: stage, commit, verify sequentially, never chain (GPG-sign can fail and tangle staging)
- [feedback_explain_before_changes.md](feedback_explain_before_changes.md): Explain diagnosis + proposed fix first, wait for approval, only then edit; never both in one turn
- [feedback_thresholds_as_substitutions.md](feedback_thresholds_as_substitutions.md): Never hardcode magic numbers ANYWHERE (antipattern); tunables go in substitutions/named constants
