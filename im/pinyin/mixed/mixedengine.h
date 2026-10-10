/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_PINYIN_MIXED_MIXEDENGINE_H_
#define _FCITX_PINYIN_MIXED_MIXEDENGINE_H_

#include <cstddef>

#include <string>
#include <string_view>
#include <vector>

#include "mixedcompositionstate.h"
#include "mixedsegmentation.h"
#include "rewriter.h"
#include "segmentcomposer.h"
#include "unifiedranker.h"

namespace pinyin {

// M2+ LM-state-aware single-pass bounded mixed search, as an std-only hook so
// this orchestrator keeps no LibIME dependency. The implementation lives in
// mixed/m2psearch.cpp; the fusion seam owns it and passes it to `compute`.
// A non-null hook replaces the std cost search with the LM-state pass over the
// whole raw.
class MixedLmStateSearchHook {
public:
    virtual ~MixedLmStateSearchHook() = default;
    // Top-K complete paths over the whole raw, priced against the same
    // admitted English arcs the cost search consumes.
    virtual std::vector<SegmentationPath>
    search(std::string_view raw, const IEnglishArcOracle &english,
           size_t topK) const = 0;
};

// Merges the arc output of several IEnglishArcOracle implementations at each
// begin position. Used to combine the System Lexicon, User Lexicon, and
// Correction Oracle into the single interface the segmentation search expects.
// Emitters are consulted in insertion order and their sourceLocalRank counters
// stay source-local: each underlying oracle still numbers its own arcs, and the
// composite does not renumber across sources (the two-level ranking contract
// requires source-local ranks to remain comparable within one source only).
//
// This class is std-only and independent of any decoding backend.
class CompositeEnglishArcOracle : public IEnglishArcOracle {
public:
    CompositeEnglishArcOracle() = default;
    // Sources are referenced, not owned. Order is significant only insofar as
    // it decides the order arcs appear in the merged vector for equal-cost
    // ties (SegmentationPath is not aware of this — cost is the only
    // discriminator).
    void addSource(const IEnglishArcOracle *oracle);
    void clearSources() { sources_.clear(); }
    std::size_t sourceCount() const { return sources_.size(); }

    std::vector<SegmentationArc> arcsAt(std::string_view raw, size_t begin,
                                        size_t maxSpan) const override;

private:
    std::vector<const IEnglishArcOracle *> sources_;
};

// std-only orchestrator that ties the Architecture A pipeline together:
// segmentation search -> compose (with a caller-supplied Han resolver) ->
// bounded dedup pool -> cross-source rank -> surface rewriter. Owns no
// decoding backend; the LibIME adapter that populates `IChineseArcOracle` and
// the resolver callback is wired at the pinyin.cpp fusion seam (batch 7B)
// so this class remains independently testable without LibIME.
//
// The engine is intentionally stateless across calls: the caller owns the
// MixedCompositionState and its frozen-prefix selection frontier and passes
// the uncommitted raw suffix into `compute`.
class MixedEngine {
public:
    struct Config {
        MixedSegmentationSearch::Config search;
        CandidatePool::Config pool;
        UnifiedRanker::Weights ranker;
        RewriterConfig rewriter;
    };

    // One source-local commit step derived from a selected UnifiedCandidate.
    // The caller translates each transaction into the matching LibIME
    // primitive at the fusion seam: `selectCandidatesToCursor(idx)` for a
    // Han span, `selectCustom(inputLength, segment, encodedPinyin="")` for an
    // English span. Transactions are emitted in left-to-right raw order and
    // tile the candidate's raw range contiguously (batch 7B invariant).
    struct CommitTransaction {
        std::size_t rawBegin = 0;
        std::size_t rawEnd = 0;
        std::string output;
        SegmentSource source = SegmentSource::Chinese;
        CandidateProvenance provenance = CandidateProvenance::Exact;
    };

    MixedEngine();
    explicit MixedEngine(Config config);

    // Full pipeline. `hanResolver` is called only for arcs whose
    // `resolvedOutput` is empty (typically Chinese partial-span arcs).
    // English arcs come pre-resolved from their oracles and are passed
    // through. `m2p` is the LibIME-side LM-state search the fusion seam
    // supplies; without it the pipeline runs the std bounded cost search,
    // which is also what the LibIME-free unit fixtures drive.
    // Returns the ranked + rewritten pool in descending final score order.
    std::vector<UnifiedCandidate>
    compute(std::string_view raw, const IChineseArcOracle &chinese,
            const IEnglishArcOracle &english, const ArcResolver &hanResolver,
            const MixedLmStateSearchHook *m2p = nullptr) const;

    // Split a candidate into per-span source-local commit transactions.
    // Order matches the candidate's segment list exactly.
    static std::vector<CommitTransaction>
    planCommit(const UnifiedCandidate &candidate);

    // Advance `state` to freeze `candidate` as the new committed prefix.
    // Verifies the candidate's segments tile [selectionFrontier, rawLen)
    // contiguously before mutating; returns false and leaves state
    // unchanged on any mismatch. The caller is responsible for running the
    // LibIME-side planCommit transactions in lock-step with this state
    // update so raw↔output alignment stays consistent after the write.
    static bool applyToState(MixedCompositionState &state,
                             const UnifiedCandidate &candidate);

    const Config &config() const { return config_; }

    // Batch 9: update the Rewriter's presentation policy at runtime (e.g.
    // after `MixedAutoSpacing` toggles) without rebuilding the engine or
    // losing its segmentation-search / ranker state. Only the rewriter is
    // re-inited; pool cap, search beam and ranker weights remain frozen for
    // the life of the engine. The Rewriter is stateless so this swap is
    // safe mid-session.
    void updateRewriterConfig(RewriterConfig rewriter) {
        config_.rewriter = std::move(rewriter);
        rewriter_ = Rewriter(config_.rewriter);
    }

private:
    Config config_;
    MixedSegmentationSearch search_;
    SegmentComposer composer_;
    UnifiedRanker ranker_;
    Rewriter rewriter_;
};

} // namespace pinyin

#endif // _FCITX_PINYIN_MIXED_MIXEDENGINE_H_
