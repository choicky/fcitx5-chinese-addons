/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_PINYIN_MIXED_ENGLISH_ENGLISHARCORACLE_H_
#define _FCITX_PINYIN_MIXED_ENGLISH_ENGLISHARCORACLE_H_

#include <cstddef>
#include <string_view>
#include <vector>

#include "../mixedsegmentation.h"
#include "englishlexicon.h"

namespace pinyin {

// IEnglishArcOracle implementation backed by the self-contained
// EnglishLexicon. Emits arcs for every valid English surface interpretation
// of a raw span, subject to fan-out caps:
//   * Exact:   span folds to a lexicon key with a case-identical surface.
//   * Canonical: span folds to a lexicon key but differs in casing (e.g.
//                user typed "iPhone" and the display form is "iPhone", or
//                user typed "HELLO" and the display form is "hello").
//   * Completion: span folds to a proper prefix of at least one non-literal
//                lexicon entry. Top-`maxCompletionsPerSpan` completions by
//                frequency tier (inside a tier: shortest completion first,
//                then key) over a bounded prefix scan,
//                matching the tier-driven completionEvidence scoring (§13).
// Correction arcs (Batch 5) and user-lexicon arcs (Batch 4) are emitted by
// separate oracles and merged downstream, keeping this oracle's job narrow.
//
// Confidence values are source-local (§19); UnifiedRanker re-normalizes
// across sources.
//
// Production-scale finding (closure §6): the shipped SCOWL-derived lexicon
// contains ~200 one- and two-letter "words" (a, i, an, us, ma, ii, cos, ...).
// At the product surface those letters and short interjections double as
// pinyin initials and two-letter syllables, so short English arcs let the
// beam tile almost any raw as letter soup ("wodakaIGitHub"), starve real
// candidates out of the bounded pool, and can never pass the §9 lead-evidence
// bar anyway. The oracle therefore requires `minSpanLength` (default 3) raw
// bytes before a span participates as exact/canonical/completion evidence —
// the same evidence floor the correction oracle already uses (§12) and the
// same §9(b) minimum that decides whether English may lead. The lexicon
// itself keeps these entries (dictionary-level `isExact`/completion APIs are
// unaffected); only arc emission is gated. Completion fan-out remains
// evidence-driven beyond that floor (§13).
class EnglishArcOracle final : public IEnglishArcOracle {
public:
    struct Config {
        // Bounded fan-out per raw span for completion arcs. NOT a user
        // exposed knob; internal to keep the DAG cost predictable.
        std::size_t maxCompletionsPerSpan = 4;
        // Minimum raw span that may become an English arc (see header note).
        std::size_t minSpanLength = 3;
    };

    // `lex` must outlive the oracle. nullptr is not accepted.
    explicit EnglishArcOracle(const EnglishLexicon *lex);
    EnglishArcOracle(const EnglishLexicon *lex, Config config);

    std::vector<SegmentationArc> arcsAt(std::string_view raw, size_t begin,
                                        size_t maxSpan) const override;

    const Config &config() const { return config_; }

private:
    const EnglishLexicon *lex_;
    Config config_;
};

} // namespace pinyin

#endif // _FCITX_PINYIN_MIXED_ENGLISH_ENGLISHARCORACLE_H_
