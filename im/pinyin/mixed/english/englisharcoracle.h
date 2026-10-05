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
//                key order (bounded).
// Correction arcs (Batch 5) and user-lexicon arcs (Batch 4) are emitted by
// separate oracles and merged downstream, keeping this oracle's job narrow.
//
// Confidence values are source-local (§19); UnifiedRanker re-normalizes
// across sources. No hard minimum prefix is enforced (§13).
class EnglishArcOracle final : public IEnglishArcOracle {
public:
    struct Config {
        // Bounded fan-out per raw span for completion arcs. NOT a user
        // exposed knob; internal to keep the DAG cost predictable.
        std::size_t maxCompletionsPerSpan = 4;
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
