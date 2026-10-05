/*
 * SPDX-FileCopyrightText: 2026 Fcitx5 Fusion contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "rewriter.h"

#include <utility>

namespace pinyin {

Rewriter::Rewriter() : Rewriter(RewriterConfig{}) {}

Rewriter::Rewriter(RewriterConfig config) : config_(std::move(config)) {}

UnifiedCandidate Rewriter::rewrite(const UnifiedCandidate &in) const {
    UnifiedCandidate out = in;
    if (!config_.autoSpaceAtBoundary || out.segments.size() <= 1) {
        return out;
    }
    std::string acc;
    std::size_t outCursor = 0;
    out.alignment.clear();
    out.alignment.reserve(out.segments.size());
    SegmentSource prev = out.segments.front().source;
    for (std::size_t i = 0; i < out.segments.size(); ++i) {
        const auto &seg = out.segments[i];
        RawOutputSpan span;
        span.rawBegin = seg.rawBegin;
        span.rawEnd = seg.rawEnd;
        if (i > 0 && seg.source != prev) {
            acc.append(config_.boundarySeparator);
            outCursor += config_.boundarySeparator.size();
        }
        span.outputBegin = outCursor;
        acc.append(seg.output);
        outCursor += seg.output.size();
        span.outputEnd = outCursor;
        out.alignment.push_back(span);
        prev = seg.source;
    }
    out.composedText = std::move(acc);
    return out;
}

std::vector<UnifiedCandidate>
Rewriter::rewritePool(const std::vector<UnifiedCandidate> &ranked) const {
    std::vector<UnifiedCandidate> out;
    out.reserve(ranked.size());
    for (const auto &c : ranked) {
        out.push_back(rewrite(c));
    }
    return out;
}

} // namespace pinyin
