/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_PINYIN_MIXED_UNIFIEDRANKER_H_
#define _FCITX_PINYIN_MIXED_UNIFIEDRANKER_H_

#include <cstddef>
#include <vector>

#include "mixedcompositionstate.h"
#include "segmentcomposer.h"

namespace pinyin {

// Cross-source features extracted from a UnifiedCandidate. All fields are
// computed from source-local evidence that has already been normalized to
// [0, 1] by the producing oracle; UnifiedRanker is allowed to combine them
// but not to re-introduce raw LM floats (§19).
struct RankFeatures {
    std::size_t arcCount = 0;
    std::size_t chineseArcs = 0;
    std::size_t englishArcs = 0;
    std::size_t exactArcs = 0;
    std::size_t canonicalArcs = 0;
    std::size_t completionArcs = 0;
    std::size_t correctionArcs = 0;
    std::size_t userArcs = 0;
    // Language-source switches across the path (adjacent arcs with
    // different SegmentSource). NOT penalized in the default weights (§11);
    // recorded only so ranker tuning can observe it.
    std::size_t boundarySwitches = 0;
    float meanArcConfidence = 0.0F;
    float minArcConfidence = 0.0F;
    float meanBoundaryConfidence = 0.0F;
    double segmentationCost = 0.0;
};

class FeatureBuilder {
public:
    RankFeatures build(const UnifiedCandidate &c) const;
};

// Classical, deterministic cross-source ranker. No learned model, no
// neural net (Architecture A prohibitions). Combines source-local evidence
// with a small hand-tuned weight vector documented below. Weights are
// internal (not user exposed) and reviewed via unit tests.
//
// Score = w_exact * exactShare
//       + w_canonical * canonicalShare
//       + w_completion * completionShare
//       + w_correction * correctionShare
//       + w_user * userShare
//       + w_confidence * meanArcConfidence
//       - w_minConfidencePenalty * (1 - minArcConfidence)
//       + w_boundary * meanBoundaryConfidence
//       - w_cost * segmentationCost
//       + w_wholeSpan * (arcCount == 1 ? 1 : 0)
//
// All shares are counts / arcCount (0 if arcCount is 0, which the caller
// should filter). Higher score = better. The segmentation cost term keeps
// the beam search's own preference alive even after re-normalization.
class UnifiedRanker {
public:
    struct Weights {
        float wExact = 0.30F;
        float wCanonical = 0.22F;
        float wCompletion = 0.14F;
        float wCorrection = 0.06F;
        float wUser = 0.10F;
        float wConfidence = 0.35F;
        float wMinConfidencePenalty = 0.15F;
        float wBoundary = 0.10F;
        float wCost = 0.20F;
        // Bonus for candidates whose whole raw suffix is explained by a
        // single arc. Production corpus evidence: with the full SCOWL-backed
        // lexicon, a fragmented tiling can outscore the whole-span reading
        // on source-local evidence alone (e.g. "iphon" → Completion
        // "iPhone" + Exact "on" = 0.4916 beats Completion "iPhone" over the
        // whole span = 0.3986), and no wCost value can close that gap
        // (reversal would need wCost > 1.44, which destroys cost ordering
        // everywhere else). The bonus encodes the prior that a single
        // dictionary interpretation of the whole suffix is stronger
        // evidence than an ad-hoc multi-arc tiling.
        float wWholeSpan = 0.15F;
    };

    UnifiedRanker();
    explicit UnifiedRanker(Weights weights);

    // Rank a pool by descending final score. Ties preserve insertion order
    // (stable_sort), so the caller's cost-ascending insert convention
    // yields a deterministic final order.
    std::vector<UnifiedCandidate>
    rank(const std::vector<UnifiedCandidate> &pool) const;

    // Same as rank() but returns indices into the caller's pool vector.
    std::vector<std::size_t>
    rankIndices(const std::vector<UnifiedCandidate> &pool) const;

    // Bounded cross-source insertion class for product placement (final
    // ranking closure; supersedes the blanket "classical pure-Han coverage
    // puts every English candidate behind the whole Chinese list" rule).
    // A candidate belongs to this class when it explains the WHOLE raw
    // stream with a SINGLE English arc whose provenance is Canonical or
    // CustomPhrase. Pure property test, no score comparison and no word
    // list (§3): Canonical re-spelling only exists when the dictionary
    // surface differs from the typed span in case form (chatgpt ->
    // ChatGPT, macos -> macOS, openwrt -> OpenWrt), which ambiguous
    // lowercase overlap words (win/long/game/pin/an/ai: surface == raw,
    // hence Exact) can never satisfy. Completion/Correction arcs are
    // excluded: they extrapolate beyond typed evidence. CustomPhrase
    // inclusion lets a user-confirmed word move into the class by
    // learning alone. Everything outside the class keeps the existing
    // placement semantics (pool-front mixed lead, else behind classical).
    static bool isBoundedInsertionClass(const UnifiedCandidate &c,
                                        std::size_t rawSize);

    // Exposed for tests so assertions can pin the score directly rather
    // than relying only on ordering.
    float score(const RankFeatures &f) const;

    const Weights &weights() const { return weights_; }

private:
    Weights weights_;
};

} // namespace pinyin

#endif // _FCITX_PINYIN_MIXED_UNIFIEDRANKER_H_
