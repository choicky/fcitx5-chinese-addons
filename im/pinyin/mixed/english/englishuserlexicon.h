/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_PINYIN_MIXED_ENGLISH_ENGLISHUSERLEXICON_H_
#define _FCITX_PINYIN_MIXED_ENGLISH_ENGLISHUSERLEXICON_H_

#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "../mixedsegmentation.h"

namespace pinyin {

// A single English user-learned entry: a word the user has confirmed at
// least once via the mixed-input commit path. Not the same concept as the
// Chinese CustomPhrase table; both use the shared CustomPhrase provenance
// tag downstream because both represent user-added surface forms.
struct EnglishUserEntry {
    std::string key;     // folded ASCII lowercase
    std::string display; // user-preferred spelling
    std::uint32_t count = 0;
    std::uint64_t firstSeenSec = 0;
    std::uint64_t lastUsedSec = 0;
};

// Versioned, atomic-persistable, self-contained English user lexicon.
//
// Persistence contract (§14, §22):
//   * Schema version is the leading `#schema=1` header line. Loader rejects
//     any other version and starts empty without throwing, so Pinyin init
//     never blocks on a corrupt or future-schema file.
//   * Malformed data rows are silently skipped; only rows carrying a valid
//     `key<TAB>count<TAB>first_seen<TAB>last_used<TAB>display` quintuple
//     contribute to state.
//   * The caller is expected to save via Fcitx5 `StandardPaths::safeSave`,
//     which provides atomic write-then-rename (same mechanism used for the
//     existing Chinese custom phrase table at pinyin.cpp `saveCustomPhrase`).
//     save() itself only emits to the provided ostream and does NOT flush
//     the underlying file.
class EnglishUserLexicon {
public:
    static constexpr std::uint32_t kSchemaVersion = 1;

    EnglishUserLexicon();

    // Load schema-versioned TSV. Returns true iff the stream parsed cleanly
    // AND the schema version equals kSchemaVersion. On any failure the
    // lexicon is left in an empty (but valid) state; no throw path.
    bool load(std::istream &in);

    // Emit the current state as the same schema-versioned TSV. The caller
    // is expected to invoke this inside a `safeSave` lambda so the write is
    // crash-atomic against the target file.
    void save(std::ostream &out) const;

    // Learn a confirmed selection: increments count, refreshes lastUsedSec,
    // creates the entry on first confirmation. Does NOT persist; the caller
    // must trigger save through StandardPaths::safeSave when appropriate
    // (e.g. on commit, on addon shutdown). FoldedKey is derived from
    // `folded`; display is the user's confirmed casing. Empty folded is a
    // no-op.
    void learn(std::string_view folded, std::string_view display,
               std::uint64_t nowSec);

    // Remove one entry. Returns true iff it was present.
    bool forget(std::string_view folded);

    // Clear all entries. Used by the config's "reset English learning"
    // action; reuses the existing custom-phrase delete semantics by being
    // a pure state clear (persistence handled by caller).
    void reset();

    // Binary-search lookup by folded key. nullptr on miss.
    const EnglishUserEntry *lookup(std::string_view folded) const;

    // Read-only view of all entries, sorted by key.
    const std::vector<EnglishUserEntry> &entries() const { return entries_; }

    std::size_t size() const { return entries_.size(); }

    // Source-local evidence, saturating in count. Never 1.0 to preserve
    // headroom for UnifiedRanker's cross-source normalization (§19).
    //   0.55 + 0.40 * log1p(count) / log1p(1000)  clamped to 0.95
    float evidence(const EnglishUserEntry &entry) const;

private:
    std::size_t lowerBound(std::string_view key) const;
    std::vector<EnglishUserEntry> entries_;
};

// Emits user-learned English surface forms as arcs with
// SegmentSource::English + CandidateProvenance::CustomPhrase. UnifiedRanker
// distinguishes user-learned from System Lexicon by provenance, not by
// source (§19). `lex` must outlive the oracle.
class EnglishUserArcOracle final : public IEnglishArcOracle {
public:
    explicit EnglishUserArcOracle(const EnglishUserLexicon *lex);

    std::vector<SegmentationArc> arcsAt(std::string_view raw, size_t begin,
                                        size_t maxSpan) const override;

private:
    const EnglishUserLexicon *lex_;
};

} // namespace pinyin

#endif // _FCITX_PINYIN_MIXED_ENGLISH_ENGLISHUSERLEXICON_H_
