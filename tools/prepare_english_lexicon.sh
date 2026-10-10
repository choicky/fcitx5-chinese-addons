#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Offline preparation of the production English system lexicon resource
# consumed by the Architecture A English Core. This script is NOT run by
# the build; it is a manual, review-controlled step. The generated file
# is committed under im/pinyin/english_lexicon.tsv together with
# data-licenses/english/manifest.json; the manifest SHA-256 is re-verified
# at configure time by im/pinyin/CMakeLists.txt (build fails on mismatch).
#
# Design contract (see data-licenses/english/AUDIT.md):
#   * Single word inventory source: SCOWL v2 (en-wl/wordlist), permissive
#     custom license that explicitly covers word lists created from it.
#   * Tier (0..9, higher = more frequent) is derived ONLY from the SCOWL
#     size tag at which a word first appears:
#         tier = 9 - round(9 * (size - 35) / 25)
#     i.e. 35->9, 40->7, 50->4, 60->0 for the shipped American basic list.
#   * Frequency-rank data (hermitdave/FrequencyWords) is NOT used: its
#     word content is CC-BY-SA-4.0 (ShareAlike) and cannot be relicensed
#     into this LGPL project's derived TSV. See AUDIT.md §4/§5.
#   * Curated proper / technical surface forms live in this repository
#     (data-licenses/english/proper.allow, technical.allow) and ship with
#     tier 9 and flags p / t. Their canonical surface replaces the folded
#     SCOWL display on collision; the runtime emits the lowercase literal
#     beside it when appropriate.
#   * Runtime format: TSV as documented in english_lexicon.h.
#   * No external download at addon runtime, no OTA lexicon update.
#   * SCOWL usage-note classes offensive-1/2 and vulgar-1/2/3 are excluded
#     structurally at extraction time. SCOWL does not mark every offensive
#     homograph (including faggot/fagot), so the pinned MIT-licensed severe
#     list is also applied by key, not as an eval-word hand list.
#
# Usage:
#   tools/prepare_english_lexicon.sh <work-dir>
#
# Emits (in <work-dir>):
#   english_lexicon.tsv   -> copy to im/pinyin/english_lexicon.tsv
#   manifest.json         -> copy to data-licenses/english/manifest.json
#   NOTICE                -> copy to data-licenses/english/NOTICE

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <work-dir>" >&2
  exit 2
fi

WORK="$1"
mkdir -p "$WORK"

# Pinned upstream snapshot. Any change to these pins must be a separate,
# reviewed commit from any tiering-logic change (AUDIT.md §3).
UPSTREAM_SCOWL_REPO="https://github.com/en-wl/wordlist.git"
UPSTREAM_SCOWL_TAG="rel-2026.02.25"
UPSTREAM_SCOWL_COMMIT="7e99edab8e32f9f9ea2b15f249ca8d4d67237410" # == rel-2026.02.25

# Shipped SCOWL size buckets (American spelling, variant level <= 1,
# deaccented). Sizes are the buckets actually present in SCOWL v2.
SCOWL_SIZES=(35 40 50 60)

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ALLOW_DIR="$REPO_ROOT/data-licenses/english"

for f in proper.allow technical.allow profanity-severe.allow; do
  [[ -r "$ALLOW_DIR/$f" ]] || { echo "ERROR: missing $ALLOW_DIR/$f" >&2; exit 4; }
done

# --- fetch pinned snapshot -------------------------------------------------
SCOWL_DIR="$WORK/wordlist"
if [[ ! -d "$SCOWL_DIR/.git" ]]; then
  git -c advice.detachedHead=false clone --depth=1 --branch "$UPSTREAM_SCOWL_TAG" \
      "$UPSTREAM_SCOWL_REPO" "$SCOWL_DIR"
fi
ACTUAL_COMMIT=$(git -C "$SCOWL_DIR" rev-parse HEAD)
if [[ "$ACTUAL_COMMIT" != "$UPSTREAM_SCOWL_COMMIT"* && \
      "$UPSTREAM_SCOWL_COMMIT" != "$ACTUAL_COMMIT"* ]]; then
  echo "WARNING: checked-out commit $ACTUAL_COMMIT differs from recorded" \
       "$UPSTREAM_SCOWL_COMMIT; trusting the tag but update the pin." >&2
fi

# --- build the SCOWL database (deterministic from the pinned tree) ---------
if [[ ! -f "$SCOWL_DIR/scowl.db" ]]; then
  (cd "$SCOWL_DIR" && python3 ./combine.py create-db scowl.db)
fi

# --- extract per-size word lists -------------------------------------------
# SCOWL carries ~30k all-uppercase abbreviations and uncertain-capital
# names (AA, NIH, NAACP, MHz, …). Those are spell-check artifacts, not
# English the IME should offer as exact arcs for 2–4 letter raw streams
# that overlap pinyin abbreviations, so the shipped inventory keeps only
# regular lower-case word classes. Curated brands/acronyms ship via the
# allow-lists instead.
POS_CLASSES_TO_EXCLUDE="upper,upper?,abbr,abbr?,trademark,number,ordinal,name,name?,surname"
USAGE_NOTES_TO_EXCLUDE="offensive-1,offensive-2,vulgar-1,vulgar-2,vulgar-3"
for s in "${SCOWL_SIZES[@]}"; do
  (cd "$SCOWL_DIR" && ./scowl --db scowl.db word-list "$s" A 1 --deaccent \
      --wo-pos-classes "$POS_CLASSES_TO_EXCLUDE" \
      --wo-usage-notes "$USAGE_NOTES_TO_EXCLUDE") \
      2>/dev/null | sort -u > "$WORK/list_$s.txt"
done

# --- merge, tier, apply curated allow-lists, emit TSV ----------------------
EMITTED="$WORK/english_lexicon.tsv"
python3 - "$WORK" "$ALLOW_DIR" "$EMITTED" <<'PYEOF'
import math, os, re, sys

work, allow_dir, out = sys.argv[1:4]
sizes = [35, 40, 50, 60]

def tier_for(size):
    return 9 - int(round(9 * (size - 35) / 25.0))

key_re = re.compile(r"^[a-zA-Z'-]+$")
best = {}  # folded key -> (tier, display, flags)

for s in sizes:
    with open(os.path.join(work, f"list_{s}.txt"), encoding="utf-8") as fh:
        for line in fh:
            w = line.strip()
            if not w or not key_re.match(w):
                continue
            k = w.lower()
            t = tier_for(s)
            if k not in best or t > best[k][0]:
                best[k] = (t, w, "")

for name, flag in (("proper.allow", "p"), ("technical.allow", "t")):
    with open(os.path.join(allow_dir, name), encoding="utf-8") as fh:
        for line in fh:
            w = line.split("#", 1)[0].strip()
            if not w or not key_re.match(w):
                continue
            k = w.lower()
            # Curated entries ship at tier 9 with their canonical display.
            # This applies uniformly to title case, internal capitals, and
            # all-uppercase forms; the English oracle supplies the lowercase
            # literal as a parallel arc, so canonical data never removes it.
            best[k] = (9, w, flag)

# SCOWL usage notes are authoritative where present, but its catalog leaves
# some offensive homographs unmarked. Apply the pinned, MIT-licensed severe
# list after curated entries so no later allow-list collision can reintroduce
# an excluded key.
with open(os.path.join(allow_dir, "profanity-severe.allow"), encoding="utf-8") as fh:
    offensive = {
        line.split("#", 1)[0].strip().lower()
        for line in fh
        if line.split("#", 1)[0].strip()
    }
for k in list(best):
    if k in offensive or any(
        k in ({base + "s", base + "'s", base + "ed", base + "ing"} |
              ({base + base[-1] + "ed", base + base[-1] + "ing"}
               if base[-1].isalpha() and base[-1] not in "aeiou"
               else set()))
        for base in offensive
    ):
        best.pop(k, None)

with open(out, "w", encoding="utf-8") as fh:
    fh.write("# english_lexicon.tsv — production English system lexicon\n")
    fh.write("# source: SCOWL v2 (github.com/en-wl/wordlist) tag rel-2026.02.25,\n")
    fh.write("#         American spellings, variant level <= 1, deaccented,\n")
    fh.write("#         sizes 35/40/50/60, SCOWL upper/abbr/name classes excluded,\n")
    fh.write("#         SCOWL usage notes offensive-1/2 and vulgar-1/2/3 excluded,\n")
    fh.write("#         plus pinned MIT severe profanity list excluded.\n")
    fh.write("#         first-size tiers 35->9 40->7 50->4 60->0\n")
    fh.write("# plus curated proper/technical allow-list entries (tier 9;\n")
    fh.write("#         folded keys colliding with SCOWL words are skipped).\n")
    fh.write("# format: key<TAB>tier<TAB>display<TAB>flags{p,t,l}\n")
    for k in sorted(best):
        t, display, flags = best[k]
        fh.write(f"{k}\t{t}\t{display}\t{flags}\n")
PYEOF

# --- manifest + NOTICE ------------------------------------------------------
SHA=$(sha256sum "$EMITTED" | awk '{print $1}')
SIZE=$(wc -c < "$EMITTED" | awk '{print $1}')
COUNT=$(grep -c -v -e '^#' -e '^$' "$EMITTED" || true)
LICENSE=$(sed -n '1,5p' "$SCOWL_DIR/Copyright" | tr '\n' ' ' | sed 's/  */ /g')

MANIFEST="$WORK/manifest.json"
cat > "$MANIFEST" <<JSON
{
  "resource": "im/pinyin/english_lexicon.tsv",
  "upstream": {
    "name": "SCOWL (v2)",
    "repo": "${UPSTREAM_SCOWL_REPO}",
    "tag": "${UPSTREAM_SCOWL_TAG}",
    "commit": "${ACTUAL_COMMIT}",
    "license": "SCOWL permissive custom license (see Copyright file; explicitly covers word lists created from SCOWL)",
    "selection": "American spellings, variant level <= 1, deaccented, sizes 35/40/50/60, excluding SCOWL pos-classes upper/abbr/trademark/number/ordinal/name/surname and usage notes offensive-1/2,vulgar-1/2/3"
  },
  "tiering": "tier = 9 - round(9*(size-35)/25) on first-appearance size; curated proper/technical entries ship at tier 9",
  "frequency_source": "none — hermitdave/FrequencyWords content is CC-BY-SA-4.0 and was rejected for relicensing reasons (AUDIT.md §4/§5)",
    "curated": ["data-licenses/english/proper.allow", "data-licenses/english/technical.allow"],
    "offensive_filter": "data-licenses/english/profanity-severe.allow (MIT; canonical severe entries)",
  "sha256": "${SHA}",
  "bytes": ${SIZE},
  "entries": ${COUNT},
  "prepared_by": "tools/prepare_english_lexicon.sh",
  "prepared": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
JSON

cat > "$WORK/NOTICE" <<NOTICE
Production English system lexicon (english_lexicon.tsv)

Word inventory derived from SCOWL v2, (C) 2000-2026 Kevin Atkinson and
the SCOWL contributors, ${UPSTREAM_SCOWL_REPO} tag ${UPSTREAM_SCOWL_TAG}
(commit ${ACTUAL_COMMIT}).

SCOWL license (verbatim excerpt from the pinned Copyright file):
${LICENSE}

The shipped TSV contains only lower-case lookup keys, a coarse SCOWL
size-derived tier integer, the SCOWL surface spelling, and repository-
curated proper/technical allow-list entries (LGPL-2.1-or-later, (C) 2026
Fcitx5 Fusion contributors). SCOWL offensive-1/2 and vulgar-1/2/3 usage-note
entries and the pinned MIT severe profanity list are excluded structurally.
No frequency-rank corpus content is included.

SHA-256: ${SHA}
Bytes:   ${SIZE}
Entries: ${COUNT}
NOTICE

echo "emitted:  $EMITTED"
echo "manifest: $MANIFEST"
echo "notice:   $WORK/NOTICE"
echo "entries:  $COUNT  sha256: $SHA"
