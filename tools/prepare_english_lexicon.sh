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
#   * Word inventory: Rime Ice's curated English lists (iDvel/rime-ice,
#     en_dicts/en.dict.yaml + en_dicts/en_ext.dict.yaml) are the primary
#     vocabulary; Rime Frost (gaboolic/rime-frost, same two files)
#     supplements folded keys absent from Ice. Both upstreams are
#     GPL-3.0-only; the derived TSV is redistributed as a separable data
#     resource under GPL-3.0-only (AUDIT.md §4/§5). The former SCOWL-based
#     ~95k inventory was retired by fusion Decision D079.
#   * Tier (0..9, higher = more common) is a coarse list-membership
#     mapping, not imported per-word frequency: Ice en.dict=9, Ice
#     en_ext=8, Frost en=7, Frost en_ext=6; curated allow-list entries
#     ship at tier 9. No other ranking signal derives from upstream.
#   * Deterministic collision rules: a fold key's display comes from the
#     highest-priority source that carries it (Ice > Frost; within a repo
#     the higher-tier list wins); a plain-lowercase surface beats title
#     or mixed case inside one source; remaining ties resolve by byte
#     order under LC_ALL=C. Curated entries always ship their canonical
#     display at tier 9; the English Core emits the lowercase literal as
#     a parallel arc (D071) and user-typed case is authoritative (D069).
#   * Reachable keys only: the Pinyin composition path accepts ASCII
#     letters and an apostrophe (PinyinEngine::keyEvent, pinyin.cpp), so
#     surfaces are filtered to letters with optional internal
#     apostrophes. Digits, hyphens, spaces and punctuation forms are
#     skipped, never admitted by engine changes.
#   * Runtime format: TSV as documented in english_lexicon.h.
#   * No external download at addon runtime, no OTA lexicon update.
#   * The pinned MIT-licensed severe profanity list is applied by key
#     after the merge (plus simple inflections), as a structural content
#     guard independent of upstream filtering.
#
# Usage:
#   tools/prepare_english_lexicon.sh <work-dir>
#
# Emits (in <work-dir>):
#   english_lexicon.tsv   -> copy to im/pinyin/english_lexicon.tsv
#   manifest.json         -> copy to data-licenses/english/manifest.json
#   NOTICE                -> copy to data-licenses/english/NOTICE

set -euo pipefail

# Deterministic generation: display-collision ties resolve by byte order;
# a UTF-8 collation can flip rows and break the manifest SHA-256 gate.
export LC_ALL=C

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <work-dir>" >&2
  exit 2
fi

WORK="$1"
mkdir -p "$WORK"

# Pinned upstream snapshots. Any change to these pins must be a separate,
# reviewed commit from any merge-logic change (AUDIT.md §3).
UPSTREAM_ICE_REPO="https://github.com/iDvel/rime-ice.git"
UPSTREAM_ICE_COMMIT="da1fbe602e38f26db846fa10120ee64c2b0324c0"
UPSTREAM_FROST_REPO="https://github.com/gaboolic/rime-frost.git"
UPSTREAM_FROST_COMMIT="f1bc68b308adcca916c51bcaad25a267876e6efe"

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ALLOW_DIR="$REPO_ROOT/data-licenses/english"

for f in proper.allow technical.allow profanity-severe.allow; do
  [[ -r "$ALLOW_DIR/$f" ]] || { echo "ERROR: missing $ALLOW_DIR/$f" >&2; exit 4; }
done

# --- fetch pinned snapshots (exact commit, no tag drift) --------------------
fetch_pinned() {
  local repo="$1" commit="$2" dir="$3"
  if [[ ! -d "$dir/.git" ]]; then
    git init -q "$dir"
    git -C "$dir" remote add origin "$repo"
  fi
  if [[ "$(git -C "$dir" rev-parse HEAD 2>/dev/null || true)" != "$commit" ]]; then
    git -C "$dir" fetch --quiet --depth=1 origin "$commit"
    git -C "$dir" checkout --quiet FETCH_HEAD
  fi
  local actual
  actual="$(git -C "$dir" rev-parse HEAD)"
  if [[ "$actual" != "$commit" ]]; then
    echo "ERROR: checked-out $repo commit $actual does not match the pinned" \
         "$commit; refusing to generate from an unpinned source snapshot." >&2
    exit 5
  fi
}

ICE_DIR="$WORK/rime-ice"
FROST_DIR="$WORK/rime-frost"
fetch_pinned "$UPSTREAM_ICE_REPO" "$UPSTREAM_ICE_COMMIT" "$ICE_DIR"
fetch_pinned "$UPSTREAM_FROST_REPO" "$UPSTREAM_FROST_COMMIT" "$FROST_DIR"

for f in "$ICE_DIR/en_dicts/en.dict.yaml" "$ICE_DIR/en_dicts/en_ext.dict.yaml" \
         "$FROST_DIR/en_dicts/en.dict.yaml" "$FROST_DIR/en_dicts/en_ext.dict.yaml"; do
  [[ -r "$f" ]] || { echo "ERROR: missing $f" >&2; exit 4; }
done

# --- merge, tier, apply curated allow-lists, emit TSV ----------------------
EMITTED="$WORK/english_lexicon.tsv"
python3 - "$ICE_DIR" "$FROST_DIR" "$ALLOW_DIR" "$EMITTED" <<'PYEOF'
import os, re, sys

ice_dir, frost_dir, allow_dir, out = sys.argv[1:5]

# (path, tier), lowest source priority first; higher tier == higher
# priority (the two orderings agree by construction).
SOURCES = [
    (os.path.join(frost_dir, "en_dicts", "en_ext.dict.yaml"), 6),
    (os.path.join(frost_dir, "en_dicts", "en.dict.yaml"), 7),
    (os.path.join(ice_dir, "en_dicts", "en_ext.dict.yaml"), 8),
    (os.path.join(ice_dir, "en_dicts", "en.dict.yaml"), 9),
]

# Reachable surface alphabet: PinyinEngine::keyEvent appends ASCII letters
# and (with a non-empty preedit) the apostrophe; no other byte can enter
# an English span. Apostrophes may not lead or trail a word.
surface_re = re.compile(r"^[A-Za-z]+(?:'[A-Za-z]+)*$")

def parse(path):
    surfaces = []
    started = False
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if line == "...":
                started = True
                continue
            if not started or not line or line.startswith("#"):
                continue
            if "\t" not in line:
                continue
            surf = line.split("\t")[0].strip()
            if surf == "&nbsp;":
                continue
            if surface_re.match(surf):
                surfaces.append(surf)
    return surfaces

# folded key -> (tier, surfaces) keeping only the winning source's surfaces.
# Sources are processed in ascending priority, so later files may only
# override at strictly higher tier; equal tier across files is impossible
# because each file has a distinct tier constant here.
best = {}
for path, tier in SOURCES:
    for surf in parse(path):
        k = surf.lower()
        cur = best.get(k)
        if cur is None or tier > cur[0]:
            best[k] = (tier, {surf})
        elif tier == cur[0]:
            cur[1].add(surf)

merged = {}
for k, (tier, surfaces) in best.items():
    display = k if k in surfaces else sorted(surfaces)[0]
    merged[k] = (tier, display, "")

for name, flag in (("proper.allow", "p"), ("technical.allow", "t")):
    with open(os.path.join(allow_dir, name), encoding="utf-8") as fh:
        for line in fh:
            w = line.split("#", 1)[0].strip()
            if not w or not surface_re.match(w):
                continue
            k = w.lower()
            # Curated entries ship at tier 9 with their canonical display.
            # This applies uniformly to title case, internal capitals, and
            # all-uppercase forms; the English oracle supplies the lowercase
            # literal as a parallel arc, so canonical data never removes it.
            merged[k] = (9, w, flag)

# Apply the pinned, MIT-licensed severe profanity list after curated
# entries so no later allow-list collision can reintroduce an excluded key.
with open(os.path.join(allow_dir, "profanity-severe.allow"), encoding="utf-8") as fh:
    offensive = {
        line.split("#", 1)[0].strip().lower()
        for line in fh
        if line.split("#", 1)[0].strip()
    }
for k in list(merged):
    if k in offensive or any(
        k in ({base + "s", base + "'s", base + "ed", base + "ing"} |
              ({base + base[-1] + "ed", base + base[-1] + "ing"}
               if base[-1].isalpha() and base[-1] not in "aeiou"
               else set()))
        for base in offensive
    ):
        merged.pop(k, None)

with open(out, "w", encoding="utf-8") as fh:
    fh.write("# english_lexicon.tsv - production English system lexicon\n")
    fh.write("# source: Rime Ice en_dicts (github.com/iDvel/rime-ice, GPL-3.0-only)\n")
    fh.write("#         + Rime Frost en_dicts (github.com/gaboolic/rime-frost,\n")
    fh.write("#         GPL-3.0-only) supplementing folded keys absent from Ice;\n")
    fh.write("#         list-membership tiers Ice-en 9 / Ice-ext 8 / Frost-en 7 / Frost-ext 6;\n")
    fh.write("#         pinned MIT severe profanity list excluded.\n")
    fh.write("#         pins and SHA: data-licenses/english/manifest.json\n")
    fh.write("# plus curated proper/technical allow-list entries (tier 9;\n")
    fh.write("#         a colliding folded key takes the curated canonical display;\n")
    fh.write("#         the lowercase literal stays a parallel arc, D071).\n")
    fh.write("# keys filtered to the reachable input alphabet: letters with optional\n")
    fh.write("#         internal apostrophes; digits/hyphens/spaces/punctuation skipped.\n")
    fh.write("# format: key<TAB>tier<TAB>display<TAB>flags{p,t,l}\n")
    for k in sorted(merged):
        t, display, flags = merged[k]
        fh.write(f"{k}\t{t}\t{display}\t{flags}\n")
PYEOF

# --- manifest + NOTICE ------------------------------------------------------
SHA=$(sha256sum "$EMITTED" | awk '{print $1}')
SIZE=$(wc -c < "$EMITTED" | awk '{print $1}')
COUNT=$(grep -c -v -e '^#' -e '^$' "$EMITTED" || true)

MANIFEST="$WORK/manifest.json"
cat > "$MANIFEST" <<JSON
{
  "resource": "im/pinyin/english_lexicon.tsv",
  "resource_license": "GPL-3.0-only (derived word data, redistributed as a separable resource with attribution - see data-licenses/english/NOTICE and AUDIT.md)",
  "upstreams": [
    {
      "name": "Rime Ice",
      "role": "primary English vocabulary",
      "repo": "${UPSTREAM_ICE_REPO}",
      "commit": "${UPSTREAM_ICE_COMMIT}",
      "files": ["en_dicts/en.dict.yaml", "en_dicts/en_ext.dict.yaml"],
      "license": "GPL-3.0-only"
    },
    {
      "name": "Rime Frost",
      "role": "supplement (folded keys absent from Ice)",
      "repo": "${UPSTREAM_FROST_REPO}",
      "commit": "${UPSTREAM_FROST_COMMIT}",
      "files": ["en_dicts/en.dict.yaml", "en_dicts/en_ext.dict.yaml"],
      "license": "GPL-3.0-only"
    }
  ],
  "tiering": "list membership only: Ice en.dict=9, Ice en_ext=8, Frost en=7, Frost en_ext=6; curated proper/technical entries ship at tier 9; no per-word upstream frequency is imported",
  "key_filter": "reachable input alphabet: ASCII letters with optional internal apostrophes (PinyinEngine::keyEvent); digits/hyphens/spaces/punctuation forms skipped",
  "frequency_source": "none - no frequency-rank corpus is consumed",
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

Word inventory derived from the Rime Ice English dictionaries
(github.com/iDvel/rime-ice, commit ${UPSTREAM_ICE_COMMIT}) supplemented by
the Rime Frost English dictionaries (github.com/gaboolic/rime-frost, commit
${UPSTREAM_FROST_COMMIT}). Both upstream projects are licensed under the
GNU General Public License version 3 only; this generated data resource is
redistributed under GPL-3.0-only as a separable data file (the consuming
addon code remains LGPL-2.1-or-later). Upstream license texts are available
in the pinned repositories. The corresponding source of this resource is
the exact TSV in this repository together with the regeneration process
recorded in data-licenses/english/manifest.json and
tools/prepare_english_lexicon.sh, from the pinned commits above.

The shipped TSV contains lower-case lookup keys, a coarse list-membership
tier integer, the upstream surface spelling, and repository-curated
proper/technical allow-list entries ((C) 2026 Fcitx5 Fusion contributors).
The pinned MIT-licensed severe profanity list is applied by key at
generation time. No frequency-rank corpus content is included. The former
SCOWL-derived inventory was retired on 2026-10-12 (fusion Decision D079);
its NOTICE/manifest records remain in git history.

SHA-256: ${SHA}
Bytes:   ${SIZE}
Entries: ${COUNT}
NOTICE

echo "emitted:  $EMITTED"
echo "manifest: $MANIFEST"
echo "notice:   $WORK/NOTICE"
echo "entries:  $COUNT  sha256: $SHA"
