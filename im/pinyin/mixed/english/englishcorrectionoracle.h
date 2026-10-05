/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_PINYIN_MIXED_ENGLISH_ENGLISHCORRECTIONORACLE_H_
#define _FCITX_PINYIN_MIXED_ENGLISH_ENGLISHCORRECTIONORACLE_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "../mixedsegmentation.h"
#include "englishlexicon.h"

namespace pinyin {

// Bounded English correction oracle. V1 restricts correction to a small,
// evidence-gated neighborhood so we do not silently rewrite intentional
// words (§17.4 "false correction"). Specifically:
//
//   * Substitution of exactly one ASCII letter with a QWERTY-adjacent key.
//   * Transposition of exactly two adjacent ASCII letters.
//
// No insertions, no deletions. Missing letters are already covered by the
// batch-3 EnglishArcOracle Completion path; those provenance channels stay
// distinct so UnifiedRanker can weight them differently (§19).
//
// Neighbor generation is bounded: for a span of length L we generate at
// most L * K substitution neighbors (K is the average adjacency fanout of
// the QWERTY row, small) plus (L - 1) transposition neighbors, then look
// each up in the system lexicon by folded ASCII. This makes cost linear in
// L and independent of total lexicon size — no scan of the whole dictionary.
//
// Correction NEVER outranks an Exact hit on the same span: if the raw span
// itself folds to a lexicon key, the correction oracle emits no arc. This
// is the false-correction guard for intentional words (§17.4).
//
// Confidence is source-local in [0, 0.85]; boundary confidence is fixed at
// 0.55 because a correction is by definition an uncertain word boundary.
class EnglishCorrectionOracle final : public IEnglishArcOracle {
public:
    struct Config {
        // Per-span cap on correction arcs. Internal knob, not user-exposed.
        std::size_t maxCorrectionsPerSpan = 3;
        // Only correction spans strictly longer than this are attempted, to
        // avoid over-eager corrections on 1-2 byte spans that are legitimately
        // short words or valid prefixes. Set to 3 by default.
        std::size_t minSpanLength = 3;
    };

    EnglishCorrectionOracle(const EnglishLexicon *systemLex, Config config);
    explicit EnglishCorrectionOracle(const EnglishLexicon *systemLex);

    std::vector<SegmentationArc> arcsAt(std::string_view raw, size_t begin,
                                        size_t maxSpan) const override;

    // QWERTY adjacency lookup: returns the set of letters physically
    // adjacent to `c` on a QWERTY keyboard. Folded lowercase. Exposed for
    // unit tests to verify layout coverage; production callers should not
    // depend on ordering.
    static const std::vector<char> &qwertyAdjacent(char c);

    // Exposed for tests: the bounded neighbor enumeration for a folded
    // span. Includes substitution neighbors (adjacent-key only) and
    // transposition neighbors, deduplicated, excluding the input itself.
    static std::vector<std::string> neighbors(std::string_view folded);

    // Edit cost used internally by confidence: substitution = 0.30,
    // transposition = 0.20, so transposition errors (which are more
    // common) rank slightly higher than substitutions.
    enum class Kind { Substitution, Transposition };
    static float evidenceFor(Kind kind);

    const Config &config() const { return config_; }

private:
    const EnglishLexicon *systemLex_;
    Config config_;
};

} // namespace pinyin

#endif // _FCITX_PINYIN_MIXED_ENGLISH_ENGLISHCORRECTIONORACLE_H_
