/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "unifiedranker.h"

#include <algorithm>
#include <cmath>

namespace pinyin {

RankFeatures FeatureBuilder::build(const UnifiedCandidate &c) const {
    RankFeatures f;
    f.arcCount = c.segments.size();
    f.segmentationCost = c.segmentationCost;
    if (f.arcCount == 0) {
        return f;
    }
    double confSum = 0.0;
    double boundarySum = 0.0;
    float minConf = 1.0F;
    bool first = true;
    SegmentSource prev;
    for (std::size_t i = 0; i < c.segments.size(); ++i) {
        const auto &seg = c.segments[i];
        switch (seg.source) {
        case SegmentSource::Chinese:
            ++f.chineseArcs;
            break;
        case SegmentSource::English:
            ++f.englishArcs;
            break;
        }
        switch (seg.provenance) {
        case CandidateProvenance::Exact:
            ++f.exactArcs;
            break;
        case CandidateProvenance::Canonical:
            ++f.canonicalArcs;
            break;
        case CandidateProvenance::Completion:
            ++f.completionArcs;
            break;
        case CandidateProvenance::Correction:
            ++f.correctionArcs;
            break;
        case CandidateProvenance::CustomPhrase:
            ++f.userArcs;
            break;
        case CandidateProvenance::Unknown:
            break;
        }
        // Recompute minArcConfidence from the arc-level confidence field
        // stored on the segment (which mirrors SegmentationArc.confidence).
        const float arcConf = seg.confidence;
        if (first || arcConf < minConf) {
            minConf = arcConf;
        }
        first = false;
        confSum += static_cast<double>(arcConf);
        if (i > 0) {
            if (seg.source != prev) {
                ++f.boundarySwitches;
            }
        }
        prev = seg.source;
        // Boundary confidence is not directly stored on MixedSegment; use
        // 1.0 as a placeholder when provenance is Exact or CustomPhrase
        // (strong boundary), lower when Completion/Correction (weak
        // boundary). This keeps MixedSegment free of arc-only fields while
        // still giving the ranker a usable signal.
        double b = 0.85;
        switch (seg.provenance) {
        case CandidateProvenance::Exact:
            b = 1.00;
            break;
        case CandidateProvenance::CustomPhrase:
            b = 0.90;
            break;
        case CandidateProvenance::Canonical:
            b = 0.95;
            break;
        case CandidateProvenance::Completion:
            b = 0.60;
            break;
        case CandidateProvenance::Correction:
            b = 0.55;
            break;
        case CandidateProvenance::Unknown:
            b = 0.70;
            break;
        }
        boundarySum += b;
    }
    f.meanArcConfidence =
        static_cast<float>(confSum / static_cast<double>(f.arcCount));
    f.meanBoundaryConfidence =
        static_cast<float>(boundarySum / static_cast<double>(f.arcCount));
    f.minArcConfidence = minConf;
    return f;
}

UnifiedRanker::UnifiedRanker() : UnifiedRanker(Weights{}) {}

UnifiedRanker::UnifiedRanker(Weights weights) : weights_(weights) {}

float UnifiedRanker::score(const RankFeatures &f) const {
    if (f.arcCount == 0) {
        return -1e9F;
    }
    const double n = static_cast<double>(f.arcCount);
    const double exactShare = static_cast<double>(f.exactArcs) / n;
    const double canonicalShare = static_cast<double>(f.canonicalArcs) / n;
    const double completionShare = static_cast<double>(f.completionArcs) / n;
    const double correctionShare = static_cast<double>(f.correctionArcs) / n;
    const double userShare = static_cast<double>(f.userArcs) / n;
    double s = static_cast<double>(weights_.wExact) * exactShare +
               static_cast<double>(weights_.wCanonical) * canonicalShare +
               static_cast<double>(weights_.wCompletion) * completionShare +
               static_cast<double>(weights_.wCorrection) * correctionShare +
               static_cast<double>(weights_.wUser) * userShare +
               static_cast<double>(weights_.wConfidence) *
                   static_cast<double>(f.meanArcConfidence) -
               static_cast<double>(weights_.wMinConfidencePenalty) *
                   (1.0 - static_cast<double>(f.minArcConfidence)) +
               static_cast<double>(weights_.wBoundary) *
                   static_cast<double>(f.meanBoundaryConfidence) -
               static_cast<double>(weights_.wCost) * f.segmentationCost;
    return static_cast<float>(s);
}

std::vector<std::size_t>
UnifiedRanker::rankIndices(const std::vector<UnifiedCandidate> &pool) const {
    std::vector<std::size_t> idx(pool.size());
    for (std::size_t i = 0; i < idx.size(); ++i) {
        idx[i] = i;
    }
    FeatureBuilder fb;
    std::vector<float> scores(pool.size());
    for (std::size_t i = 0; i < pool.size(); ++i) {
        scores[i] = score(fb.build(pool[i]));
    }
    std::stable_sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) {
        return scores[a] > scores[b];
    });
    return idx;
}

std::vector<UnifiedCandidate>
UnifiedRanker::rank(const std::vector<UnifiedCandidate> &pool) const {
    const auto idx = rankIndices(pool);
    std::vector<UnifiedCandidate> out;
    out.reserve(idx.size());
    for (const auto i : idx) {
        out.push_back(pool[i]);
    }
    return out;
}

} // namespace pinyin
