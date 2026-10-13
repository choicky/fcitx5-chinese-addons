# AGENTS.md

This repository follows the Fcitx5 Fusion branch and review governance. Keep
product changes on the `fusion` product trunk through the normal reviewed PR
workflow; `master` is the upstream-only sync line.

## Workspace artifact lifecycle

Choose storage by artifact lifecycle, not historical habit:

- `~/worktmp/fcitx5-chinese-addons/<task>/` is durable recovery storage for
  CHECKLIST/recovery state and important evidence, diffs, reports, results, or
  release material that must survive interruption. It is not a permanent
  evidence repository or a replacement for Git, PRs, or project documents.
- `/tmp/fcitx5-chinese-addons/<task>/` is disposable/regenerable scratch for
  builds, extraction, inspection copies, intermediate files, and diagnostics.
- `/dev/shm/fcitx5-chinese-addons/<task>/` is only explicit, small,
  short-lived, fully-disposable RAM-backed scratch; it is not the default.
- Git, commits, PRs, and accepted repository documentation are the durable
  authority and normal review handoff. Historical evidence is not rewritten.
