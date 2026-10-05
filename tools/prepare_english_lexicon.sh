#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Offline preparation of the production English system lexicon resource
# consumed by the Architecture A English Core. This script is NOT run by
# the build; it is a manual, review-controlled step. The generated file
# must be re-hashed and its SHA-256 recorded in
# data-licenses/english/manifest.json before shipping.
#
# Design contract (see data-licenses/english/AUDIT.md):
#   * Word inventory source (pinned upstream, permissive license):
#       SCOWL  -- see UPSTREAM_SCOWL_TAG below
#   * Frequency rank source (used only to derive 0..9 tier; the shipped
#     file contains tier integers, no verbatim rank data):
#       hermitdave/FrequencyWords  -- see UPSTREAM_FREQ_TAG below
#   * Curated proper / technical allow-lists maintained in this repository.
#   * Runtime format: TSV as documented in english_lexicon.h.
#   * No external download at addon runtime, no OTA lexicon update.
#
# Usage:
#   tools/prepare_english_lexicon.sh <work-dir>
#
# Emits:
#   <work-dir>/english_lexicon.tsv
#   <work-dir>/manifest.json   (SHA-256, byte size, entry count, upstream pins)

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <work-dir>" >&2
  exit 2
fi

WORK="$1"
mkdir -p "$WORK"

# Pinned upstream snapshots. These MUST be reviewed by a maintainer before
# any change; the audit doc requires that the pin be updated in a
# separate commit from any tiering logic change.
UPSTREAM_SCOWL_REPO="https://repo.well.com.br/~arthur/scowl/"
UPSTREAM_SCOWL_TAG="" # set to a specific SCOWL release (e.g. "v10.1")
UPSTREAM_FREQ_REPO="https://github.com/hermitdave/FrequencyWords"
UPSTREAM_FREQ_TAG="" # set to a specific commit short-sha

if [[ -z "$UPSTREAM_SCOWL_TAG" || -z "$UPSTREAM_FREQ_TAG" ]]; then
  cat >&2 <<'MSG'
ERROR: pinned upstream tags are unset.

This script intentionally refuses to fetch an unpinned snapshot. Edit
UPSTREAM_SCOWL_TAG and UPSTREAM_FREQ_TAG to the exact release/commit you
have reviewed for license compatibility, then rerun. See
data-licenses/english/AUDIT.md §3–§5.
MSG
  exit 3
fi

# Fetch steps are intentionally left as comments until the first production
# resource bump is authorized. Keeping the pipeline auditable:
#
#   curl -L --fail -o "$WORK/scwl.tar.gz" \
#       "${UPSTREAM_SCOWL_REPO}scowl-${UPSTREAM_SCOWL_TAG}.tar.gz"
#   tar -xzf "$WORK/scwl.tar.gz" -C "$WORK"
#   git -c advice.detachedHead=false clone --depth=1 --branch "$UPSTREAM_FREQ_TAG" \
#       "$UPSTREAM_FREQ_REPO" "$WORK/freq"
#
# Tier assignment: rank-based quintile within the merged inventory.
#
#   awk 'BEGIN{OFS="\t"} { tier = int(9 * (1 - NR/N)); print tolower($1), tier, $1, "" }' \
#       "$WORK/freq/en/wordfrequencies.txt" > "$WORK/english_lexicon.tsv"
#
# Proper and technical flags are applied from this repository's curated
# allow-lists (data-licenses/english/proper.allow, technical.allow) once
# those files exist; V1 ships zero curated entries until reviewed.

EMITTED="$WORK/english_lexicon.tsv"
: > "$EMITTED"
echo "# placeholder: no production resource pinned yet" >> "$EMITTED"

SHA=$(sha256sum "$EMITTED" | awk '{print $1}')
SIZE=$(wc -c < "$EMITTED" | awk '{print $1}')
COUNT=$(grep -c -v -e '^#' -e '^$' "$EMITTED" || true)

MANIFEST="$WORK/manifest.json"
cat > "$MANIFEST" <<JSON
{
  "resource": "english_lexicon.tsv",
  "upstream": {
    "scowl": { "repo": "${UPSTREAM_SCOWL_REPO}", "tag": "${UPSTREAM_SCOWL_TAG}" },
    "frequency": { "repo": "${UPSTREAM_FREQ_REPO}", "tag": "${UPSTREAM_FREQ_TAG}" }
  },
  "sha256": "${SHA}",
  "bytes": ${SIZE},
  "entries": ${COUNT},
  "prepared_by": "tools/prepare_english_lexicon.sh"
}
JSON

echo "emitted: $EMITTED"
echo "manifest: $MANIFEST"
