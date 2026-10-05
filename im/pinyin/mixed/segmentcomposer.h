/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_PINYIN_MIXED_SEGMENTCOMPOSER_H_
#define _FCITX_PINYIN_MIXED_SEGMENTCOMPOSER_H_

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "mixedcompositionstate.h"
#include "mixedsegmentation.h"

namespace pinyin {

// Fills the concrete output for an arc whose `resolvedOutput` was intentionally
// left empty by the producing oracle. The primary use is partial-span Chinese
// arcs: the segmentation search must stay std-only and cheap, so the Chinese
// oracle emits arcs as structural evidence and the real Han decode is done
// here against the whole composition. Callers should memoize by (rawBegin,
// rawEnd, sourceLocalRank) so repeated paths sharing an arc do not re-decode.
using ArcResolver = std::function<std::string(const SegmentationArc &)>;

// A single candidate produced from one segmentation path. Holds the concrete
// per-arc composition output joined with any configured separator, the
// arc-level MixedSegments, and the source-local evidence inherited from the
// search path. NOT yet normalized across sources — that is the Unified
// Ranker's job (§19).
struct UnifiedCandidate {
    std::string composedText;
    std::vector<MixedSegment> segments;
    // Raw↔output byte alignment for the composed text (see
    // MixedCompositionState §9). Kept separate from `segments` because Rewriter
    // can insert boundary separators, which invalidates any output offsets
    // stored on segments. The composer and rewriter maintain this alignment as
    // the single source of truth.
    std::vector<RawOutputSpan> alignment;
    // Copy of the arc-level provenance list; useful for FeatureBuilder
    // without walking segments.
    std::vector<CandidateProvenance> provenances;
    std::vector<SegmentSource> sources;
    // Search cost from the segmentation beam (lower is better). Preserved
    // so downstream ranking can incorporate the segmentation confidence
    // distribution rather than only per-arc averages.
    double segmentationCost = 0.0;
    // Number of arcs.
    std::size_t arcCount = 0;
};

// SegmentComposer turns a segmentation path into a candidate carrying the
// concrete per-span resolved outputs. Composition here is deliberately
// context-free: each arc carries its own `resolvedOutput` from the arc
// oracle, and the composer concatenates them. Any LM-based whole-sentence
// re-decoding for a Chinese arc is handled by the arc emitter producing
// multiple parallel top-N arcs on the same span (each with a distinct
// sourceLocalRank), not by the composer.
class SegmentComposer {
public:
    SegmentComposer();

    // Compose one path into a candidate. Returns false if any arc on the
    // path is unresolved (empty `resolvedOutput`) so the caller can decide
    // whether to skip; unresolved arcs must not silently compose into an
    // empty string.
    bool compose(const SegmentationPath &path, UnifiedCandidate &out) const;

    // Compose one path resolving empty `resolvedOutput` arcs via `resolver`.
    // A null resolver behaves like the overload above (empty output -> reject).
    bool compose(const SegmentationPath &path, const ArcResolver &resolver,
                 UnifiedCandidate &out) const;

    // Compose a set of paths, dropping any that contain unresolved arcs.
    // Bounded by `maxCandidates` (0 == no cap).
    std::vector<UnifiedCandidate>
    composeAll(const std::vector<SegmentationPath> &paths,
               std::size_t maxCandidates) const;

    // Resolver-aware composeAll.
    std::vector<UnifiedCandidate>
    composeAll(const std::vector<SegmentationPath> &paths,
               const ArcResolver &resolver, std::size_t maxCandidates) const;
};

// Deduplicating, bounded candidate pool. Insertion preserves the first
// occurrence of each distinct `composedText` (provenance-preserving dedup:
// the FIRST insertion's provenance list is kept, since the segmentation
// beam explores paths in cost order and a later insertion for the same
// text is at best equally plausible). If the caller wants a strict
// "keep-best" policy, they should insert in cost-ascending order.
class CandidatePool {
public:
    struct Config {
        std::size_t maxSize = 32;
    };

    CandidatePool();
    explicit CandidatePool(Config config);

    // Insert one candidate. Returns true iff it was added (dedup by exact
    // composedText). If `maxSize` is reached, the caller must call
    // `evict` explicitly or use the `insertBest` variant that respects
    // existing size and drops on cap.
    bool insert(UnifiedCandidate c);

    std::size_t size() const { return items_.size(); }
    bool empty() const { return items_.empty(); }
    const std::vector<UnifiedCandidate> &items() const { return items_; }

    // Drop the trailing item(s) beyond maxSize. Callers typically invoke
    // this after a bulk insert to enforce the pool bound before ranking.
    void evictToCap();

    void clear() { items_.clear(); }

    const Config &config() const { return config_; }

private:
    Config config_;
    std::vector<UnifiedCandidate> items_;
};

} // namespace pinyin

#endif // _FCITX_PINYIN_MIXED_SEGMENTCOMPOSER_H_
