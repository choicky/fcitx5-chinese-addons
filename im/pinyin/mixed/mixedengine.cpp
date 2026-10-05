/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "mixedengine.h"

#include <utility>

namespace pinyin {

void CompositeEnglishArcOracle::addSource(const IEnglishArcOracle *oracle) {
    if (oracle == nullptr) {
        return;
    }
    sources_.push_back(oracle);
}

std::vector<SegmentationArc>
CompositeEnglishArcOracle::arcsAt(std::string_view raw, size_t begin,
                                  size_t maxSpan) const {
    std::vector<SegmentationArc> out;
    for (const auto *s : sources_) {
        if (s == nullptr) {
            continue;
        }
        auto part = s->arcsAt(raw, begin, maxSpan);
        for (auto &a : part) {
            out.push_back(std::move(a));
        }
    }
    return out;
}

MixedEngine::MixedEngine() : MixedEngine(Config{}) {}

MixedEngine::MixedEngine(Config config)
    : config_(config), search_(config_.search), ranker_(config_.ranker),
      rewriter_(config_.rewriter) {}

std::vector<UnifiedCandidate>
MixedEngine::compute(std::string_view raw, const IChineseArcOracle &chinese,
                     const IEnglishArcOracle &english,
                     const ArcResolver &hanResolver) const {
    if (raw.empty()) {
        return {};
    }
    auto paths = search_.search(raw, chinese, english);
    if (paths.empty()) {
        return {};
    }
    const std::size_t cap = config_.pool.maxSize;
    auto composed = composer_.composeAll(paths, hanResolver, cap);
    if (composed.empty()) {
        return {};
    }
    CandidatePool pool(config_.pool);
    for (auto &c : composed) {
        pool.insert(std::move(c));
    }
    pool.evictToCap();
    auto ranked = ranker_.rank(pool.items());
    return rewriter_.rewritePool(ranked);
}

std::vector<MixedEngine::CommitTransaction>
MixedEngine::planCommit(const UnifiedCandidate &candidate) {
    std::vector<CommitTransaction> out;
    out.reserve(candidate.segments.size());
    for (const auto &seg : candidate.segments) {
        CommitTransaction t;
        t.rawBegin = seg.rawBegin;
        t.rawEnd = seg.rawEnd;
        t.output = seg.output;
        t.source = seg.source;
        t.provenance = seg.provenance;
        out.push_back(std::move(t));
    }
    return out;
}

bool MixedEngine::applyToState(MixedCompositionState &state,
                               const UnifiedCandidate &candidate) {
    if (candidate.segments.empty()) {
        return false;
    }
    const std::string_view remaining = state.remainingRaw();
    const std::size_t frontier = state.selectionFrontier();
    // Candidate must tile the uncommitted suffix contiguously.
    std::size_t cursor = frontier;
    for (const auto &seg : candidate.segments) {
        if (seg.rawBegin != cursor) {
            return false;
        }
        if (seg.rawEnd > frontier + remaining.size()) {
            return false;
        }
        cursor = seg.rawEnd;
    }
    if (cursor != frontier + remaining.size()) {
        return false;
    }
    // Rebase the candidate's segment list to be relative to the suffix
    // (MixedCompositionState::setSegments expects segments covering only
    // [selectionFrontier, rawLen), which the candidate already satisfies
    // because rawBegin/rawEnd are absolute offsets into the same raw string
    // the state owns).
    auto copy = candidate.segments;
    if (!state.setSegments(std::move(copy))) {
        return false;
    }
    return state.consumeSelection(frontier + remaining.size());
}

} // namespace pinyin
