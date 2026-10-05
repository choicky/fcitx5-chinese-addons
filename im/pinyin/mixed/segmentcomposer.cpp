/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "segmentcomposer.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace pinyin {

SegmentComposer::SegmentComposer() = default;

bool SegmentComposer::compose(const SegmentationPath &path,
                              UnifiedCandidate &out) const {
    out = UnifiedCandidate{};
    out.segmentationCost = path.cost;
    out.arcCount = path.arcs.size();
    std::string acc;
    std::size_t outCursor = 0;
    std::size_t rawCursor = 0;
    out.segments.reserve(path.arcs.size());
    out.alignment.reserve(path.arcs.size());
    out.provenances.reserve(path.arcs.size());
    out.sources.reserve(path.arcs.size());
    for (const auto &arc : path.arcs) {
        if (arc.resolvedOutput.empty()) {
            out = UnifiedCandidate{};
            return false;
        }
        if (arc.rawBegin != rawCursor) {
            // Non-contiguous path; composition must be over a valid tiling.
            out = UnifiedCandidate{};
            return false;
        }
        MixedSegment seg;
        seg.rawBegin = arc.rawBegin;
        seg.rawEnd = arc.rawEnd;
        seg.source = arc.source;
        seg.provenance = arc.provenance;
        seg.output = arc.resolvedOutput;
        seg.sourceLocalRank = arc.sourceLocalRank;
        seg.confidence = arc.confidence;
        seg.selected = false;
        RawOutputSpan span;
        span.rawBegin = arc.rawBegin;
        span.rawEnd = arc.rawEnd;
        span.outputBegin = outCursor;
        acc.append(arc.resolvedOutput);
        outCursor += arc.resolvedOutput.size();
        span.outputEnd = outCursor;
        out.segments.push_back(std::move(seg));
        out.alignment.push_back(span);
        out.provenances.push_back(arc.provenance);
        out.sources.push_back(arc.source);
        rawCursor = arc.rawEnd;
    }
    out.composedText = std::move(acc);
    return true;
}

std::vector<UnifiedCandidate>
SegmentComposer::composeAll(const std::vector<SegmentationPath> &paths,
                            std::size_t maxCandidates) const {
    std::vector<UnifiedCandidate> out;
    for (const auto &p : paths) {
        if (maxCandidates != 0 && out.size() >= maxCandidates) {
            break;
        }
        UnifiedCandidate c;
        if (compose(p, c)) {
            out.push_back(std::move(c));
        }
    }
    return out;
}

CandidatePool::CandidatePool() : CandidatePool(Config{}) {}

CandidatePool::CandidatePool(Config config) : config_(config) {}

bool CandidatePool::insert(UnifiedCandidate c) {
    // Dedup by exact composedText.
    for (const auto &existing : items_) {
        if (existing.composedText == c.composedText) {
            return false;
        }
    }
    items_.push_back(std::move(c));
    return true;
}

void CandidatePool::evictToCap() {
    if (config_.maxSize == 0) {
        items_.clear();
        return;
    }
    if (items_.size() > config_.maxSize) {
        items_.resize(config_.maxSize);
    }
}

} // namespace pinyin
