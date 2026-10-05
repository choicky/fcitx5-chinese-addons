/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_PINYIN_MIXED_ENGLISH_ENGLISHLEXICON_H_
#define _FCITX_PINYIN_MIXED_ENGLISH_ENGLISHLEXICON_H_

#include <cstddef>
#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pinyin {

// One immutable English system-lexicon entry.
// `key` is the case-folded ASCII lookup form (lowercase).
// `display` is the recommended output form (may preserve Title/UPPER casing
// for brands and acronyms). It is NOT required to equal `key`.
// `tier` is a coarse 0..9 source-local frequency bucket. It is *not* a
// cross-source comparable score (§19) and is used only inside the English
// Core for confidence ranking.
struct EnglishLexiconEntry {
    std::string key;
    std::string display;
    std::uint16_t tier = 0;
    bool proper = false;
    bool technical = false;
    // Literal-only entries are accepted as typed English but never contribute
    // to completion fan-out. Used to keep obscure surface spellings without
    // polluting the completion candidate pool.
    bool literalOnly = false;
};

// Self-contained English system lexicon used by Architecture A.
//
// Design notes (§13, §14):
//   * Runtime representation is a sorted vector keyed by case-folded ASCII
//     with binary search for exact / canonical lookup and equal-range scan
//     for prefix completions. No external dependency and no fixed minimum
//     prefix requirement — completions are emitted at any prefix length that
//     has at least one hit, ranked by dynamic evidence (§13).
//   * Confidence is derived from tier + prefix length + completion fan-out so
//     short ambiguous prefixes are naturally weaker than long specific ones,
//     without a hard gate.
//   * The lexicon is loaded once at addon init from a versioned resource file
//     installed under ${FCITX_INSTALL_PKGDATADIR}/pinyin. The production
//     resource preparation step is documented under data-licenses/ENGLISH.md;
//     the shipped resource must be pinned by source version and SHA-256.
//   * English Core never depends on the runtime ISpell sidecar (Spell module).
class EnglishLexicon {
public:
    EnglishLexicon();

    // Load entries from a TSV-formatted istream. Lines starting with '#' are
    // comments; blank lines are skipped. Each data row is:
    //   key <TAB> tier <TAB> display <TAB> flags
    // `flags` is a comma-separated list of {p,t,l} meaning proper / technical
    // / literalOnly, and may be empty. Any parsing failure on a single line
    // skips that line without aborting the load. The lexicon is cleared
    // before loading. After load, entries are sorted by key.
    void load(std::istream &in);

    // Load with an explicit inline range (used by tests and by Batch 4 to
    // merge user-lexicon learning results without re-reading the resource).
    void loadFromEntries(std::vector<EnglishLexiconEntry> entries);

    void clear();

    bool empty() const { return entries_.empty(); }
    std::size_t size() const { return entries_.size(); }

    // Case-insensitive exact lookup on the folded ASCII form. Returns a
    // pointer into the internal store, or nullptr.
    const EnglishLexiconEntry *lookup(std::string_view folded) const;

    // Case-insensitive membership: any spelling whose folded ASCII form
    // matches a lexicon key. Empty spans and non-ASCII spans return false.
    bool isExact(std::string_view raw) const;

    // Canonicalize: if the folded form exists, return the recommended display
    // spelling for that entry. Returns nullopt on a miss.
    std::optional<std::string> canonicalize(std::string_view raw) const;

    // Completions: enumerate entries whose key begins with the folded prefix.
    // Empty prefix returns an empty result (defensive). `maxResults` bounds
    // fan-out. Results preserve the sorted-by-key order; callers should apply
    // completionEvidence() to rank them further. Literal-only entries are
    // excluded from completions.
    std::vector<const EnglishLexiconEntry *>
    completions(std::string_view foldedPrefix, std::size_t maxResults) const;

    // Number of non-literal completions for a folded prefix. Used to compute
    // dynamic fan-out penalty.
    std::size_t completionCount(std::string_view foldedPrefix) const;

    // Source-local confidence, always clamped to [0.0, 1.0]. Never used
    // across sources (§19); UnifiedRanker re-normalizes per source (§19).
    // Exact: 0.65 + 0.035 * tier.
    // Canonical (case variant): Exact * 0.95.
    // Completion: 0.45 + 0.035 * tier + specificity - fanout_penalty,
    //   where specificity = min(0.10, 0.02 * (prefixLen - 1)) and
    //   fanout_penalty = min(0.10, 0.002 * completionCountForPrefix).
    // Literal-only entries: Exact is 0.55 regardless of tier.
    float exactEvidence(const EnglishLexiconEntry &entry) const;
    float canonicalEvidence(const EnglishLexiconEntry &entry) const;
    float completionEvidence(const EnglishLexiconEntry &entry,
                             std::size_t foldedPrefixLen,
                             std::size_t completionCount) const;

    // Fold raw ASCII letters to lowercase for lookup. Non-ASCII bytes are
    // passed through unchanged. Returns nullopt if the span contains any
    // non-alphabetic byte other than '\'' or '-', which signals that the raw
    // span cannot be an English word surface form.
    static std::optional<std::string> foldKey(std::string_view raw);

private:
    std::size_t lowerBound(std::string_view key) const;
    std::size_t upperBound(std::string_view key) const;

    std::vector<EnglishLexiconEntry> entries_;
};

} // namespace pinyin

#endif // _FCITX_PINYIN_MIXED_ENGLISH_ENGLISHLEXICON_H_
